#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$ROOT/h264-h265/run-chrome-h264-h265-vaapi.sh" "$@"
