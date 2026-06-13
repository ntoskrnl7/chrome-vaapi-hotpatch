# Chrome H.264/H.265 VAAPI Hotpatch Distribution

This folder contains the redistributable combined H.264/H.265 VAAPI hotpatch
package. Use it when validating HEVC/H.265 encode or decode behavior; it also
includes H.264 I420/shared-memory encode path patches because the HEVC runtime
reuses Chrome's H.264 encoder plumbing.

Use:

```bash
./chrome-vaapi-h264-h265-hotpatch/run-chrome-h264-h265-vaapi.sh
```

For NVIDIA/custom VAAPI driver testing:

```bash
CHROME_H265_VAAPI_DRIVER=nvidia \
  ./chrome-vaapi-h264-h265-hotpatch/run-chrome-h264-h265-vaapi.sh
```

NVIDIA mode expects the experimental `nvenc` branch of
`ntoskrnl7/nvidia-vaapi-driver` to be built and installed, or exposed through
`LIBVA_DRIVERS_PATH`. This branch is not assumed to be available from upstream
distribution packages yet:

```text
https://github.com/ntoskrnl7/nvidia-vaapi-driver/tree/nvenc
```

The packaged delegate gates Intel-only HEVC tuning to Intel VAAPI drivers.
NVIDIA and other non-Intel drivers use the legacy HEVC VAAPI parameter path
from the Electron patch stack.

Package archive:

```text
chrome-vaapi-h264-h265-hotpatch.tar.gz
```
