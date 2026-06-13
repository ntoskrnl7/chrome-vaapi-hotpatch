#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INJECTOR_DIR="$ROOT/injector"
CHROME="${CHROME:-/opt/google/chrome/chrome}"
if command -v readlink >/dev/null 2>&1; then
  CHROME="$(readlink -f "$CHROME")"
fi
RUNTIME="${CHROME_H265_VAAPI_RUNTIME:-$ROOT/.runtime/live}"
PROFILE="${CHROME_H265_VAAPI_PROFILE:-}"
LOG="$RUNTIME/trampoline.log"
SCOUT_LOG="$RUNTIME/scout.log"
CHROME_LOG="$RUNTIME/chrome.log"
LAUNCHER_LOG="$RUNTIME/launcher.log"
OFFSETS_LOG="$RUNTIME/offsets.env"
OFFSETS_ERR="$RUNTIME/offsets.err"
SO="$INJECTOR_DIR/bin/libchrome_hevc_trampoline_probe.so"
SCOUT_SO="$INJECTOR_DIR/bin/libchrome_hevc_delegate_scout.so"
DELEGATE_SO="$ROOT/delegate-injection/bin/libh265_delegate_bundle_probe.so"
OFFSET_PROBE="$INJECTOR_DIR/tools/probe_version_offsets.py"
SUPPORTED_MAJORS="${CHROME_H265_SUPPORTED_MAJORS:-146 147 148 149}"

detect_vaapi_driver_mode() {
  local requested="${CHROME_H265_VAAPI_DRIVER:-auto}"
  case "${requested,,}" in
    nvidia|nv|nvenc)
      echo "nvidia"
      return
      ;;
    intel|ihd|i965)
      echo "intel"
      return
      ;;
    auto|"")
      ;;
    *)
      echo "$requested"
      return
      ;;
  esac

  local libva_driver="${LIBVA_DRIVER_NAME:-}"
  case "${libva_driver,,}" in
    *nvidia*|*nvdec*|*nvenc*)
      echo "nvidia"
      return
      ;;
    *intel*|*ihd*|*i965*)
      echo "intel"
      return
      ;;
  esac

  if command -v vainfo >/dev/null 2>&1; then
    local info
    if command -v timeout >/dev/null 2>&1; then
      info="$(timeout "${CHROME_H265_VAINFO_TIMEOUT:-2s}" vainfo 2>&1 || true)"
    else
      info="$(vainfo 2>&1 || true)"
    fi
    if grep -Eiq 'VA-API NVDEC|NVENC|NVIDIA' <<<"$info"; then
      echo "nvidia"
      return
    fi
    if grep -Eiq 'Intel iHD|Intel i965' <<<"$info"; then
      echo "intel"
      return
    fi
  fi

  echo "auto"
}

feature_arg() {
  local features="AcceleratedVideoEncoder"
  if [[ "${CHROME_H265_ENABLE_VAAPI_IGNORE_DRIVER_CHECKS:-0}" == "1" ]]; then
    features+=",VaapiIgnoreDriverChecks"
  fi
  if [[ -n "${CHROME_H265_EXTRA_FEATURES:-}" ]]; then
    features+=",$CHROME_H265_EXTRA_FEATURES"
  fi
  echo "${CHROME_H265_ENABLE_FEATURES:-$features}"
}

mkdir -p "$RUNTIME"
rm -f "$LOG" "$SCOUT_LOG" "$CHROME_LOG" "$LAUNCHER_LOG" "$OFFSETS_LOG" "$OFFSETS_ERR"

make -C "$INJECTOR_DIR" >/dev/null
if [[ ! -x "$DELEGATE_SO" ]]; then
  "$ROOT/delegate-injection/build-probe-bundle.sh" >/dev/null
fi

