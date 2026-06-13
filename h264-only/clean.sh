#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rm -rf \
  "$ROOT/.runtime" \
  "$ROOT/dist" \
  "$ROOT/chrome-dir" \
  "$ROOT/chrome-beta-dir" \
  "$ROOT/chrome-dev-dir" \
  "$ROOT"/profile-* \
  "$ROOT"/chrome-auto-test \
  "$ROOT"/chrome-beta-h264-vaapi \
  "$ROOT"/chrome-dev-h264-vaapi \
  "$ROOT"/chrome-h264-shmem \
  "$ROOT"/chrome-h264-shmem-i420 \
  "$ROOT"/chrome-h264-shmem-i420-cpu \
  "$ROOT/test/probe/bin" \
  "$ROOT"/*.log \
  "$ROOT"/*.report.json

echo "cleaned generated artifacts"
