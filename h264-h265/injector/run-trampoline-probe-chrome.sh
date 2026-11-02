#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="${CHROME_H265_VAAPI_RUNTIME:-$ROOT/.runtime/trampoline-probe}"
PROFILE="$RUNTIME/chrome-profile"
LOG="$RUNTIME/trampoline.log"
SCOUT_LOG="$RUNTIME/scout.log"
CHROME_LOG="$RUNTIME/chrome.log"
HTTP_LOG="$RUNTIME/http.log"
PORT="${PORT:-18081}"
TEST_PAGE="${TEST_PAGE:-h265-encode-visual.html}"
TEST_URL="${TEST_URL:-file://$ROOT/test/$TEST_PAGE}"

mkdir -p "$RUNTIME"
rm -rf "$PROFILE"
python3 -m http.server "$PORT" --bind 127.0.0.1 \
  --directory "$ROOT/test" >"$HTTP_LOG" 2>&1 &
http_pid=$!
trap 'kill "$http_pid" 2>/dev/null || true' EXIT

# Use the public launcher so offsets and VA-API negotiation match real sessions.
set +e
CHROME_H265_VAAPI_RUNTIME="$RUNTIME" \
CHROME_H265_VAAPI_PROFILE="$PROFILE" \
CHROME_H265_NO_FIRST_RUN=1 \
CHROME_H265_EXTRA_FEATURES="PlatformHEVCEncoderSupport,HEVCRextCodecStringParsing${CHROME_H265_EXTRA_FEATURES:+,$CHROME_H265_EXTRA_FEATURES}" \
timeout -k 5s 30s "$ROOT/run-chrome-h264-h265-vaapi.sh" \
  --disable-sync \
  --autoplay-policy=no-user-gesture-required \
  --enable-logging=stderr \
  --vmodule='*vaapi*=4,*video*=3,*media*=3,*webcodecs*=4' \
  "$@" "$TEST_URL"
status=$?
set -e

echo "Chrome status: $status"
echo "Trampoline log: $LOG"
echo "VAAPI scout log: $SCOUT_LOG"
echo "Chrome log: $CHROME_LOG"
rg -n "support=|fatal:|chunk [0-9]+:|flush resolved|HEVC|hvc1|VaapiVideoEncodeAccelerator" "$CHROME_LOG" || true

if [[ "$status" == 0 || "$status" == 124 ]] &&
   rg -q "flush resolved chunks=[1-9][0-9]*" "$CHROME_LOG" &&
   rg -q 'vaCreateConfig ret=0 profile=(17|18) entrypoint=6' "$SCOUT_LOG" &&
   rg -q 'vaRenderPicture ret=0' "$SCOUT_LOG" &&
   ! rg -q "fatal:|encoder error callback|segmentation|SIGSEGV|SIGABRT" "$CHROME_LOG"; then
  echo "PASS: Chrome used VAAPI HEVC hardware encode"
  exit 0
fi

echo "FAIL: Chrome VAAPI encode test did not complete"
exit 1
