#!/usr/bin/env bash
set -euo pipefail

LOG="${1:-}"
if [[ -z "$LOG" ]]; then
  LOG="$(ls -t "$(dirname "${BASH_SOURCE[0]}")"/logs/vaapi-probe-*.log 2>/dev/null | head -n1 || true)"
fi
if [[ -z "$LOG" || ! -f "$LOG" ]]; then
  echo "No log found. Run ./run-chrome-probe.sh observe first." >&2
  exit 1
fi

echo "Log: $LOG"
echo

count_matches() {
  local pattern="$1"
  local count
  count="$(rg -c "$pattern" "$LOG" 2>/dev/null || true)"
  if [[ -z "$count" ]]; then
    count=0
  fi
  echo "$count"
}

loaded="$(count_matches 'probe-loaded')"
profiles="$(count_matches 'vaQueryConfigProfiles')"
hevc_profiles="$(count_matches 'profile\\[[0-9]+\\]=1[78]|VAProfileHEVC')"
hevc_entrypoints="$(count_matches 'vaQueryConfigEntrypoints profile=(17|18|23|24|25|26|27|28)')"
hevc_create="$(count_matches 'vaCreateConfig profile=(17|18|23|24|25|26|27|28)')"
h264_profiles="$(count_matches 'VAProfileH264|profile\\[[0-9]+\\]=(6|7|13)')"
h264_entrypoints="$(count_matches 'vaQueryConfigEntrypoints profile=(6|7|13)')"
h264_create="$(count_matches 'vaCreateConfig profile=(6|7|13)')"

printf 'probe-loaded: %s\n' "$loaded"
printf 'vaQueryConfigProfiles calls: %s\n' "$profiles"
printf 'H.264 profiles observed: %s\n' "$h264_profiles"
printf 'H.264 entrypoint queries: %s\n' "$h264_entrypoints"
printf 'H.264 vaCreateConfig calls: %s\n' "$h264_create"
printf 'HEVC profiles observed/injected: %s\n' "$hevc_profiles"
printf 'HEVC entrypoint queries: %s\n' "$hevc_entrypoints"
printf 'HEVC vaCreateConfig calls: %s\n' "$hevc_create"
echo

if [[ "$h264_create" != "0" ]]; then
  echo "Verdict: Chrome reached H.264 VAAPI config creation. If playback still is not accelerated, the blocker is after VA config creation: surface allocation, sandbox/device access, GL import, zero-copy, or decoder fallback."
elif [[ "$h264_entrypoints" != "0" ]]; then
  echo "Verdict: Chrome queried H.264 VAAPI entrypoints but did not create a config. The blocker is likely attribute filtering, driver checks, or GPU feature policy."
elif [[ "$h264_profiles" != "0" ]]; then
  echo "Verdict: H.264 profiles are visible in libva results, but Chrome did not ask for H.264 decode entrypoints in this run."
elif [[ "$hevc_create" != "0" ]]; then
  echo "Verdict: Chrome reached libva HEVC config creation. A capability/driver-level hook may be enough to experiment further."
elif [[ "$hevc_entrypoints" != "0" ]]; then
  echo "Verdict: Chrome queried HEVC VAAPI entrypoints but did not create a config. The next blocker is likely attributes, driver support, or a later policy check."
elif [[ "$hevc_profiles" != "0" ]]; then
  echo "Verdict: HEVC profiles are visible in libva results, but Chrome did not ask for HEVC decode entrypoints in this run."
elif [[ "$profiles" != "0" ]]; then
  echo "Verdict: Chrome used libva, but no HEVC VAAPI path was observed. This points toward Chrome not having/enabling the HEVC VAAPI decoder path."
else
  echo "Verdict: The preload shim loaded, but Chrome did not call the hooked libva capability APIs. Try a real HEVC video page, check sandbox/LD_PRELOAD inheritance, or run observe with --no-sandbox as an extra arg for diagnosis only."
fi

echo
echo "Relevant lines:"
rg 'probe-loaded|vaQueryConfigProfiles|VAProfileH264|VAProfileHEVC|vaQueryConfigEntrypoints profile=(6|7|13|17|18|23|24|25|26|27|28)|vaCreateConfig profile=(6|7|13|17|18|23|24|25|26|27|28)|vaGetConfigAttributes profile=(6|7|13|17|18|23|24|25|26|27|28)' "$LOG" || true
