#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /path/to/chrome-or-electron" >&2
  exit 1
fi

strings -a "$1" | rg -n \
  'H265Vaapi|H265.*VideoEncoder|Unsupported hevc profile|Unsupported H265 profile|HEVC encoder support is not compiled|HEVC Main10 native|HEVC Main444|HEVC VAAPI mode|Falling back to HEVC CQP|Failed injecting expected HEVC|Packed HEVC headers|HEVC|hvc1|hev1|VAProfileHEVC|H265AnnexB|H265Parser|Initializing VAVEA|VideoEncodeAcceleratorAdapter::EncodeOnAcceleratorThread' \
  || true
