#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$ROOT/run-chrome-probe.sh" observe \
  "file://$ROOT/test-media/h264-encode-force-hw.html" \
  --enable-features=AcceleratedVideoEncoder,VaapiVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxZeroCopyGL,VaapiIgnoreDriverChecks \
  --enable-blink-features=WebCodecs \
  "$@"
