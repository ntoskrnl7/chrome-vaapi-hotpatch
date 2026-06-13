#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_ROOT="$ROOT/dist"
PKG_NAME="chrome-vaapi-h264-hotpatch"
PKG="$DIST_ROOT/$PKG_NAME"

rm -rf "$PKG"
mkdir -p "$PKG"

install -m 0644 "$ROOT/DIST_README.md" "$DIST_ROOT/README.md"
install -m 0644 "$ROOT/DIST_PACKAGE_README.md" "$PKG/README.md"
install -m 0755 "$ROOT/auto_patch_chrome_h264_vaapi.py" "$PKG/auto_patch_chrome_h264_vaapi.py"
install -m 0755 "$ROOT/run-h264-vaapi-chrome.sh" "$PKG/run-chrome-h264-vaapi.sh"

tar -C "$DIST_ROOT" -czf "$DIST_ROOT/$PKG_NAME.tar.gz" \
  "$PKG_NAME"

echo "$PKG"
echo "$DIST_ROOT/$PKG_NAME.tar.gz"
