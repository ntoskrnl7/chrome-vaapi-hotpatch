# Chrome VAAPI Hotpatch

Tools for testing and patching Linux VAAPI hardware video paths in official
Google Chrome. The current focus is WebCodecs H.264 encode, H.265/HEVC encode,
and HEVC decode verification.

The scripts run copied or injected runtime paths. They do not overwrite the
system Chrome binary.

## Quick Answer

Most users should start from the repository root:

```bash
./run-h264-vaapi.sh         # dedicated H.264/AVC runtime
./run-h264-h265-vaapi.sh    # combined H.264/H.265 runtime; use for HEVC checks
```

Use `h264-only/` for H.264-only work. Use `h264-h265/` for the combined runtime:
it is not H.265-only. The combined runtime is used for HEVC, and it also carries
the H.264 hooks and tests needed by that path.

## Commands

| Goal | Command | Notes |
| --- | --- | --- |
| Run Chrome with dedicated H.264 VAAPI hotpatch | `./run-h264-vaapi.sh` | Smaller and more stable path. Use this for H.264-only work. |
| Run Chrome with combined H.264/H.265 VAAPI runtime | `./run-h264-h265-vaapi.sh` | Use this for HEVC/H.265 encode/decode checks; H.264 support is also present. |
| Test H.264 encode | `./test-h264-vaapi.sh` | Runs the H.264 WebCodecs self-test. |
| Test combined runtime | `./test-h264-h265-vaapi.sh` | Runs the HEVC visual encode check through the combined runtime. |
| Check HEVC decode behavior | `h264-h265/test/h265-decode-visual.html?sample=file:///path/to/video.mp4` | Visual decode page for a local HEVC sample. Use `probe/` for libva decode-path tracing. |

The Chrome launchers enable Chrome's accelerated video encoder feature for
encode paths:

```text
--enable-features=AcceleratedVideoEncoder
```

## Build The HEVC Delegate Bundle

The combined H.264/H.265 runtime needs:

```text
h264-h265/delegate-injection/bin/libh265_delegate_bundle_probe.so
```

This repository includes the build script for that file. It prepares the
Chromium checkout/build tree it needs, so no external Chromium checkout is
required before starting:

```bash
cd h264-h265
./delegate-injection/build-self-contained-bundle.sh
```

On a fresh Debian/Ubuntu builder, let Chromium install its platform build
dependencies during the same run:

```bash
INSTALL_CHROMIUM_BUILD_DEPS=1 ./delegate-injection/build-self-contained-bundle.sh
```

The script downloads `depot_tools`, fetches Chromium into `.runtime/chromium`,
checks out `CHROMIUM_REF` (`146.0.7680.177` by default), builds the Chromium
outputs needed by `delegate-injection/build-probe-bundle.sh`, and then runs
that script. Set `CHROMIUM_NINJA_TARGETS=chrome` to force the older full
Chromium build fallback.

Expected output:

```text
h264-h265/delegate-injection/bin/libh265_delegate_bundle_probe.so
```

After the delegate bundle exists, build the combined release package with:

```bash
./make-dist.sh
```

Set `CHROMIUM_REF`, `CHROMIUM_WORKDIR`, `SRC_ROOT`, or `OUT_DIR` to reuse an
existing Chromium checkout/build.

## NVIDIA Driver Note

NVIDIA HEVC/H.265 testing currently expects the experimental `nvenc` branch of
`ntoskrnl7/nvidia-vaapi-driver`:

```text
https://github.com/ntoskrnl7/nvidia-vaapi-driver/tree/nvenc
```

This branch is not assumed to be available from upstream distribution packages
yet. Build and install it yourself, or point `LIBVA_DRIVERS_PATH` at its build
directory, before using the combined runtime in NVIDIA mode:

```bash
LIBVA_DRIVER_NAME=nvidia \
CHROME_H265_VAAPI_DRIVER=nvidia \
  ./run-h264-h265-vaapi.sh
```

If it is not installed system-wide:

```bash
LIBVA_DRIVERS_PATH=/path/to/nvidia-vaapi-driver/build \
LIBVA_DRIVER_NAME=nvidia \
CHROME_H265_VAAPI_DRIVER=nvidia \
  ./run-h264-h265-vaapi.sh
```

Use `vainfo` with the same driver environment to confirm the advertised
decode/encode entrypoints on the target machine.

In NVIDIA mode, both launchers add `VaapiOnNvidiaGPUs`,
`VaapiIgnoreDriverChecks`, and `--ignore-gpu-blocklist`. The first feature lets
Chrome open the NVIDIA render node; it does not add encoding support to the
driver. The H.264-only launcher detects `LIBVA_DRIVER_NAME` or `vainfo`, or can
be selected explicitly with `CHROME_H264_VAAPI_DRIVER=nvidia`.

The combined runtime negotiates HEVC packed headers using the driver's reported
capabilities. It does not request packed slice headers from NVENC when only
sequence/picture headers are supported. Explicit application attributes remain
unchanged. Set `CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS=0` to disable this
compatibility behavior for diagnosis.

Use `CHROME_H264_EXTRA_FEATURES` or `CHROME_H265_EXTRA_FEATURES` to add Chrome
features without losing the launcher's defaults. The corresponding
`CHROME_H264_ENABLE_FEATURES` / `CHROME_H265_ENABLE_FEATURES` variables replace
the complete list. Passing another `--enable-features` argument can also
override it. Intel mode does not enable the NVIDIA-specific defaults.

