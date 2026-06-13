#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHROME="${CHROME:-/opt/google/chrome/chrome}"
RUNTIME_DIR="${CHROME_H265_VAAPI_RUNTIME_DIR:-$ROOT/.runtime}"
PROFILE="$RUNTIME_DIR/chrome-profile"
OUT="$RUNTIME_DIR/chrome-h265-encode.log"
TEST_PAGE="$ROOT/test/h265-encode-force-hw.html"

mkdir -p "$RUNTIME_DIR"
rm -rf "$PROFILE" "$OUT"

if [[ ! -x "$CHROME" ]]; then
  echo "missing Chrome binary: $CHROME" >&2
  exit 1
fi

set +e
timeout 20s "$CHROME" \
  --user-data-dir="$PROFILE" \
  --no-first-run \
  --disable-sync \
  --autoplay-policy=no-user-gesture-required \
  --enable-logging=stderr \
  --vmodule='*vaapi*=4,*video*=3,*media*=3,*webcodecs*=3' \
  --ignore-gpu-blocklist \
  --enable-features=AcceleratedVideoEncoder,VaapiIgnoreDriverChecks \
  --enable-blink-features=WebCodecs \
  "file://$TEST_PAGE" \
  >"$OUT" 2>&1
status=$?
set -e

cat "$OUT"

if rg -q "flush resolved chunks=[1-9]" "$OUT" && ! rg -q "fatal:|encoder error|flush rejected" "$OUT"; then
  echo "PASS: Chrome H.265 encode test produced chunks"
  echo "Log: $OUT"
  exit 0
fi

echo "FAIL: Chrome H.265 encode test did not produce a successful HEVC stream"
echo "Chrome command status: $status"
echo "Log: $OUT"
exit 1

