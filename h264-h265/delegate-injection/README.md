# Delegate Injection Probe

This directory is for the experimental HEVC delegate bundle.

The goal is to reuse the working Electron/Chromium
`H265VaapiVideoEncoderDelegate` path in an injected `.so`. The bundle is used
by the trampoline experiment under `../injector` to give official Chrome the
HEVC delegate path it does not ship.

## Build

The self-contained path starts from this repository and prepares the Chromium
checkout/build it needs:

```bash
cd <repo>/h264-h265
./delegate-injection/build-self-contained-bundle.sh
```

The script downloads `depot_tools`, fetches Chromium into `.runtime/chromium`,
checks out `CHROMIUM_REF` (`146.0.7680.177` by default), builds the Chromium
outputs needed by the delegate link step, and then writes:

```text
delegate-injection/bin/libh265_delegate_bundle_probe.so
```

It needs network access, disk space, and time for the Chromium checkout/build.
On a fresh Debian/Ubuntu builder, let Chromium install its platform build
dependencies during the same run:

```bash
INSTALL_CHROMIUM_BUILD_DEPS=1 ./delegate-injection/build-self-contained-bundle.sh
```

By default, the script asks Ninja for only the known object/static-library
outputs used by `build-probe-bundle.sh`. If Chromium's build graph changes and
that is not enough, force the full build fallback:

```bash
CHROMIUM_NINJA_TARGETS=chrome ./delegate-injection/build-self-contained-bundle.sh
```

If you already have a compatible Chromium checkout and `out/Release` build,
use the direct build script instead:

```bash
cd <repo>/h264-h265/delegate-injection
SRC_ROOT=/path/to/chromium/src \
OUT_DIR=/path/to/chromium/src/out/Release \
  ./build-probe-bundle.sh
```

The script expects these inputs:

- `SRC_ROOT`: Chromium source checkout.
- `OUT_DIR`: matching Chromium build output directory.
- Chromium toolchain, generated headers, sysroot, `libbase.a`, libc++ objects,
  and media/VAAPI object files under `OUT_DIR`.

Output:

```text
bin/libh265_delegate_bundle_probe.so
```

From `h264-h265/`, this same direct build is run by:

```bash
cd <repo>/h264-h265
SRC_ROOT=/path/to/chromium/src \
OUT_DIR=/path/to/chromium/src/out/Release \
  ./make-dist.sh
```

`make-dist.sh` packages the resulting `.so` into
`dist/chrome-vaapi-h264-h265-hotpatch.tar.gz`.

## Publish For Release Workflow

After rebuilding the delegate bundle, publish it to the repository's
`h265-delegate-bundle` maintenance release:

```bash
cd <repo>/h264-h265
./delegate-injection/publish-delegate-bundle.sh
```

The release workflow uses that asset automatically when `h265_delegate_bundle_url`
and `H265_DELEGATE_BUNDLE_URL` are not set. The asset is not a user-facing
runtime package; it is repackaged into `chrome-vaapi-h264-h265-hotpatch.tar.gz`.

The same publish path is available through the `Build HEVC delegate bundle`
manual GitHub Actions workflow. Use it when you want GitHub Actions to perform
the Chromium checkout/build and refresh the maintenance release.

## Loadability Check

```bash
LD_PRELOAD=bin/libh265_delegate_bundle_probe.so \
  /usr/bin/google-chrome-stable --version
```

Expected result:

```text
Google Chrome 146.0.7680.177
```

## Current Limit

The bundle is sufficient for the current all-keyframe Annex-B encode baseline.
P-frame encode is still under investigation; disabling the all-keyframe shim
currently fails in the iHD VAAPI submission path.
