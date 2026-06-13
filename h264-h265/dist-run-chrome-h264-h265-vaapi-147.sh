#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export CHROME_H265_EXPECT_MAJOR=147
exec "$ROOT/run-chrome-h264-h265-vaapi.sh" "$@"
