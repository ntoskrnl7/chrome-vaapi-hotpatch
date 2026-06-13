#!/usr/bin/env bash
set -euo pipefail

TEST_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TEST_ROOT/.." && pwd)"
RUNTIME_DIR="${CHROME_H264_VAAPI_RUNTIME_DIR:-$ROOT/.runtime}"
PROBE="$TEST_ROOT/probe/bin/libvaapi_probe.so"
TEST_PAGE="$TEST_ROOT/test-media/h264-encode-force-hw.html"
TEST_DIR="$RUNTIME_DIR/self-test"
PROFILE="$TEST_DIR/profile"
CHROME_LOG="$TEST_DIR/chrome.log"
VAAPI_LOG="$TEST_DIR/vaapi.log"

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

if [[ ! -f "$PROBE" || "$TEST_ROOT/probe/src/vaapi_probe.c" -nt "$PROBE" ]]; then
  make -C "$TEST_ROOT/probe"
fi

set +e
env LD_PRELOAD="$PROBE" \
  CHROME_VAAPI_PROBE_LOG="$VAAPI_LOG" \
  timeout 18s "$ROOT/run-h264-vaapi-chrome.sh" \
    --user-data-dir="$PROFILE" \
    --no-first-run \
    --disable-sync \
    --autoplay-policy=no-user-gesture-required \
    --enable-logging=stderr \
    --vmodule='*vaapi*=4,*video*=3,*media*=3,*webcodecs*=3' \
    "file://$TEST_PAGE" \
    > "$CHROME_LOG" 2>&1
status=$?
set -e

if [[ "$status" != 0 && "$status" != 124 ]]; then
  echo "Chrome test command failed with status $status"
  echo "Chrome log: $CHROME_LOG"
  echo "VAAPI log: $VAAPI_LOG"
  exit "$status"
fi

required=(
  "Initializing VAVEA, input_format: PIXEL_FORMAT_I420"
  "storage_type: SharedMemory"
  "required_encoder_type: hardware"
  "flush resolved chunks=30"
)

for pattern in "${required[@]}"; do
  if ! rg -q "$pattern" "$CHROME_LOG"; then
    echo "self-test failed: missing '$pattern'"
    echo "Chrome log: $CHROME_LOG"
    echo "VAAPI log: $VAAPI_LOG"
    exit 1
  fi
done

if ! rg -q "vaCreateConfig profile=13 .*VAEntrypointEncSlice" "$VAAPI_LOG"; then
  echo "self-test failed: missing H.264 VAAPI EncSlice config"
  echo "Chrome log: $CHROME_LOG"
  echo "VAAPI log: $VAAPI_LOG"
  exit 1
fi

if ! rg -q "vaBeginPicture|vaRenderPicture|vaEndPicture" "$VAAPI_LOG"; then
  echo "self-test failed: missing VAAPI picture submission"
  echo "Chrome log: $CHROME_LOG"
  echo "VAAPI log: $VAAPI_LOG"
  exit 1
fi

echo "PASS: patched Chrome used VAAPI H.264 hardware encode"
echo "Chrome log: $CHROME_LOG"
echo "VAAPI log: $VAAPI_LOG"
