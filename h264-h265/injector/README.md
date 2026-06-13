# Chrome HEVC Delegate Injector

This directory is for the `.so` injection path.

The working Electron implementation added a real
`H265VaapiVideoEncoderDelegate`. Official Chrome should be assumed not to have
that delegate. Therefore this path is not a small machine-code patch; it needs
an injected library plus trampolines into Chrome's GPU-process encode path.

## Current Goal

Build the work in layers:

1. Prove the injector is loaded into Chrome's GPU process.
2. Observe HEVC VAAPI capability discovery from inside the injected library.
3. Locate a stable internal hook point near `VaapiVideoEncodeAccelerator`
   profile discovery or initialization.
4. Port the delegate logic into the injected library and keep the verified
   encode path reproducible.

## Why VAAPI Interposition Alone Is Not Enough

Chrome already queries iHD for HEVC encode profiles. The trace shows
`VAProfileHEVCMain`, `VAProfileHEVCMain10`, and `VAEntrypointEncSlice` during
capability discovery. The failure happens later, when WebCodecs reports:

```text
VideoEncoder.isConfigSupported(...) => supported:false
```

So faking libva capability is not the fix. The missing part is Chromium's
internal HEVC encoder path and delegate selection.

## Files

| File | Purpose |
| --- | --- |
| `src/injector_scout.c` | First-stage preload library. Logs Chrome process type and selected VAAPI calls. |
| `src/trampoline_probe.c` | Runtime trampoline probe for selected Chrome 146 VEA functions. |
| `Makefile` | Builds the scout and trampoline probe `.so` files under `bin/`. |
| `run-scout-chrome.sh` | Runs official Chrome with the scout preload and HEVC test page. |
| `run-trampoline-probe-chrome.sh` | Runs official Chrome with the trampoline probe. |
| `tools/scan_chrome_hevc_hook_candidates.py` | Read-only scanner for stripped Chrome binary hook landmarks. |
| `docs/hook-plan.md` | Current hook plan and source references. |

## Build

```bash
cd <repo>/h264-h265/injector
make
```

## Run

```bash
./run-scout-chrome.sh
```

For the internal trampoline probe:

```bash
./run-trampoline-probe-chrome.sh
```

This is the current known-good H.265 encode hotpatch entry point. It defaults
to the visual Annex-B encode page and enables the required trampoline,
delegate, coded-buffer-size, and HEVC level shims.

Generated logs go under:

```text
<repo>/h264-h265/.runtime/injector-scout
<repo>/h264-h265/.runtime/trampoline-probe
```

## Scan Hook Landmarks

```bash
tools/scan_chrome_hevc_hook_candidates.py /opt/google/chrome/chrome
```

For Chrome `146.0.7680.177`, this finds the VEA initialization/error region
around `0x8c60d10..0x8c62420`. That is the current candidate area for a runtime
trampoline.

## Current Trampoline Status

Stable:

- `VaapiVideoEncodeAccelerator` constructor hook installs through zygote and
  hits in the GPU process.
- `VaapiVideoEncodeAccelerator::Initialize` hook installs and is reached after
  spoofing an HEVC supported profile.
- `VaapiVideoEncodeAccelerator::GetSupportedProfiles` postprocess is now stable
  after preserving the copied prologue's live `rax` value with an `r11`
  absolute jump trampoline.
- `VideoEncoder.isConfigSupported()` can be forced to return `supported:true`
  for HEVC Main on official Chrome 146.
- `bitrateMode` does not need to be specified in the WebCodecs config for this
  path. With `hvc1.1.6.L93`, Chrome fills in `bitrateMode:"variable"` and
  still reaches `Initialize()`.
- A one-byte mode-selection patch and an aggressive accept patch are present as
  experiments.
- The current trampoline also contains later-stage patches that add HEVC to the
  `Initialize()` codec mask, bypass Chrome's non-H264 VBR rejection for this
  experiment, and bypass the `VaapiWrapper::Create()` supported-profile table
  miss.
- With those enabled, official Chrome reaches `InitializeTask()` and calls
  `vaCreateConfig(profile=17, entrypoint=6)` for HEVC encode.
- After correcting the profile-table bypass target, `vaCreateConfig()` returns
  success for HEVC EncSlice:

```text
vaCreateConfig call profile=17 entrypoint=6 attribs=3
vaCreateConfig ret=0 profile=17 entrypoint=6 config=1072
VaapiWrapper::Create mode=1 va_profile=17 status=0
CreateForVideoCodec mode=1 profile=16 status=0
```

Current working path:

- Official Chrome reaches the delegate switch after HEVC VA wrapper creation.
  Chrome 146's native `VideoCodec::kHEVC` switch entry points to `int3/ud2`, so
  the trampoline path replaces that missing arm with the injected delegate
  bundle.
- A self-contained bundle built from the working Electron
  `h265_vaapi_video_encoder_delegate` path can be loaded by official Chrome:
  `delegate-injection/bin/libh265_delegate_bundle_probe.so`.
- The visual encode test succeeds with I420 input and Annex-B output. The
  stable baseline currently forces all frames to key frames through
  `CHROME_HEVC_SHIM_FORCE_ALL_KEYFRAMES` defaulting to true.

Remaining blocker:

- P-frame encode is not stable yet. Disabling the all-keyframe fallback with
  `CHROME_HEVC_SHIM_FORCE_ALL_KEYFRAMES=0` currently fails around
  `vaEndPicture ret=24` on the tested Intel iHD system.
