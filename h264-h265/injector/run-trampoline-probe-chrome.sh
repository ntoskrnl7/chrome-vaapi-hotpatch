#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INJECTOR_DIR="$ROOT/injector"
CHROME="${CHROME:-/opt/google/chrome/chrome}"
RUNTIME="$ROOT/.runtime/trampoline-probe"
PROFILE="$RUNTIME/chrome-profile"
LOG="$RUNTIME/trampoline.log"
SCOUT_LOG="$RUNTIME/scout.log"
CHROME_LOG="$RUNTIME/chrome.log"
HTTP_LOG="$RUNTIME/http.log"
PORT="${PORT:-18081}"
SO="$INJECTOR_DIR/bin/libchrome_hevc_trampoline_probe.so"
SCOUT_SO="$INJECTOR_DIR/bin/libchrome_hevc_delegate_scout.so"
DELEGATE_SO="$ROOT/delegate-injection/bin/libh265_delegate_bundle_probe.so"
TEST_PAGE="${TEST_PAGE:-h265-encode-visual.html}"
TEST_URL="${TEST_URL:-file://$ROOT/test/$TEST_PAGE}"

detect_vaapi_driver_mode() {
  local requested="${CHROME_H265_VAAPI_DRIVER:-auto}"
  case "${requested,,}" in
    nvidia|nv|nvenc)
      echo "nvidia"
      return
      ;;
    intel|ihd|i965)
      echo "intel"
      return
      ;;
  esac
  case "${LIBVA_DRIVER_NAME:-}" in
    *nvidia*|*NVIDIA*|*nvdec*|*NVDEC*|*nvenc*|*NVENC*)
      echo "nvidia"
      return
      ;;
  esac
  echo "auto"
}

mkdir -p "$RUNTIME"
rm -rf "$PROFILE"
rm -f "$LOG" "$SCOUT_LOG" "$CHROME_LOG" "$HTTP_LOG"

make -C "$INJECTOR_DIR" >/dev/null
if [[ ! -x "$DELEGATE_SO" ]]; then
  "$ROOT/delegate-injection/build-probe-bundle.sh" >/dev/null
fi

VAAPI_DRIVER_MODE="$(detect_vaapi_driver_mode)"
if [[ "$VAAPI_DRIVER_MODE" == "nvidia" ]]; then
  if [[ -z "${LIBVA_DRIVER_NAME:-}" &&
        "${CHROME_H265_SET_LIBVA_DRIVER_NAME:-1}" == "1" ]]; then
    export LIBVA_DRIVER_NAME=nvidia
  fi
  export CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP="${CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP:-1}"
fi

PRELOAD="${CHROME_HEVC_PRELOAD:-$SO:$DELEGATE_SO:$SCOUT_SO}"
COMMON_ENV=(
  "CHROME_HEVC_DELEGATE_SCOUT_LOG=$SCOUT_LOG"
  "CHROME_HEVC_TRAMPOLINE_PROBE_LOG=$LOG"
  "CHROME_HEVC_TRAMPOLINE_PROBE=1"
  "CHROME_HEVC_TRAMPOLINE_SPOOF_PROFILE=${CHROME_HEVC_TRAMPOLINE_SPOOF_PROFILE:-1}"
  "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT=${CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_READONLY_POOL=${CHROME_HEVC_TRAMPOLINE_HOOK_READONLY_POOL:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_PREPARE_CPU=${CHROME_HEVC_TRAMPOLINE_HOOK_PREPARE_CPU:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_REQUIRE_BITSTREAM=${CHROME_HEVC_TRAMPOLINE_HOOK_REQUIRE_BITSTREAM:-1}"
  "CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS=${CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK=${CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK:-1}"
  "CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION=${CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH=${CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH:-1}"
  "CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT=${CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264=${CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264:-1}"
  "CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265=${CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE_ENCODE_JOB=${CHROME_HEVC_TRAMPOLINE_HOOK_CREATE_ENCODE_JOB:-1}"
  "CHROME_HEVC_TRAMPOLINE_CREATE_JOB_AS_H264=${CHROME_HEVC_TRAMPOLINE_CREATE_JOB_AS_H264:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES=${CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE=${CHROME_HEVC_TRAMPOLINE_HOOK_CREATE:-1}"
  "CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS=${CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS:-1}"
  "CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP=${CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP:-0}"
  "CHROME_H265_RESOLVED_VAAPI_DRIVER=$VAAPI_DRIVER_MODE"
  "CHROME_HEVC_SHIM_LEVEL=${CHROME_HEVC_SHIM_LEVEL:-180}"
  "CHROME_HEVC_SCOUT_FORCE_CODED_BUFFER_SIZE=${CHROME_HEVC_SCOUT_FORCE_CODED_BUFFER_SIZE:-524288}"
  "LD_PRELOAD=$PRELOAD"
)
if [[ -n "${LIBVA_DRIVER_NAME:-}" ]]; then
  COMMON_ENV+=("LIBVA_DRIVER_NAME=$LIBVA_DRIVER_NAME")
fi
GPU_LAUNCHER="env ${COMMON_ENV[*]}"

python3 -m http.server "$PORT" --bind 127.0.0.1 \
  --directory "$ROOT/test" >"$HTTP_LOG" 2>&1 &
http_pid=$!
trap 'kill "$http_pid" 2>/dev/null || true' EXIT

sleep 0.3

set +e
env "${COMMON_ENV[@]}" \
timeout 15s "$CHROME" \
  --user-data-dir="$PROFILE" \
  --no-first-run \
  --no-sandbox \
  --disable-sync \
  --autoplay-policy=no-user-gesture-required \
  --enable-logging=stderr \
  --gpu-launcher="$GPU_LAUNCHER" \
  --vmodule='*vaapi*=4,*video*=3,*media*=3,*webcodecs*=4' \
  --ignore-gpu-blocklist \
  --enable-features=AcceleratedVideoEncoder,VaapiIgnoreDriverChecks,PlatformHEVCEncoderSupport,HEVCRextCodecStringParsing \
  --enable-blink-features=WebCodecs \
  "$TEST_URL" \
  >"$CHROME_LOG" 2>&1
status=$?
set -e

echo "Chrome status: $status"
echo "Trampoline log: $LOG"
echo "VAAPI scout log: $SCOUT_LOG"
echo "Chrome log: $CHROME_LOG"
echo
cat "$LOG" 2>/dev/null || true
echo
rg -n "support=|fatal:|chunk [0-9]+:|flush resolved|HEVC|hvc1|VaapiVideoEncodeAccelerator|Trace/breakpoint|segmentation|SIG" "$CHROME_LOG" || true

if rg -q "flush resolved chunks=[1-9]" "$CHROME_LOG" &&
   ! rg -q "fatal:|encoder error callback" "$CHROME_LOG"; then
  echo
  echo "PASS: Chrome VAAPI encode test produced chunks"
  exit 0
fi

echo
echo "FAIL: Chrome VAAPI encode test did not complete"
exit 1
