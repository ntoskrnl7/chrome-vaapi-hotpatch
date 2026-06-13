#!/usr/bin/env bash
set -u

log_file="$1"
shift

{
  echo "gpu-gdb-launcher argv: $*"
  echo "LD_PRELOAD=${LD_PRELOAD-}"
  gdb -q -batch \
    -ex 'set pagination off' \
    -ex run \
    -ex 'thread apply all bt' \
    --args "$@"
} >"$log_file" 2>&1
