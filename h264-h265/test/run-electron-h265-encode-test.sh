#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELECTRON="${ELECTRON:-}"
RUNTIME_DIR="${CHROME_H265_VAAPI_RUNTIME_DIR:-$ROOT/.runtime}"
OUT="$RUNTIME_DIR/electron-h265-encode.log"

if [[ -z "$ELECTRON" ]] && command -v electron >/dev/null 2>&1; then
  ELECTRON="$(command -v electron)"
fi

mkdir -p "$RUNTIME_DIR"
rm -f "$OUT"

if [[ -z "$ELECTRON" || ! -x "$ELECTRON" ]]; then
  echo "missing Electron binary; set ELECTRON=/path/to/electron" >&2
  exit 1
fi

set +e
env -u ELECTRON_RUN_AS_NODE timeout 25s "$ELECTRON" "$ROOT/test" >"$OUT" 2>&1
status=$?
set -e

cat "$OUT"

if [[ "$status" != 0 ]]; then
  echo "FAIL: Electron H.265 encode test failed with status $status"
  echo "Log: $OUT"
  exit "$status"
fi

echo "PASS: Electron H.265 encode test produced chunks"
echo "Log: $OUT"
