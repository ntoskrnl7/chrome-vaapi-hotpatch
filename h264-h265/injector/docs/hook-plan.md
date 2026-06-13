# HEVC Delegate Injection Hook Plan

## Premise

The official Chrome binary should be treated as missing the user's
`H265VaapiVideoEncoderDelegate` implementation.

The reference.implementation lives in Chromium source here:

```text
media/gpu/vaapi/h265_vaapi_video_encoder_delegate.{h,cc}
media/gpu/vaapi/vaapi_video_encode_accelerator.cc
media/video/video_encode_accelerator_adapter.cc
media/base/video_encoder.{h,cc}
media/mojo/mojom/video_encode_accelerator.mojom
third_party/blink/renderer/modules/webcodecs/video_encoder.cc
```

## What The Delegate Does

It is not a boolean switch. It owns the HEVC encode state:

- builds VPS/SPS/PPS and packed slice headers
- fills `VAEncSequenceParameterBufferHEVC`
- fills `VAEncPictureParameterBufferHEVC`
- fills `VAEncSliceParameterBufferHEVC`
- manages IDR/P-frame state and POC
- handles Intel iHD-specific CQP and placeholder/header behavior
- injects expected HEVC bitstream bytes when required

## Hook Boundary Candidates

### Candidate A: `VaapiVideoEncodeAccelerator::InitializeTask`

Best semantic hook. This is where the official implementation chooses a codec
delegate for H264/VP8/VP9/AV1. The reference.s patch adds HEVC delegate selection
here.

Problem: this is an internal C++ method in a stripped monolithic Chrome binary.
No exported symbol can be interposed directly with plain `LD_PRELOAD`.

### Candidate B: `VaapiVideoEncoderDelegate::Encode`

This is a common delegate virtual call path after a delegate exists.

Problem: if official Chrome never constructs an HEVC delegate, this hook is too
late.

### Candidate C: VAAPI call interposition

Intercepting `vaCreateBuffer`, `vaRenderPicture`, etc. can observe or modify
VAAPI traffic.

Problem: official Chrome is not submitting HEVC encode frame buffers. VAAPI
interposition cannot create the missing Chromium-side frame/bitstream lifecycle
by itself.

### Candidate D: runtime trampoline into external `.so`

Patch a small internal function in Chrome to jump to an injected `.so`.

This is the most plausible binary route:

1. `LD_PRELOAD` the injector.
2. Injector locates Chrome `.text` and build-id.
3. Injector pattern-matches a stable function near `VaapiVideoEncodeAccelerator`.
4. Injector mprotects the page and installs a trampoline.
5. Trampoline forwards HEVC initialization/encode to the external delegate.

This still requires duplicating enough Chromium object layout to safely read
`VideoEncodeAccelerator::Config`, `EncodeJob`, `ScopedVABuffer`,
`CodecPicture`, and `VaapiWrapper` fields. That is the main risk.

## Official Chrome 146 Binary Landmarks

Target checked:

```text
/opt/google/chrome/chrome
Google Chrome 146.0.7680.177
Build ID: 5570e100d5af8a240754617d8bce01d4a868040d
```

The binary is stripped, so these are not exported symbols. They are addresses
found by matching surviving strings in `.rodata` to RIP-relative references in
`.text`.

Use:

```bash
cd <repo>/h264-h265/injector
tools/scan_chrome_hevc_hook_candidates.py /opt/google/chrome/chrome
```

Current landmarks:

```text
0x8c606d0  VaapiVideoEncodeAccelerator constructor
0x8c60660  VaapiVideoEncodeAccelerator::GetSupportedProfiles vtable slot
0x8c60d10  likely VaapiVideoEncodeAccelerator::Initialize(...)
0x8c617a0  VaapiVideoEncodeAccelerator::InitializeTask(...)
0x8c6156f  "Unsupported output profile "
0x8c61bd2  "Failed initializing VAAPI for profile "
0x8c61e76  "Failed initializing encoder. config: "
0x8c6242a  delegate switch unsupported-codec trap, int3/ud2
0x2367d08  delegate switch table HEVC entry, VMA
0x8c62314  "media::VaapiVideoEncodeAccelerator"
0x8c678e0  likely VAAPI error-reporting callback factory
0x8c6796f  "VaapiVideoEncodeAcceleratorDelegate error"
```

The block around `0x8c60d10..0x8c62420` corresponds closely to the source-level
`VaapiVideoEncodeAccelerator::Initialize()` / `InitializeTask()` area. It
contains the supported-profile rejection, VAAPI wrapper creation failure,
delegate initialization failure, and the implementation-name assignment.

This is the first real trampoline region. It is still not enough to jump
straight into an external HEVC delegate, because the external library needs a
stable ABI for Chromium's internal objects.

## Trampoline Probe Result

Use:

```bash
cd <repo>/h264-h265/injector
./run-trampoline-probe-chrome.sh
```

Updated status:

- The injected `.so` can patch Chrome code from the zygote so the GPU process
  inherits the patched text page.
- The `VaapiVideoEncodeAccelerator` constructor trampoline hits in the GPU
  process and safely returns to Chrome.
- The `Initialize` trampoline installs and is reached after the
  `GetSupportedProfiles` vector is postprocessed.
- The `GetSupportedProfiles` vtable slot was confirmed by relocation records:

```text
R_X86_64_RELATIVE f8ad600 -> 8c60660
R_X86_64_RELATIVE f8ad608 -> 8c60d10
```

The `GetSupportedProfiles` crash was fixed. The original absolute jump used
`rax`, clobbering a live value from the copied prologue. The trampoline now
uses `r11` and preserves a 19-byte instruction boundary for that function.

Observed official Chrome behavior with the HEVC profile spoof enabled:

