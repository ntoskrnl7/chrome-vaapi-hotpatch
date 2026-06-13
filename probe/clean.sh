#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rm -rf \
  "$ROOT/bin" \
  "$ROOT/logs" \
  "$ROOT"/profile*

echo "cleaned generated probe artifacts"
