#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHROME="${CHROME:-/opt/google/chrome/chrome}"
if [[ ! -x "$CHROME" ]]; then
  CHROME="$(command -v google-chrome || command -v google-chrome-stable || true)"
fi
if [[ -z "${CHROME:-}" || ! -x "$CHROME" ]]; then
  echo "Chrome executable not found. Set CHROME=/path/to/chrome." >&2
  exit 1
fi

LIB="$ROOT/bin/libvaapi_probe.so"
if [[ ! -f "$LIB" ]]; then
  make -C "$ROOT"
fi

mkdir -p "$ROOT/logs"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="$ROOT/logs/vaapi-probe-$STAMP.log"
PROFILE_DIR="${CHROME_VAAPI_PROBE_PROFILE:-$ROOT/profile}"
MODE="${1:-observe}"
shift || true

case "$MODE" in
  observe)
    export CHROME_VAAPI_PROBE_INJECT_PROFILES=0
    export CHROME_VAAPI_PROBE_FORCE_VLD=0
    ;;
  inject)
    export CHROME_VAAPI_PROBE_INJECT_PROFILES=1
    export CHROME_VAAPI_PROBE_FORCE_VLD=1
    ;;
  *)
    echo "Usage: $0 [observe|inject] [url-or-extra-chrome-args...]" >&2
    exit 1
    ;;
esac

export CHROME_VAAPI_PROBE_LOG="$LOG"
export CHROME_VAAPI_PROBE_LOG_ALL_PROFILES="${CHROME_VAAPI_PROBE_LOG_ALL_PROFILES:-0}"
export LD_PRELOAD="${LIB}${LD_PRELOAD:+:$LD_PRELOAD}"

URL="${1:-chrome://gpu}"
if [[ $# -gt 0 ]]; then
  shift
fi

echo "Chrome: $CHROME"
echo "Mode: $MODE"
echo "Log: $LOG"
echo "Profile: $PROFILE_DIR"

exec "$CHROME" \
  --user-data-dir="$PROFILE_DIR" \
  --enable-logging=stderr \
  --vmodule='*vaapi*=3,*video*=2,*media*=2' \
  --ignore-gpu-blocklist \
  --enable-features=VaapiVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxZeroCopyGL,VaapiIgnoreDriverChecks \
  "$URL" \
  "$@"