```text
VEA profiles count=4 capacity=4 has_hevc=0 source=1 source_rate_modes=0x03
spoofed HEVC Main supported profile
VideoEncoder.isConfigSupported(...) => supported:true
Initialize(): output_profile: hevc main, bitrate: VBR ...
EncoderStatus::7
```

The WebCodecs test intentionally has no `bitrateMode` field:

```text
test/h265-encode-minimal.html
codec: hvc1.1.6.L93
hardwareAcceleration: prefer-hardware
latencyMode: realtime
```

Chrome fills in `bitrateMode:"variable"` and still reaches `Initialize()`.
Therefore `bitrateMode:"constant"` is not required for the current blocker.

Two additional experiments exist in `src/trampoline_probe.c`:

- `CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH=1` changes the VP8/VP9 mode
  selection range compare so enum value 8 can also pass.
- `CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT=1` force-jumps a later mode
  selection rejection to the accepted block.

Those patches were not enough by themselves. The next blocking checks were in
the earlier `Initialize()` path and in `VaapiWrapper::Create()`:

- `CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK=1` changes the accepted
  `VideoCodec` bitmask from `0x4c2` to `0x5c2`, adding enum value 8
  (`VideoCodec::kHEVC`).
- `CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION=1` bypasses the
  official Chrome guard that rejects non-H264 VBR. The test page still omits
  `bitrateMode`; Chrome fills `bitrateMode:"variable"`.
- `CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS=1` bypasses the
  `VASupportedProfiles` table miss in `VaapiWrapper::Create()` so HEVC can
  attempt real VA wrapper initialization.

After correcting the profile-table bypass target, official Chrome reaches and
successfully returns from:

```text
hit VEA initialize task trampoline
vaCreateConfig call profile=17 entrypoint=6 attribs=3
vaCreateConfig ret=0 profile=17 entrypoint=6 config=1072
VaapiWrapper::Create mode=1 va_profile=17 status=0
CreateForVideoCodec mode=1 profile=16 status=0
```

That is the first observed official Chrome HEVC encode VAAPI config success in
this experiment.

The current blocker moved again: the GPU process exits with `SIGSEGV` during
the delegate switch. Disassembly of Chrome 146 shows `VideoCodec::kHEVC` lands
on the unsupported trap:

```text
0x2367d08  delegate switch table HEVC entry
0x8c6242a  int3
0x8c6242b  ud2
```

A proof-only patch was added:

```text
CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264=1
```

It routes the HEVC switch entry to the H264 delegate arm. With that enabled,
Chrome no longer traps; it reaches `H264VaapiVideoEncoderDelegate::Initialize()`
with `output_profile: hevc main` and reports a normal
`Failed initializing encoder` error. This confirms the next real requirement:
an injected HEVC delegate object, not more VAAPI capability spoofing.

## Delegate Bundle Probe

A probe `.so` has been built from the working Electron object files:

```text
delegate-injection/bin/libh265_delegate_bundle_probe.so
```

It links in:

```text
h265_vaapi_video_encoder_delegate.o
vaapi_video_encoder_delegate.o
vaapi_wrapper.o
h265_builder.o
h265_parser.o
h26x_annex_b_bitstream_builder.o
gpu_video_encode_accelerator_helpers.o
video_encode_accelerator.o
video_bitrate_allocation.o
bitrate.o
gfx size/color_space support
base/libbase.a
partition_alloc support
libc++/libc++abi
```

The bundle is loadable by official Chrome:

```bash
LD_PRELOAD=delegate-injection/bin/libh265_delegate_bundle_probe.so \
  /usr/bin/google-chrome-stable --version
```

This only proves loadability. It does not yet construct and install an
`H265VaapiVideoEncoderDelegate` into Chrome's `encoder_` field.

## Next Practical Hook Shape

The safest next experiment is a two-stage hook:

1. Keep the `GetSupportedProfiles` postprocess hook; it is now the working way
   to expose HEVC Main to WebCodecs.
2. Add precise branch probes around `InitializeTask` error exits to identify
   the exact `EncoderStatus::7` source branch.
3. Once that branch is identified, hook the HEVC branch inside
   `InitializeTask` and redirect only the
   missing delegate creation to a delegate object supplied by the injected
   library.

Trying to synthesize the entire encode lifecycle only with `vaCreateBuffer` /
`vaRenderPicture` interposition is the wrong layer: official Chrome never
submits HEVC encode jobs today.

## First Milestone

The first milestone is not full HEVC encode. It is proving that an injected
library is loaded into Chrome's GPU process and sees HEVC VAAPI profile
discovery.

Use:

```bash
cd <repo>/h264-h265/injector
./run-scout-chrome.sh
```

## Scout Result

Status: passed.

The scout `.so` reaches Chrome's GPU process when launched through the helper.
Plain PLT interposition was not enough because Chrome resolves libva through
`dlsym()`. The scout now redirects selected `dlsym()` lookups.

Observed official Chrome GPU-process log:

```text
dlsym redirect vaCreateConfig
dlsym redirect vaQueryConfigEntrypoints
dlsym redirect vaQueryConfigProfiles
vaQueryConfigProfiles ret=0 count=15 hevc_main=1 hevc_main10=1
vaQueryConfigEntrypoints profile=17 ret=0 count=3 enc_slice=1
vaQueryConfigEntrypoints profile=18 ret=0 count=2 enc_slice=1
```

Chrome then creates encode configs for H.264/VP8, but not HEVC encode configs:

```text
vaCreateConfig profile=6 entrypoint=6
vaCreateConfig profile=7 entrypoint=6
vaCreateConfig profile=13 entrypoint=6
vaCreateConfig profile=14 entrypoint=6
```

This confirms the driver advertises HEVC encode, and the break is above raw
VAAPI capability discovery.
