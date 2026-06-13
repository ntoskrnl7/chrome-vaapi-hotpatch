# Chrome H.264/H.265 VAAPI Hotpatch

This directory contains the combined H.264/H.265 VAAPI hotpatch path for
official Google Chrome on Linux. Use it when you need the HEVC/H.265 path,
including encode work and decode checks, but do not read it as H.265-only:
the runtime also includes H.264 hooks and tests.

Short version:

- This directory is not H.265-only.
- Use it when you need H.265/HEVC encode work or decode checks.
- It also contains a validated H.264 I420/shared-memory encode path.
- For normal H.264-only work, use `../h264-only/` or `../run-h264-vaapi.sh`.

Some files and environment variables mention H.264. That is intentional:
official Chrome's working encode plumbing is H.264-centered, so the HEVC path
borrows, redirects, or validates parts of the H.264 delegate and frame
preparation flow.

Environment variables with `CHROME_H265_*` or `CHROME_HEVC_*` names control the
HEVC side of this combined runtime. They do not mean this directory excludes
H.264.

HEVC encode is not a small configuration patch like the H.264 path. Official
Chrome can reach parts of the VAAPI encode stack, but the full HEVC encoder
delegate path is not shipped in the tested builds. This project therefore uses
runtime trampolines plus an injected delegate bundle.

## Scope

This directory covers two related paths:

- H.265/HEVC: the primary target. The trampoline adds Chrome-side support,
  redirects the missing delegate arm, and loads an injected HEVC VAAPI delegate.
- H.265/HEVC decode: included as a visual verification page for the Chrome
  HEVC decode path.
- H.264/AVC: included as support code and a validated secondary path. The
  trampoline can force the WebCodecs H.264 encode path to I420/shared-memory
  input and keep hardware VAAPI encode active.

For H.264-only use cases, use `../h264-only/` or the repository-root
`../run-h264-vaapi.sh` wrapper. Use this directory for HEVC encode/decode work
or for validating the combined H.264/H.265 runtime.

Naming note: the directory is named `h264-h265/` because it contains both code
paths. Files with `h265` or `hevc` in their names are HEVC-specific launchers,
tests, or delegate components inside that combined runtime.

## Status

Verified baseline:

- Google Chrome `146.0.7680.177` can produce HEVC VAAPI WebCodecs output with
  the injected delegate/trampoline path.
- The H.264 I420 test path can produce WebCodecs `avc1.420028` output through
  the same trampoline runtime.
- The known-good WebCodecs config is intentionally small:
  `codec: "hvc1.1.6.L93.B0"`, I420 input, and `hevc: {format: "annexb"}`.
- The Intel iHD stable path forces all submitted frames to key frames by
  default through `CHROME_HEVC_SHIM_FORCE_ALL_KEYFRAMES=1`. This avoids the
  current Intel iHD P-frame failure around `vaEndPicture ret=24`.
- Stock Chrome HEVC decode can be verified separately and does not require the
  encode delegate injection path.
- The launcher supports Intel and NVIDIA/custom VAAPI driver modes. NVIDIA mode
  enables the standard driver-check/blocklist relaxations and can retry HEVC CQP
  creation when CBR wrapper creation fails.

Tested Chrome major versions for the packaged launcher:

```text
146 147 148 149
```

Use the experimental wrapper only while validating a new Chrome version.

## Usage

From the repository root:

```bash
./run-h264-h265-vaapi.sh
```

From this directory:

```bash
./run-chrome-h264-h265-vaapi.sh
```

Force NVIDIA/custom VAAPI mode:

```bash
CHROME_H265_VAAPI_DRIVER=nvidia ./run-chrome-h264-h265-vaapi.sh
```

## NVIDIA VAAPI Driver

NVIDIA mode is tested against the experimental `nvenc` branch of
`ntoskrnl7/nvidia-vaapi-driver`:

```text
https://github.com/ntoskrnl7/nvidia-vaapi-driver/tree/nvenc
```

This branch is not assumed to be available from upstream distribution packages
yet. Build that driver first:

```bash
git clone -b nvenc https://github.com/ntoskrnl7/nvidia-vaapi-driver.git
cd nvidia-vaapi-driver
meson setup build
meson install -C build
```

The driver build requires Meson, GStreamer codec parser development files, and
NVENC headers. See that branch's README for distribution-specific package
names.

Use `sudo meson install -C build` if the install prefix requires root.

If the driver is not installed system-wide, point libva at the build directory:

```bash
LIBVA_DRIVERS_PATH=/path/to/nvidia-vaapi-driver/build \
LIBVA_DRIVER_NAME=nvidia \
CHROME_H265_VAAPI_DRIVER=nvidia \
  ./run-chrome-h264-h265-vaapi.sh
```

Use `vainfo` with the same `LIBVA_DRIVER_NAME` and `LIBVA_DRIVERS_PATH` values
to confirm that the target GPU exposes the expected HEVC/H.264 decode and
`VAEntrypointEncSlice` encode capabilities.

Open a URL directly:

```bash
./run-chrome-h264-h265-vaapi.sh https://example.com
```

Use an isolated Chrome profile:

