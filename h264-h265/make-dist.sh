#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_ROOT="$ROOT/dist"
PKG_NAME="chrome-vaapi-h264-h265-hotpatch"
PKG="$DIST_ROOT/$PKG_NAME"

make -C "$ROOT/injector" >/dev/null
if [[ ! -x "$ROOT/delegate-injection/bin/libh265_delegate_bundle_probe.so" ]]; then
  "$ROOT/delegate-injection/build-probe-bundle.sh" >/dev/null
fi

rm -rf "$PKG"
mkdir -p "$PKG/injector/bin" "$PKG/injector/tools" "$PKG/delegate-injection/bin"

install -m 0644 "$ROOT/DIST_README.md" "$DIST_ROOT/README.md"
install -m 0644 "$ROOT/DIST_PACKAGE_README.md" "$PKG/README.md"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi.sh" \
  "$PKG/run-chrome-h264-h265-vaapi.sh"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi-146.sh" \
  "$PKG/run-chrome-h264-h265-vaapi-146.sh"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi-147.sh" \
  "$PKG/run-chrome-h264-h265-vaapi-147.sh"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi-148.sh" \
  "$PKG/run-chrome-h264-h265-vaapi-148.sh"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi-149.sh" \
  "$PKG/run-chrome-h264-h265-vaapi-149.sh"
install -m 0755 "$ROOT/dist-run-chrome-h264-h265-vaapi-experimental.sh" \
  "$PKG/run-chrome-h264-h265-vaapi-experimental.sh"
install -m 0755 "$ROOT/injector/bin/libchrome_hevc_trampoline_probe.so" \
  "$PKG/injector/bin/libchrome_hevc_trampoline_probe.so"
install -m 0755 "$ROOT/injector/bin/libchrome_hevc_delegate_scout.so" \
  "$PKG/injector/bin/libchrome_hevc_delegate_scout.so"
install -m 0755 "$ROOT/injector/tools/probe_version_offsets.py" \
  "$PKG/injector/tools/probe_version_offsets.py"
install -m 0755 "$ROOT/delegate-injection/bin/libh265_delegate_bundle_probe.so" \
  "$PKG/delegate-injection/bin/libh265_delegate_bundle_probe.so"

tar -C "$DIST_ROOT" -czf "$DIST_ROOT/$PKG_NAME.tar.gz" \
  "$PKG_NAME"

echo "$PKG"
echo "$DIST_ROOT/$PKG_NAME.tar.gz"
