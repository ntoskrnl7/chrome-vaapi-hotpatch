#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKDIR="${CHROMIUM_WORKDIR:-$ROOT/.runtime/chromium}"
DEPOT_TOOLS_DIR="${DEPOT_TOOLS_DIR:-$WORKDIR/depot_tools}"
CHROMIUM_PARENT="$WORKDIR/chromium"
SRC_ROOT="${SRC_ROOT:-$CHROMIUM_PARENT/src}"
OUT_DIR="${OUT_DIR:-$SRC_ROOT/out/Release}"
CHROMIUM_REF="${CHROMIUM_REF:-146.0.7680.177}"
GN_ARGS="${GN_ARGS:-is_debug=false is_component_build=false is_official_build=true use_sysroot=true use_vaapi=true proprietary_codecs=true ffmpeg_branding=\"Chrome\" symbol_level=0 treat_warnings_as_errors=false}"

DEFAULT_NINJA_TARGETS=(
  obj/media/gpu/vaapi/common/vaapi_wrapper.o
  obj/media/gpu/common/codec_picture.o
  obj/media/gpu/common/h265_dpb.o
  obj/media/gpu/common/h265_builder.o
  obj/media/parsers/parsers/h265_parser.o
  obj/media/parsers/parsers/h265_poc.o
  obj/media/filters/filters/h26x_annex_b_bitstream_builder.o
  obj/media/gpu/common/gpu_video_encode_accelerator_helpers.o
  obj/media/video/video/video_encode_accelerator.o
  obj/media/base/base/video_color_space.o
  obj/media/base/base/video_bitrate_allocation.o
  obj/media/base/base/bitrate.o
  obj/ui/gfx/geometry/geometry/size.o
  obj/ui/gfx/color_space/color_space.o
  obj/ui/gfx/color_space/hdr_metadata.o
  obj/ui/gfx/color_space/hdr_static_metadata.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/track_event_internal.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/event_context.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/tracing_muxer_impl.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/track.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/data_source.o
  obj/third_party/perfetto/src/tracing/client_api_without_backends/track_event_category_registry.o
  obj/third_party/perfetto/src/tracing/common/trace_writer_base.o
  obj/third_party/perfetto/src/protozero/protozero/message.o
  obj/third_party/perfetto/src/protozero/protozero/scattered_stream_writer.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/float_conversion.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/parser.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/arg.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/output.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/extension.o
  obj/third_party/abseil-cpp/absl/strings/str_format_internal/bind.o
  obj/third_party/abseil-cpp/absl/strings/strings/numbers.o
  obj/base/libbase.a
  obj/base/allocator/partition_allocator/src/partition_alloc/libraw_ptr.a
  obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_base.a
  obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_core.a
  obj/buildtools/third_party/libc++/libc++.a
  obj/buildtools/third_party/libc++abi/libc++abi.a
)

if [[ -n "${CHROMIUM_NINJA_TARGETS:-}" ]]; then
  read -r -a NINJA_TARGETS <<<"$CHROMIUM_NINJA_TARGETS"
else
  NINJA_TARGETS=("${DEFAULT_NINJA_TARGETS[@]}")
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 1
  fi
}

need_cmd git
need_cmd python3

mkdir -p "$WORKDIR"

if [[ ! -d "$DEPOT_TOOLS_DIR/.git" ]]; then
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
    "$DEPOT_TOOLS_DIR"
fi

export PATH="$DEPOT_TOOLS_DIR:$PATH"
need_cmd fetch
need_cmd gclient

if [[ ! -d "$SRC_ROOT/.git" ]]; then
  mkdir -p "$CHROMIUM_PARENT"
  (
    cd "$CHROMIUM_PARENT"
    fetch --nohooks chromium
  )
fi

cd "$SRC_ROOT"
git fetch --tags --force
if ! git checkout "$CHROMIUM_REF"; then
  git checkout "tags/$CHROMIUM_REF"
fi

if [[ "${INSTALL_CHROMIUM_BUILD_DEPS:-0}" == "1" ]]; then
  if [[ ! -x build/install-build-deps.sh ]]; then
    echo "missing Chromium dependency installer: build/install-build-deps.sh" >&2
    exit 1
  fi
  build/install-build-deps.sh
fi

gclient sync --with_branch_heads --with_tags -D

need_cmd gn
need_cmd autoninja

gn gen "$OUT_DIR" --args="$GN_ARGS"
autoninja -C "$OUT_DIR" "${NINJA_TARGETS[@]}"

SRC_ROOT="$SRC_ROOT" OUT_DIR="$OUT_DIR" \
  "$ROOT/delegate-injection/build-probe-bundle.sh"
