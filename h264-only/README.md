# Chrome H.264 VAAPI Encoder Hotpatch

This tool patches a local copy of official Google Chrome so the Linux WebCodecs
H.264 encode path uses VAAPI with shared-memory I420 input.

It does not overwrite `/opt/google/chrome/chrome`. The launcher copies Chrome
into `.runtime/`, applies the patch to that copy, and runs the patched binary
with the required feature flag.

## Status

Verified channels:

| Channel | Version | Result |
| --- | --- | --- |
| Stable | 146.0.7680.177 | PASS |
| Beta | 148.0.7778.96 | PASS |
| Dev | 149.0.7815.2 | PASS |

The tested path requires:

```text
--enable-features=AcceleratedVideoEncoder
```

## Usage

From the repository root:

```bash
./run-h264-vaapi.sh
```

From this directory:

Run patched Chrome:

```bash
./run-h264-vaapi-chrome.sh
```

Force a rebuild after Chrome updates:

```bash
./run-h264-vaapi-chrome.sh --repatch
```

Prepare the patched copy without launching Chrome:

```bash
./run-h264-vaapi-chrome.sh --repatch --prepare
```

Run the self-test:

```bash
./test/test-h264-vaapi-chrome.sh
```

Expected result:

```text
PASS: patched Chrome used VAAPI H.264 hardware encode
```

## Files

| File | Purpose |
| --- | --- |
| `auto_patch_chrome_h264_vaapi.py` | Pattern-based Chrome ELF hotpatcher. |
| `run-h264-vaapi-chrome.sh` | Launcher for the patched Chrome copy. |
| `make-dist.sh` | Builds a redistributable package under `dist/`. |
| `clean.sh` | Removes generated runtime artifacts. |
| `test/test-h264-vaapi-chrome.sh` | WebCodecs and VAAPI submission self-test. |
| `test/probe/` | Test-only LD_PRELOAD VAAPI logger source. |
| `test/test-media/` | WebCodecs H.264 hardware encode page. |
| `tools/analyze_chrome.py` | Read-only helper for inspecting Chrome strings and code references. |
| `tools/patch_chrome_h264_shmem.py` | Fixed-address prototype retained for historical comparison. |

## What The Patch Changes

The patch targets `VideoEncodeAcceleratorAdapter` and changes the H.264 encode
setup away from Chrome's GPU-memory-buffer input path:

1. `input_buffer_preference`: `GpuMemBuf` -> `CpuMemBuf`
2. `VideoEncodeAccelerator::Config::input_format`: `NV12` -> `I420`
3. `VideoEncodeAccelerator::Config::storage_type`: `GpuMemoryBuffer` -> `Shmem`
4. `PrepareCpuFrame` allocation pixel format: `NV12` -> `I420`
5. `PrepareCpuFrame` second allocation/check pixel format: `NV12` -> `I420`

The current patcher does not hardcode the addresses. It finds the encode and
CPU-frame-preparation paths from trace strings and nearby opcode patterns, then
writes a JSON report next to the patched binary.

Example report from Chrome stable `146.0.7680.177`:

```text
input_buffer_preference GpuMemBuf -> CpuMemBuf    vaddr 0x77ccda2
VEA Config input_format NV12 -> I420              vaddr 0x77cd3a3
VEA Config storage_type GpuMemoryBuffer -> Shmem  vaddr 0x77cd3e9
PrepareCpuFrame allocation format #1              vaddr 0x77ce40f
PrepareCpuFrame allocation format #2              vaddr 0x77ce523
```

## Testing Another Chrome Binary

The launcher defaults to `/opt/google/chrome/chrome`. To patch and run another
extracted Chrome binary:

```bash
CHROME_SOURCE=/path/to/chrome \
CHROME_RESOURCES_DIR=/path/to/chrome/resource/dir \
  ./run-h264-vaapi-chrome.sh --repatch
```

For Debian packages:

```bash
mkdir -p extracted/new
dpkg-deb -x google-chrome-*.deb extracted/new
```

Then set `CHROME_SOURCE` to the extracted package's `chrome` ELF and
`CHROME_RESOURCES_DIR` to the directory containing Chrome's `.pak`, `.so`,
locale, and metadata files.

## Repository Hygiene

Generated files are ignored:

- `.runtime/`
- `dist/`
- `downloads/`
- `extracted/`
- `test/probe/bin/`
- Chrome profile, log, and patch report files

Clean generated files with:

```bash
./clean.sh
```
