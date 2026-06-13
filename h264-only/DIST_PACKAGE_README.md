# Chrome H.264 VAAPI Encoder Hotpatch

Small Linux x86_64 hotpatch package for Google Chrome H.264 WebCodecs VAAPI
hardware encoding on Intel iHD systems.

It does not overwrite `/opt/google/chrome/chrome`. It copies the Chrome binary
into this package directory, applies the hotpatch to that copy, and launches the
patched copy with the required feature flags.

## Requirements

- Linux x86_64
- Google Chrome installed at `/opt/google/chrome/chrome`, or set
  `CHROME_SOURCE`
- Intel VAAPI/iHD driver with H.264 encode support
- `python3`, `readelf`, `find`, `cp`, `rg`

## Run Patched Chrome

```bash
./run-chrome-h264-vaapi.sh
```

The launcher automatically adds:

```text
--enable-features=AcceleratedVideoEncoder
```

## Repatch After Chrome Updates

```bash
./run-chrome-h264-vaapi.sh --repatch --prepare
```

This rebuilds the local patched copy and prints the patched binary path.

## Use A Different Chrome Binary

```bash
CHROME_SOURCE=/path/to/chrome \
CHROME_RESOURCES_DIR=/path/to/chrome/resource/dir \
  ./run-chrome-h264-vaapi.sh --repatch
```

`CHROME_RESOURCES_DIR` must be the directory containing Chrome's `.pak`, `.so`,
locale, and metadata files.

## Files

| File | Purpose |
| --- | --- |
| `run-chrome-h264-vaapi.sh` | Main launcher. |
| `auto_patch_chrome_h264_vaapi.py` | Pattern-based ELF hotpatcher. |

Generated files live under `.runtime/` and are local runtime artifacts.

## Notes

- This is a binary hotpatch experiment, not an upstream Chromium patch.
- If Chrome changes the target code shape, the patcher is designed to fail
  loudly instead of patching an unknown location.
- The package targets H.264 WebCodecs hardware encode. It is not a general HEVC
  enablement package.
