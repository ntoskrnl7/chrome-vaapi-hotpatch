#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_LAUNCHER="$ROOT/dist/chrome-vaapi-h264-h265-hotpatch/run-chrome-h264-h265-vaapi.sh"
TEST_URL="file://$ROOT/test/h265-encode-visual.html"
RUNTIME_ROOT="${CHROME_H265_VERSION_MATRIX_RUNTIME:-$ROOT/.runtime/version-matrix}"
TIMEOUT_SECONDS="${CHROME_H265_VERSION_MATRIX_TIMEOUT:-18}"
DOWNLOAD="${CHROME_H265_VERSION_MATRIX_DOWNLOAD:-1}"

channels=(installed stable beta unstable)

usage() {
  cat <<EOF
Usage: ${0##*/} [channel ...]

Channels:
  installed  currently installed /opt/google/chrome/chrome
  stable     google-chrome-stable_current_amd64.deb
  beta       google-chrome-beta_current_amd64.deb
  unstable   google-chrome-unstable_current_amd64.deb

Environment:
  CHROME_H265_VERSION_MATRIX_RUNTIME   output directory
  CHROME_H265_VERSION_MATRIX_TIMEOUT   seconds per browser run, default 18
  CHROME_H265_VERSION_MATRIX_DOWNLOAD  set 0 to skip .deb downloads
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "$#" -gt 0 ]]; then
  channels=("$@")
fi

if [[ ! -x "$DIST_LAUNCHER" ]]; then
  "$ROOT/make-dist.sh" >/dev/null
fi

mkdir -p "$RUNTIME_ROOT/downloads" "$RUNTIME_ROOT/extracted"
SUMMARY="$RUNTIME_ROOT/summary.tsv"
printf "channel\tversion\tstatus\truntime\n" >"$SUMMARY"

deb_url_for_channel() {
  case "$1" in
    stable) echo "https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb" ;;
    beta) echo "https://dl.google.com/linux/direct/google-chrome-beta_current_amd64.deb" ;;
    unstable) echo "https://dl.google.com/linux/direct/google-chrome-unstable_current_amd64.deb" ;;
    *) return 1 ;;
  esac
}

chrome_path_for_channel() {
  case "$1" in
    installed) echo "/opt/google/chrome/chrome" ;;
    stable) echo "$RUNTIME_ROOT/extracted/stable/opt/google/chrome/chrome" ;;
    beta) echo "$RUNTIME_ROOT/extracted/beta/opt/google/chrome-beta/chrome" ;;
    unstable) echo "$RUNTIME_ROOT/extracted/unstable/opt/google/chrome-unstable/chrome" ;;
    *) return 1 ;;
  esac
}

prepare_channel() {
  local channel="$1"

  if [[ "$channel" == "installed" ]]; then
    return 0
  fi

  local url deb extract_dir
  url="$(deb_url_for_channel "$channel")"
  deb="$RUNTIME_ROOT/downloads/$channel.deb"
  extract_dir="$RUNTIME_ROOT/extracted/$channel"

  if [[ "$DOWNLOAD" == "1" ]]; then
    curl -L --fail --retry 2 -o "$deb" "$url"
  elif [[ ! -f "$deb" ]]; then
    echo "missing cached deb: $deb" >&2
    return 1
  fi

  rm -rf "$extract_dir"
  mkdir -p "$extract_dir"
  dpkg-deb -x "$deb" "$extract_dir"
}

run_one() {
  local channel="$1"
  local chrome runtime version status
  chrome="$(chrome_path_for_channel "$channel")"
  runtime="$RUNTIME_ROOT/runs/$channel"
  profile="$RUNTIME_ROOT/profiles/$channel"
  status="FAIL"

  if [[ ! -x "$chrome" ]]; then
    printf "%s\t%s\t%s\t%s\n" "$channel" "missing" "SKIP" "$runtime" | tee -a "$SUMMARY"
    return 0
  fi

  version="$("$chrome" --version 2>/dev/null || true)"
  rm -rf "$runtime"
  rm -rf "$profile"
  mkdir -p "$runtime" "$profile"

  echo "== $channel: $version =="
  if "$ROOT/injector/tools/probe_version_offsets.py" --env "$chrome" \
      >"$runtime/offsets.env" 2>"$runtime/offsets.err"; then
    # shellcheck disable=SC1091
    . "$runtime/offsets.env"
  fi

  CHROME="$chrome" \
    CHROME_H265_VAAPI_RUNTIME="$runtime" \
    CHROME_H265_VAAPI_PROFILE="$profile" \
    CHROME_H265_NO_FIRST_RUN=1 \
    timeout "$TIMEOUT_SECONDS" "$DIST_LAUNCHER" "$TEST_URL" >/dev/null 2>&1 || true

  local metadata_count va_end_count
  metadata_count="$(rg -c "chrome_hevc_shim: GetMetadata payload=" "$runtime/chrome.log" 2>/dev/null || true)"
  va_end_count="$(rg -c "vaEndPicture ret=0" "$runtime/scout.log" 2>/dev/null || true)"
  if rg -q "CreateEncodeJob returned 0x" "$runtime/trampoline.log" 2>/dev/null &&
     [[ "$metadata_count" -ge 10 ]] &&
     [[ "$va_end_count" -ge 10 ]] &&
     ! rg -q "fatal|encoder error|Encode error|Initialize failed|caught signal|exit_code=132|GPU process exited unexpectedly" "$runtime" 2>/dev/null; then
    status="PASS"
  fi

  printf "%s\t%s\t%s\t%s\n" "$channel" "$version" "$status" "$runtime" | tee -a "$SUMMARY"
}

for channel in "${channels[@]}"; do
  case "$channel" in
    installed|stable|beta|unstable) ;;
    *)
      echo "unknown channel: $channel" >&2
      usage >&2
      exit 2
      ;;
  esac

  prepare_channel "$channel" || {
    printf "%s\t%s\t%s\t%s\n" "$channel" "prepare-failed" "SKIP" "$RUNTIME_ROOT/runs/$channel" | tee -a "$SUMMARY"
    continue
  }
  run_one "$channel"
done

echo
echo "Summary: $SUMMARY"
