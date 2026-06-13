# Chrome H.264/H.265 VAAPI Hotpatch

Runtime package for official Google Chrome VAAPI video experiments.
This is a combined H.264/H.265 package. Use it when validating HEVC/H.265
encode or decode behavior; it also carries H.264 I420/shared-memory encode path
patches because the HEVC runtime reuses Chrome's H.264 encoder plumbing.

For normal H.264-only work, use the dedicated H.264 hotpatch package.

Environment variables with `CHROME_H265_*` or `CHROME_HEVC_*` names control the
HEVC side of this combined runtime. They do not mean the package is H.265-only.

## Run

```bash
./run-chrome-h264-h265-vaapi.sh
```

This default launcher auto-detects Chrome's major version and only runs on
tested versions. Current supported majors are `146`, `147`, `148`, and `149`.

Version-pinned launchers are also provided:

```bash
./run-chrome-h264-h265-vaapi-146.sh
./run-chrome-h264-h265-vaapi-147.sh
./run-chrome-h264-h265-vaapi-148.sh
./run-chrome-h264-h265-vaapi-149.sh
```

For unsupported versions, the default launcher exits before injecting. An
experimental escape hatch exists for development only:

```bash
CHROME=/path/to/chrome ./run-chrome-h264-h265-vaapi-experimental.sh
```

Open a URL:

```bash
./run-chrome-h264-h265-vaapi.sh https://example.com
```

## NVIDIA / Custom VAAPI Driver

NVIDIA mode expects the experimental `nvenc` branch of
`ntoskrnl7/nvidia-vaapi-driver`:

```text
https://github.com/ntoskrnl7/nvidia-vaapi-driver/tree/nvenc
```

This branch is not assumed to be available from upstream distribution packages
yet. Build and install that driver first:

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

Then run the launcher in NVIDIA mode:

```bash
CHROME_H265_VAAPI_DRIVER=nvidia ./run-chrome-h264-h265-vaapi.sh
```

If the driver is not installed system-wide, point libva at the build directory:

```bash
LIBVA_DRIVERS_PATH=/path/to/nvidia-vaapi-driver/build \
LIBVA_DRIVER_NAME=nvidia \
CHROME_H265_VAAPI_DRIVER=nvidia \
  ./run-chrome-h264-h265-vaapi.sh
```

Auto mode also selects it when `LIBVA_DRIVER_NAME` contains `nvidia`, `nvdec`,
or `nvenc`, or when `vainfo` reports `NVDEC`, `NVENC`, or `NVIDIA`.

In NVIDIA mode the launcher:

- sets `LIBVA_DRIVER_NAME=nvidia` by default;
- enables `VaapiIgnoreDriverChecks`;
- passes `--ignore-gpu-blocklist`;
- enables `CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP=1`.

The injected HEVC delegate also detects the resolved VAAPI driver mode. Intel
drivers keep the Intel iHD tuning path; NVIDIA and other non-Intel drivers use
the legacy HEVC VAAPI parameter path from the Electron patch stack. This avoids
applying Intel-only packed-slice, CTU, and QP tuning to the custom NVIDIA VAAPI
driver.
For diagnosis, `CHROME_HEVC_SHIM_INTEL_TUNING=0` forces the legacy path and
`CHROME_HEVC_SHIM_INTEL_TUNING=1` forces the Intel path.

For a differently named custom driver, set `LIBVA_DRIVER_NAME` yourself. Set
`CHROME_H265_SET_LIBVA_DRIVER_NAME=0` to prevent the launcher from filling it.
For diagnostics only, `CHROME_H265_NVIDIA_VENDOR_COMPAT=1` makes the scout
return an Intel-compatible vendor string to stock Chrome paths.
NVIDIA mode keeps the existing HEVC delegate/trampoline path; per-driver VAAPI
encode behavior still needs validation on the target machine.

Use `vainfo` with the same `LIBVA_DRIVER_NAME` and `LIBVA_DRIVERS_PATH` values
to confirm that the target GPU exposes the expected HEVC/H.264 decode and
`VAEntrypointEncSlice` encode capabilities.

The launcher uses Chrome's default profile by default. To isolate this runtime,
set a dedicated profile path:

```bash
CHROME_H265_VAAPI_PROFILE=/path/to/profile ./run-chrome-h264-h265-vaapi.sh
```

Override the Chrome binary:

```bash
CHROME=/opt/google/chrome/chrome ./run-chrome-h264-h265-vaapi.sh
```

## Files

```text
run-chrome-h264-h265-vaapi.sh
run-chrome-h264-h265-vaapi-146.sh
run-chrome-h264-h265-vaapi-147.sh
run-chrome-h264-h265-vaapi-148.sh
run-chrome-h264-h265-vaapi-149.sh
run-chrome-h264-h265-vaapi-experimental.sh
injector/bin/libchrome_hevc_trampoline_probe.so
injector/bin/libchrome_hevc_delegate_scout.so
injector/tools/probe_version_offsets.py
delegate-injection/bin/libh265_delegate_bundle_probe.so
```

## Notes

- Tested with Google Chrome `146.0.7680.177`, `147.0.7727.137`, and
  `148.0.7778.96 beta`, and `149.0.7815.2 dev` on Linux x86_64.
- Includes HEVC delegate injection plus H.264 path preparation hooks used by
  the HEVC runtime. H.264-only users should use the H.264 package.
- Newer untested Chrome majors are not enabled in the default launcher.
- Primarily tested on Intel iHD VAAPI.
- The runtime launcher keeps Chrome flags minimal. The only feature flag it
  adds by default is `--enable-features=AcceleratedVideoEncoder`; the remaining
  behavior comes from the injected trampoline/delegate environment.
- The launcher auto-resolves Chrome-version-specific patch offsets at startup.
  It writes the resolved values to `.runtime/live/offsets.env`. Set
  `CHROME_H265_AUTO_OFFSETS=0` to disable this resolver.
- VAAPI driver detection is recorded in `.runtime/live/launcher.log`, and the
  scout logs the VA vendor string in `.runtime/live/scout.log`.
- The launcher keeps Chrome sandboxing enabled by default. If a specific
  machine needs the old fallback, run with `CHROME_H265_NO_SANDBOX=1`.
- Current Intel stable encode mode forces all HEVC frames to key frames.
  P-frame encode remains experimental.

Logs are written under:

```text
.runtime/live/
```
