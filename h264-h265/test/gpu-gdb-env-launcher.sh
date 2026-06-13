#!/usr/bin/env bash
set -u

log_file="${CHROME_HEVC_GPU_GDB_LOG:-/tmp/chrome-hevc-gpu-gdb.log}"

{
  echo "gpu-gdb-env-launcher argv: $*"
  echo "LD_PRELOAD=${LD_PRELOAD-}"
  echo "CHROME_HEVC_TRAMPOLINE_PROBE_LOG=${CHROME_HEVC_TRAMPOLINE_PROBE_LOG-}"
  echo "CHROME_HEVC_DELEGATE_SCOUT_LOG=${CHROME_HEVC_DELEGATE_SCOUT_LOG-}"
  gdb -q -batch \
    -ex 'set pagination off' \
    -ex 'set debuginfod enabled off' \
    -ex 'handle SIGPIPE nostop noprint pass' \
    -ex 'handle SIG32 nostop noprint pass' \
    -ex 'handle SIGILL stop print nopass' \
    -ex 'handle SIGSEGV stop print nopass' \
    -ex 'handle SIGTRAP stop print nopass' \
    -ex run \
    -ex 'printf "\n--- registers ---\n"' \
    -ex 'info registers' \
    -ex 'printf "\n--- rip disassembly ---\n"' \
    -ex 'x/24i $pc-32' \
    -ex 'printf "\n--- stack words ---\n"' \
    -ex 'x/32gx $rsp' \
    -ex 'thread apply all bt' \
    --args "$@"
} >"$log_file" 2>&1