CHROME_VERSION="$("$CHROME" --version 2>/dev/null || true)"
CHROME_MAJOR="$(sed -nE 's/.* ([0-9]+)\..*/\1/p' <<<"$CHROME_VERSION" | head -n1)"
if [[ -n "${CHROME_H265_EXPECT_MAJOR:-}" &&
      "$CHROME_MAJOR" != "$CHROME_H265_EXPECT_MAJOR" ]]; then
  echo "unsupported Chrome version for this launcher: $CHROME_VERSION" >&2
  echo "expected major version: $CHROME_H265_EXPECT_MAJOR" >&2
  exit 2
fi
if [[ "${CHROME_H265_ALLOW_UNTESTED:-0}" != "1" ]]; then
  supported=0
  for major in $SUPPORTED_MAJORS; do
    if [[ "$CHROME_MAJOR" == "$major" ]]; then
      supported=1
      break
    fi
  done
  if [[ "$supported" != "1" ]]; then
    echo "unsupported Chrome version: $CHROME_VERSION" >&2
    echo "tested/supported major versions: $SUPPORTED_MAJORS" >&2
    echo "set CHROME_H265_ALLOW_UNTESTED=1 only for experiments" >&2
    exit 2
  fi
fi

VAAPI_DRIVER_MODE="$(detect_vaapi_driver_mode)"
if [[ "$VAAPI_DRIVER_MODE" == "nvidia" ]]; then
  if [[ "${CHROME_H265_SET_LIBVA_DRIVER_NAME:-1}" == "1" ]]; then
    export LIBVA_DRIVER_NAME=nvidia
  fi
  export CHROME_H265_ENABLE_VAAPI_IGNORE_DRIVER_CHECKS="${CHROME_H265_ENABLE_VAAPI_IGNORE_DRIVER_CHECKS:-1}"
  export CHROME_H265_IGNORE_GPU_BLOCKLIST="${CHROME_H265_IGNORE_GPU_BLOCKLIST:-1}"
  export CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP="${CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP:-1}"
  if [[ "${CHROME_H265_NVIDIA_VENDOR_COMPAT:-0}" == "1" &&
        -z "${CHROME_HEVC_SCOUT_VENDOR_STRING:-}" ]]; then
    export CHROME_HEVC_SCOUT_VENDOR_STRING="Intel iHD driver"
  fi
fi
FEATURES="$(feature_arg)"

if [[ "${CHROME_H265_AUTO_OFFSETS:-1}" == "1" && -x "$OFFSET_PROBE" ]]; then
  "$OFFSET_PROBE" --env "$CHROME" >"$OFFSETS_LOG" 2>"$OFFSETS_ERR" || true
  while IFS= read -r line; do
    [[ "$line" == export\ CHROME_HEVC_OFF_* ]] || continue
    assignment="${line#export }"
    name="${assignment%%=*}"
    value="${assignment#*=}"
    if [[ -z "${!name+x}" ]]; then
      export "$name=$value"
    fi
  done <"$OFFSETS_LOG"
fi

