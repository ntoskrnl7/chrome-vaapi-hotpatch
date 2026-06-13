#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="${CHROME_H265_VAAPI_RUNTIME_DIR:-$ROOT/.runtime}"
CHROME="${CHROME:-/opt/google/chrome/chrome}"
ELECTRON="${ELECTRON:-}"

if [[ -z "$ELECTRON" ]] && command -v electron >/dev/null 2>&1; then
  ELECTRON="$(command -v electron)"
fi
if [[ -n "$ELECTRON" ]]; then
  export ELECTRON
fi

mkdir -p "$RUNTIME_DIR"

echo "== VAAPI driver capability =="
if command -v vainfo >/dev/null 2>&1; then
  vainfo 2>&1 | tee "$RUNTIME_DIR/vainfo.log" | rg 'HEVC|H265|VAEntrypointEnc|VAProfile' || true
else
  echo "vainfo not found"
fi

echo
echo "== Official Chrome HEVC strings =="
if [[ -x "$CHROME" ]]; then
  "$ROOT/tools/scan_hevc_strings.sh" "$CHROME" | tee "$RUNTIME_DIR/official-chrome-hevc-strings.txt"
else
  echo "missing Chrome binary: $CHROME"
fi

echo
echo "== Reference Electron HEVC strings =="
if [[ -n "$ELECTRON" && -x "$ELECTRON" ]]; then
  "$ROOT/tools/scan_hevc_strings.sh" "$ELECTRON" | tee "$RUNTIME_DIR/electron-hevc-strings.txt"
else
  echo "missing Electron binary; set ELECTRON=/path/to/electron"
fi

echo
echo "== Reference Electron WebCodecs HEVC encode probe =="
set +e
"$ROOT/test/run-electron-h265-encode-test.sh"
electron_status=$?
set -e
echo "Electron probe status: $electron_status"

echo
echo "== Official Chrome WebCodecs HEVC encode probe =="
set +e
"$ROOT/test/run-chrome-h265-encode-test.sh"
chrome_status=$?
set -e
echo "Chrome probe status: $chrome_status"

echo
echo "Logs are under: $RUNTIME_DIR"

if [[ "$electron_status" == 0 && "$chrome_status" == 0 ]]; then
  exit 0
fi

exit 1
