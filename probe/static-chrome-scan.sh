#!/usr/bin/env bash
set -euo pipefail

CHROME="${CHROME:-/opt/google/chrome/chrome}"
if [[ ! -x "$CHROME" ]]; then
  CHROME="$(command -v google-chrome || command -v google-chrome-stable || true)"
fi
if [[ -z "${CHROME:-}" || ! -x "$CHROME" ]]; then
  echo "Chrome executable not found. Set CHROME=/path/to/chrome." >&2
  exit 1
fi

echo "Chrome: $CHROME"
"$CHROME" --version 2>/dev/null || true
echo

echo "VAAPI/media strings:"
strings -a "$CHROME" |
  rg 'H264Vaapi|h264_vaapi|H265Vaapi|h265_vaapi|VAProfileH264|VAProfileHEVC|HEVC decoding is not supported|VaapiVideoDecoder|VaapiVideoEncode|VideoDecodeAccelerator|VideoEncodeAccelerator|AcceleratedVideoDecodeLinux|VaapiIgnoreDriverChecks|HEVC|H264' |
  sort -u
