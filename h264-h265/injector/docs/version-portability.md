# Version Portability Notes

Goal: avoid a hard version table if possible.  The current Chrome 146 hotpatch
uses fixed offsets, but newer Chrome builds move or rewrite the target code.

## Current Matrix

```text
146.0.7680.177 stable installed   PASS
147.0.7727.137 stable current     PASS
148.0.7778.96 beta                PASS
149.0.7815.2 dev                  PASS
```

## Important Finding

The official Chrome binaries keep useful read-only strings:

```text
media::VaapiVideoEncodeAccelerator
~VaapiVideoEncodeAccelerator
Unsupported output profile
Failed initializing VAAPI for profile
Failed initializing encoder. config:
Unsupported codec:
VideoEncodeAcceleratorProvider::GetVideoEncodeAcceleratorSupportedProfiles
```

Scanning x86-64 RIP-relative `lea` references to those strings gives stable
landmarks across tested versions.  This is the best route toward a generic
resolver.

## 147 Is Mostly Relocatable

For Chrome 147, the `VaapiVideoEncodeAccelerator` cluster moves by a consistent
delta from the Chrome 146 class-string xref:

```text
146 class xref: 0x8c62314
147 class xref: 0x8cf2654
delta:          0x90340
```

With that delta, these 146 patch points still match in 147:

```text
VEA constructor
VEA initialize
VEA initialize task
HEVC initialize codec mask
HEVC initialize VBR restriction
HEVC mode-selection compare
force mode-selection accept branch
H264 delegate constructor
CreateEncodeJob
VEA GetSupportedProfiles
```

The same simple delta does not locate every dependency:

```text
VaapiWrapper CreateForVideoCodec
VaapiWrapper Create
ReadOnlyRegionPool::MaybeAllocateBuffer
SetUpVeaConfig storage/input patches
PrepareGpuFrame
PrepareCpuFrame
RequireBitstreamBuffers
```

`VEA GetSupportedProfiles` and `VaapiWrapper CreateForVideoCodec` have unique
byte signatures in the passing 146/147 builds, so those are resolved by pattern
search instead of by delta.

The WebCodecs adapter/input-frame block is now resolved from the
`PrepareCpuFrame` NV12 fast-path compare. That landmark finds:

```text
SetUpVeaConfig storage/input patches
PrepareGpuFrame
PrepareCpuFrame
RequireBitstreamBuffers
ReadOnlyRegionPool::MaybeAllocateBuffer
```

This is enough for the current Chrome 147 stable package to pass without a
manual offset table.

## 148/149 Are Resolved By Semantic Probes

In Chrome 148 and 149, the `media::VaapiVideoEncodeAccelerator` string xref
still exists, but nearby function layout is different enough that the Chrome
146 prologue signatures do not match after a simple delta.

The resolver now recovers more of those builds than the first attempt:

```text
148/149 VEA constructor       resolved by nearby prologue scan
148/149 VEA initialize        resolved by nearby prologue scan
148/149 InitializeTask        resolved from the delegate switch marker
148/149 H264 delegate ctor    resolved from the delegate ctor byte pattern
148/149 CreateEncodeJob       resolved from the delegate-error string xref
148/149 adapter/input hooks   resolved from the PrepareCpuFrame landmark
```

With those offsets plus the newer CFI/runtime-call probes, 148 beta and 149 dev
reach `CreateEncodeJob` and complete VAAPI encode work.

The 149-specific blocker was another vtable CFI guard inside
`PrepareEncodeJob`. The resolver now finds the extra
`cmp/ror/ja cfi-trap` sequence before the main `PrepareEncodeJob` CFI guard and
exports `CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK_EXTRA` when it
exists. The trampoline patches that branch only when the target bytes are an
actual `0f 87` CFI branch.

That means a fully generic resolver needs to find semantic sites:

1. Locate the VEA cluster from string xrefs.
2. Locate function starts around those xrefs by scanning backward to valid
   prologues or known call structure.
3. Locate the delegate switch table from the `lea table(%rip), %rcx;
   movslq (%rcx,%rax,4), %rax; add %rcx,%rax; jmp *%rax` sequence inside
   `InitializeTask`.
4. Patch the HEVC switch entry by copying the H264 switch entry, instead of
   relying on hardcoded 4-byte relative table values.
5. Resolve unique hooks like `VEA GetSupportedProfiles` and
   `VaapiWrapper CreateForVideoCodec` by byte pattern.
6. Resolve adapter/input-frame hooks from the WebCodecs adapter landmark, not
   from the VEA landmark.

## Tooling

Run the read-only probe:

```bash
cd <repo>/h264-h265
./injector/tools/probe_version_offsets.py /opt/google/chrome/chrome
./injector/tools/probe_version_offsets.py .runtime/version-matrix/extracted/stable/opt/google/chrome/chrome
```

Run the channel matrix:

```bash
./test/run-chrome-version-matrix.sh installed stable beta unstable
```

The runtime launcher and matrix both use `probe_version_offsets.py --env`, so
resolved `CHROME_HEVC_OFF_*` values are visible to both the browser process and
the injected GPU process launcher. Test runs also use a per-channel
`--user-data-dir` plus `--no-first-run`; those are test-only settings and are
recorded in each run's `launcher.log`.

The probe intentionally does not patch Chrome.  It reports which known 146
patch sites can be relocated and which require a smarter resolver.
