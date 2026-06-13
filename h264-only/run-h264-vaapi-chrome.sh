#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_DIR="${CHROME_H264_VAAPI_RUNTIME_DIR:-$ROOT/.runtime}"
CHROME_DIR="$RUNTIME_DIR/chrome-dir"
PATCHED_CHROME="$CHROME_DIR/chrome"
PATCHED_PAYLOAD="$RUNTIME_DIR/chrome-h264-shmem-i420-cpu"
PATCH_REPORT="$RUNTIME_DIR/chrome-h264-shmem-i420-cpu.report.json"
PATCH_SOURCE="${CHROME_SOURCE:-/opt/google/chrome/chrome}"
SOURCE_DIR="${CHROME_RESOURCES_DIR:-$(dirname "$PATCH_SOURCE")}"
FORCE_REPATCH=0
PREPARE_ONLY=0

while [[ "${1:-}" == --repatch || "${1:-}" == --prepare || "${1:-}" == --print-binary ]]; do
  case "$1" in
    --repatch)
      FORCE_REPATCH=1
      ;;
    --prepare)
      PREPARE_ONLY=1
      ;;
    --print-binary)
      echo "$PATCHED_CHROME"
      exit 0
      ;;
  esac
  shift
done

if [[ ! -x "$PATCH_SOURCE" ]]; then
  echo "missing Chrome binary: $PATCH_SOURCE" >&2
  exit 1
fi

if [[ "$FORCE_REPATCH" == 1 || ! -x "$PATCHED_CHROME" || "$PATCH_SOURCE" -nt "$PATCHED_CHROME" ]]; then
  mkdir -p "$RUNTIME_DIR"
  "$ROOT/auto_patch_chrome_h264_vaapi.py" \
    "$PATCH_SOURCE" \
    "$PATCHED_PAYLOAD" \
    --report "$PATCH_REPORT"
  rm -rf "$CHROME_DIR"
  mkdir -p "$CHROME_DIR"
  find "$SOURCE_DIR" -mindepth 1 -maxdepth 1 ! -name chrome \
    -exec ln -s {} "$CHROME_DIR/" \;
  cp -p "$PATCHED_PAYLOAD" "$PATCHED_CHROME"
fi

if [[ "$PREPARE_ONLY" == 1 ]]; then
  echo "$PATCHED_CHROME"
  exit 0
fi

exec "$PATCHED_CHROME" \
  --enable-features=AcceleratedVideoEncoder \
  "$@"
