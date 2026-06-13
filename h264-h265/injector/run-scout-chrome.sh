#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INJECTOR_DIR="$ROOT/injector"
CHROME="${CHROME:-/usr/bin/google-chrome-stable}"
RUNTIME="$ROOT/.runtime/injector-scout"
PROFILE="$RUNTIME/chrome-profile"
LOG="$RUNTIME/scout.log"
CHROME_LOG="$RUNTIME/chrome.log"
HTTP_LOG="$RUNTIME/http.log"
PORT="${PORT:-18080}"

mkdir -p "$RUNTIME"
rm -rf "$PROFILE"
rm -f "$LOG" "$CHROME_LOG" "$HTTP_LOG"

make -C "$INJECTOR_DIR" >/dev/null

python3 -m http.server "$PORT" --bind 127.0.0.1 \
  --directory "$ROOT/test" >"$HTTP_LOG" 2>&1 &
http_pid=$!
trap 'kill "$http_pid" 2>/dev/null || true' EXIT

sleep 0.3

set +e
CHROME_HEVC_DELEGATE_SCOUT_LOG="$LOG" \
LD_PRELOAD="$INJECTOR_DIR/bin/libchrome_hevc_delegate_scout.so" \
timeout 15s "$CHROME" \
  --user-data-dir="$PROFILE" \
  --no-first-run \
  --no-sandbox \
  --disable-sync \
  --autoplay-policy=no-user-gesture-required \
  --enable-logging=stderr \
  --gpu-launcher="env CHROME_HEVC_DELEGATE_SCOUT_LOG=$LOG LD_PRELOAD=$INJECTOR_DIR/bin/libchrome_hevc_delegate_scout.so" \
  --vmodule='*vaapi*=4,*video*=3,*media*=3,*webcodecs*=4' \
  --ignore-gpu-blocklist \
  --enable-features=AcceleratedVideoEncoder,VaapiIgnoreDriverChecks,PlatformHEVCEncoderSupport,HEVCRextCodecStringParsing \
  --enable-blink-features=WebCodecs \
  "http://127.0.0.1:$PORT/h265-encode-force-hw.html" \
  >"$CHROME_LOG" 2>&1
status=$?
set -e

echo "Chrome status: $status"
echo "Scout log: $LOG"
echo "Chrome log: $CHROME_LOG"
echo
cat "$LOG" 2>/dev/null || true
echo
rg -n "support=|fatal:|HEVC|hvc1|VaapiVideoEncodeAccelerator" "$CHROME_LOG" || true