PRELOAD="${CHROME_HEVC_PRELOAD:-$SO:$DELEGATE_SO:$SCOUT_SO}"
COMMON_ENV=(
  "CHROME_HEVC_DELEGATE_SCOUT_LOG=$SCOUT_LOG"
  "CHROME_HEVC_TRAMPOLINE_PROBE_LOG=$LOG"
  "CHROME_HEVC_TRAMPOLINE_CHROME_PATH=$CHROME"
  "CHROME_HEVC_TRAMPOLINE_PROBE=1"
  "CHROME_HEVC_TRAMPOLINE_SPOOF_PROFILE=${CHROME_HEVC_TRAMPOLINE_SPOOF_PROFILE:-1}"
  "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT=${CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_READONLY_POOL=${CHROME_HEVC_TRAMPOLINE_HOOK_READONLY_POOL:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_PREPARE_CPU=${CHROME_HEVC_TRAMPOLINE_HOOK_PREPARE_CPU:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_REQUIRE_BITSTREAM=${CHROME_HEVC_TRAMPOLINE_HOOK_REQUIRE_BITSTREAM:-1}"
  "CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS=${CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK=${CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK:-1}"
  "CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION=${CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH=${CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH:-1}"
  "CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT=${CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT:-1}"
  "CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264=${CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264:-1}"
  "CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265=${CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE_ENCODE_JOB=${CHROME_HEVC_TRAMPOLINE_HOOK_CREATE_ENCODE_JOB:-1}"
  "CHROME_HEVC_TRAMPOLINE_CREATE_JOB_AS_H264=${CHROME_HEVC_TRAMPOLINE_CREATE_JOB_AS_H264:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES=${CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES:-1}"
  "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE=${CHROME_HEVC_TRAMPOLINE_HOOK_CREATE:-1}"
  "CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS=${CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS:-1}"
  "CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP=${CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP:-0}"
  "CHROME_H265_RESOLVED_VAAPI_DRIVER=$VAAPI_DRIVER_MODE"
  "CHROME_HEVC_SHIM_LEVEL=${CHROME_HEVC_SHIM_LEVEL:-180}"
  "CHROME_HEVC_SCOUT_FORCE_CODED_BUFFER_SIZE=${CHROME_HEVC_SCOUT_FORCE_CODED_BUFFER_SIZE:-524288}"
  "LD_PRELOAD=$PRELOAD"
)
if [[ -n "${LIBVA_DRIVER_NAME:-}" ]]; then
  COMMON_ENV+=("LIBVA_DRIVER_NAME=$LIBVA_DRIVER_NAME")
fi
while IFS= read -r name; do
  COMMON_ENV+=("$name=${!name}")
done < <(compgen -A variable CHROME_HEVC_OFF_ | sort)
GPU_LAUNCHER="env ${COMMON_ENV[*]}"

args=("$@")
if [[ "${#args[@]}" -eq 0 ]]; then
  args=("about:blank")
fi
sandbox_args=()
if [[ "${CHROME_H265_NO_SANDBOX:-0}" == "1" ]]; then
  sandbox_args+=(--no-sandbox)
fi
profile_args=()
if [[ -n "$PROFILE" ]]; then
  mkdir -p "$PROFILE"
  profile_args+=(--user-data-dir="$PROFILE")
fi
first_run_args=()
if [[ "${CHROME_H265_NO_FIRST_RUN:-0}" == "1" ]]; then
  first_run_args+=(--no-first-run)
fi
gpu_args=()
if [[ "${CHROME_H265_IGNORE_GPU_BLOCKLIST:-0}" == "1" ]]; then
  gpu_args+=(--ignore-gpu-blocklist)
fi

echo "Chrome: $CHROME"
echo "VAAPI driver mode: $VAAPI_DRIVER_MODE"
if [[ -n "$PROFILE" ]]; then
  echo "Profile: $PROFILE"
else
  echo "Profile: Chrome default"
fi
echo "Logs: $RUNTIME"
{
  echo "Chrome: $CHROME"
  echo "Version: $CHROME_VERSION"
  echo "Supported majors: $SUPPORTED_MAJORS"
  echo "VAAPI driver mode: $VAAPI_DRIVER_MODE"
  echo "LIBVA_DRIVER_NAME: ${LIBVA_DRIVER_NAME:-}"
  if [[ -n "$PROFILE" ]]; then
    echo "Profile: $PROFILE"
  else
    echo "Profile: Chrome default"
  fi
  printf "First-run args:"
  printf " %q" "${first_run_args[@]}"
  printf "\n"
  printf "Sandbox args:"
  printf " %q" "${sandbox_args[@]}"
  printf "\n"
  printf "GPU args:"
  printf " %q" "${gpu_args[@]}"
  printf "\n"
  printf "Feature args: --enable-features=%s\n" "$FEATURES"
} >"$LAUNCHER_LOG"

exec env "${COMMON_ENV[@]}" "$CHROME" \
  "${first_run_args[@]}" \
  "${profile_args[@]}" \
  "${sandbox_args[@]}" \
  "${gpu_args[@]}" \
  --gpu-launcher="$GPU_LAUNCHER" \
  --enable-features="$FEATURES" \
  "${args[@]}" \
  >"$CHROME_LOG" 2>&1