An encode self-test passing does not establish browser hardware decode support.
On an RTX A2000 (driver 570.211.01), Chrome 148.0.7778.96 passed WebCodecs
hardware encode/decode round trips with Wayland and ANGLE OpenGL. The tested
X11/ANGLE OpenGL configuration encoded successfully but failed to initialize
the decoder frame pool. On a Wayland desktop, use:

```bash
LIBVA_DRIVER_NAME=nvidia ./run-h264-h265-vaapi.sh \
  --ozone-platform=wayland --use-gl=angle --use-angle=gl
```

The same display arguments work with `run-h264-vaapi.sh` and the encode
self-tests. The launchers do not force a display backend.

Validation covered 30-frame H.264 round trips at 640x360, 1920x1080,
1600x900, and 638x358, and HEVC at 640x360, 1920x1080, and 1520x608,
including delta frames and FFmpeg software/VA-API decoding. These checks used
the driver review build at `67fcc44`, not a distribution driver.
An additional HEVC 638x358 test produced a 640x360 stream with visible padding;
that cropping issue remains unresolved. Do not assume arbitrary HEVC sizes,
other Chrome versions, or other display backends pass from these results.

CPU-only regression checks (no Chrome or GPU required):

```bash
python3 -m unittest discover -s tests -v
make -C h264-h265/injector check
```

## Release Packages

GitHub Actions can publish ready-to-use tarballs from a tag or manual workflow
run:

- `chrome-vaapi-h264-hotpatch.tar.gz` is always built.
- `chrome-vaapi-h264-h265-hotpatch.tar.gz` is built with
  `libh265_delegate_bundle_probe.so` included.

The HEVC delegate bundle can be built from this repository with the script
above. The release workflow does not run the full Chromium checkout/build
because that is too large for the normal packaging job.

For the first release, or whenever the delegate bundle must be refreshed, host
the resulting `.so` at a URL reachable by the workflow and provide it with one
of these workflow inputs/secrets:

- `h265_delegate_bundle_url`
- `H265_DELEGATE_BUNDLE_URL`

The normal maintenance path is to publish the built `.so` to the repository's
`h265-delegate-bundle` maintenance release:

```bash
cd h264-h265
./delegate-injection/build-self-contained-bundle.sh
./delegate-injection/publish-delegate-bundle.sh
```

The same build can be run from GitHub Actions with the
`Build HEVC delegate bundle` manual workflow. That workflow uses a shallow
Chromium checkout where possible and uploads the `.so` as a workflow artifact.
Publishing to `h265-delegate-bundle` is opt-in through the workflow's
`publish` input, so inspect the artifact before replacing the maintenance
asset.

If no URL is provided, the workflow first downloads
`libh265_delegate_bundle_probe.so` from that maintenance release. If that asset
does not exist, it falls back to reusing the `.so` from an existing
`chrome-vaapi-h264-h265-hotpatch.tar.gz` release package. If it still cannot
prepare the delegate bundle, the workflow fails instead of publishing an
incomplete combined package.

For local packaging, build the delegate bundle first and then run
`h264-h265/make-dist.sh`.

## Directory Layout

| Directory | Purpose |
| --- | --- |
| `h264-only/` | H.264-only hotpatcher. Patches a copied Chrome binary to use VAAPI with I420/shared-memory input. |
| `h264-h265/` | Combined H.264/H.265 runtime. Use it for HEVC encode/decode checks and for validating the shared runtime path. |
| `probe/` | Standalone LD_PRELOAD VAAPI probe for capability and encode/decode call tracing. |
| `electron-h264-test/` | Minimal Electron WebCodecs H.264 comparison harness. |

## H.264 vs H.265

Use `h264-only/` when the target is only H.264. It has a smaller patch surface
and a direct self-test.

Use `h264-h265/` when the target is H.265/HEVC encode, HEVC decode checks, or
when validating the combined runtime. Official Chrome does not ship the full
HEVC VAAPI encoder delegate path in the tested builds, so this runtime uses
trampolines plus an injected delegate bundle for encode-side work.

`h264-h265/` means H.264 and H.265/HEVC paths are both present in that runtime.
HEVC is the main reason to use it, including HEVC decode checks, but the runtime
also includes and can test the H.264 I420/shared-memory encode path. For normal
H.264-only testing, prefer
`./run-h264-vaapi.sh`.

## Status

The H.264 path is the stable baseline. It changes Chrome's WebCodecs H.264
encode setup from GPU memory buffer/NV12 input to shared-memory/I420 CPU input.

The HEVC encode side of the combined runtime is experimental. The known-good
encode baseline is Annex-B output with I420 input and all-keyframe submission.
The same runtime can also pass the H.264 I420 WebCodecs smoke test, and the
project includes an HEVC decode visual check. H.264-only users should still
start with `./run-h264-vaapi.sh`.

## Runtime Artifacts

Generated files are ignored by git. This includes patched Chrome copies,
downloaded Chrome packages, extracted package trees, temporary browser
profiles, logs, built shared objects, and distribution archives.

Project-specific runtime files are written under each directory's `.runtime/`
folder.