```bash
CHROME_H265_VAAPI_PROFILE=/path/to/profile ./run-chrome-h264-h265-vaapi.sh
```

Use a different Chrome binary:

```bash
CHROME=/path/to/chrome ./run-chrome-h264-h265-vaapi.sh
```

Run the current HEVC visual encode check:

```bash
./injector/run-trampoline-probe-chrome.sh
```

Expected terminal result:

```text
PASS: Chrome VAAPI encode test produced chunks
```

Run the H.264 I420 encode check through the same trampoline runtime:

```bash
TEST_PAGE=h264-encode-i420.html ./injector/run-trampoline-probe-chrome.sh
```

## Version Matrix

Run the matrix against the installed browser only:

```bash
./test/run-chrome-version-matrix.sh installed
```

Run all supported channels:

```bash
./test/run-chrome-version-matrix.sh installed stable beta unstable
```

The matrix downloads official Chrome `.deb` packages into `.runtime/`, extracts
them locally with `dpkg-deb -x`, and runs each extracted binary through the
launcher. It does not install or replace system Chrome.

Results are written to:

```text
.runtime/version-matrix/summary.tsv
```

## Build Notes

The HEVC launcher uses two injected libraries:

| Component | Source |
| --- | --- |
| `injector/bin/libchrome_hevc_trampoline_probe.so` | Built from `injector/src/` with `make -C injector`. |
| `delegate-injection/bin/libh265_delegate_bundle_probe.so` | Built from the delegate bundle under `delegate-injection/` using a Chromium build tree. |

To build the delegate bundle from this repository checkout:

```bash
./delegate-injection/build-self-contained-bundle.sh
```

On a fresh Debian/Ubuntu builder, let Chromium install its platform build
dependencies during the same run:

```bash
INSTALL_CHROMIUM_BUILD_DEPS=1 ./delegate-injection/build-self-contained-bundle.sh
```

That script downloads `depot_tools`, fetches Chromium into `.runtime/chromium`,
checks out `CHROMIUM_REF` (`146.0.7680.177` by default), builds the Chromium
outputs needed by the delegate link step, and then writes:

```text
delegate-injection/bin/libh265_delegate_bundle_probe.so
```

The delegate bundle is not a small local-only compile. It links against
Chromium media objects, generated headers, `libbase.a`, libc++ objects, and the
Chromium sysroot from a compatible Chromium build output. The self-contained
script above prepares that Chromium checkout/build automatically. By default it
asks Ninja for only the known object/static-library outputs used by
`build-probe-bundle.sh`; set `CHROMIUM_NINJA_TARGETS=chrome` for the full
Chromium build fallback. If you already have a matching Chromium checkout/build,
run the direct build instead:

```bash
SRC_ROOT=/path/to/chromium/src \
OUT_DIR=/path/to/chromium/src/out/Release \
  ./delegate-injection/build-probe-bundle.sh
```

Expected output:

```text
delegate-injection/bin/libh265_delegate_bundle_probe.so
```

After the delegate bundle exists, `make-dist.sh` builds the injector libraries
and writes the release package under `dist/`:

```bash
./make-dist.sh
```

For GitHub Releases, the repository workflow can package this runtime when a
prebuilt `delegate-injection/bin/libh265_delegate_bundle_probe.so` is supplied
through the workflow input `h265_delegate_bundle_url` or the repository secrets
`H265_DELEGATE_BUNDLE_URL`.

The `Build HEVC delegate bundle` GitHub Actions workflow can also build and
publish the maintenance bundle used by release packaging. It is manual because
it has to fetch Chromium and compile Chromium build outputs.

## Useful Checks

Scan HEVC strings in a Chrome or Electron binary:

```bash
./tools/scan_hevc_strings.sh /opt/google/chrome/chrome
./tools/scan_hevc_strings.sh /path/to/electron
```

Run the stock Chrome HEVC encode probe:

```bash
./test/run-chrome-h265-encode-test.sh
```

Run the standalone Electron HEVC encode probe:

```bash
ELECTRON=/path/to/electron ./test/run-electron-h265-encode-test.sh
```

Open the visual encode probe page manually:

```text
test/h265-encode-visual.html
```

The decode visual page expects a local HEVC sample. Pass the file path with the
`sample` query parameter:

```text
test/h265-decode-visual.html?sample=file:///path/to/bear-1280x720-hevc.mp4
```

## Runtime Files

Generated files are written under `.runtime/` and ignored by git. The runtime
directory contains launch logs, auto-resolved offsets, extracted Chrome package
trees, and temporary browser profiles.

## Current Limitations

- P-frame HEVC encode is still experimental on the tested Intel iHD path.
- H.264 support in this directory exists to support and validate the HEVC
  runtime. The dedicated H.264 hotpatcher remains the cleaner path when HEVC is
  not needed.
- New Chrome major versions should be validated with the experimental wrapper
  before adding them to the supported list.
- The delegate bundle build is tied to Chromium media ABI details and should be
  rebuilt against the matching Chromium build tree.

For trampoline implementation details, see:

```text
injector/README.md
injector/docs/version-portability.md
```
