#!/usr/bin/env bash
set -euo pipefail

SRC_ROOT="${SRC_ROOT:-/path/to/chromium/src}"
OUT_DIR="${OUT_DIR:-$SRC_ROOT/out/Release}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$PROJECT_ROOT/delegate-injection/bin"
OUT_SO="$BIN_DIR/libh265_delegate_bundle_probe.so"

mkdir -p "$BIN_DIR"
if [[ ! -d "$SRC_ROOT" || "$SRC_ROOT" == "/path/to/chromium/src" ]]; then
  echo "set SRC_ROOT=/path/to/chromium/src before building the delegate bundle" >&2
  exit 1
fi
if [[ ! -d "$OUT_DIR" ]]; then
  echo "missing Chromium output directory: $OUT_DIR" >&2
  exit 1
fi
cd "$SRC_ROOT"

third_party/llvm-build/Release+Asserts/bin/clang++ \
  -shared \
  -std=c++23 \
  -fPIC \
  -fuse-ld=lld \
  -flto=thin \
  -fsplit-lto-unit \
  -D__STDC_CONSTANT_MACROS \
  -D__STDC_FORMAT_MACROS \
  -D_FORTIFY_SOURCE=2 \
  -D_FILE_OFFSET_BITS=64 \
  -D_LARGEFILE_SOURCE \
  -D_LARGEFILE64_SOURCE \
  -DNO_UNWIND_TABLES \
  -D_GNU_SOURCE \
  -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE \
  -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS \
  -D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS \
  -D_LIBCPP_INSTRUMENTED_WITH_ASAN=0 \
  -DUSE_UDEV \
  -DUSE_AURA=1 \
  -DUSE_GLIB=1 \
  -DUSE_OZONE=1 \
  -DOFFICIAL_BUILD \
  -DNDEBUG \
  -DNVALGRIND \
  -DDYNAMIC_ANNOTATIONS_ENABLED=0 \
  -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56 \
  -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56 \
  -DUNSAFE_BUFFERS_BUILD \
  -I"$PROJECT_ROOT/delegate-injection/chrome146_src" \
  -I. \
  -I"$OUT_DIR/gen" \
  -Ibuildtools/third_party/libc++ \
  -Ithird_party/perfetto/include \
  -I"$OUT_DIR/gen/third_party/perfetto/build_config" \
  -I"$OUT_DIR/gen/third_party/perfetto" \
  -I"$OUT_DIR/gen/base/allocator/partition_allocator/src" \
  -Ithird_party/skia \
  -Ithird_party/libgav1/src \
  -Ithird_party/abseil-cpp \
  -Ithird_party/boringssl/src/include \
  -Ithird_party/protobuf/src \
  -Ibase/allocator/partition_allocator/src \
  -fno-exceptions \
  -fno-rtti \
  -fvisibility-inlines-hidden \
  -nostdinc++ \
  -nostdlib++ \
  -isystem third_party/libc++/src/include \
  -isystem third_party/libc++abi/src/include \
  --sysroot=build/linux/debian_bullseye_amd64-sysroot \
  -Wl,--unresolved-symbols=ignore-all \
  -Wl,--exclude-libs,ALL \
  -Wl,-Bsymbolic \
  -Wl,-Bsymbolic-functions \
  -o "$OUT_SO" \
  "$PROJECT_ROOT/delegate-injection/chrome146_src/media/gpu/vaapi/h265_vaapi_video_encoder_delegate.cc" \
  "$PROJECT_ROOT/delegate-injection/src/delegate_factory.cc" \
  "$PROJECT_ROOT/delegate-injection/src/perfetto_trace_stubs.c" \
  "$PROJECT_ROOT/delegate-injection/chrome146_src/media/gpu/vaapi/vaapi_video_encoder_delegate.cc" \
  "$OUT_DIR/obj/media/gpu/vaapi/common/vaapi_wrapper.o" \
  "$OUT_DIR/obj/media/gpu/common/codec_picture.o" \
  "$OUT_DIR/obj/media/gpu/common/h265_dpb.o" \
  "$OUT_DIR/obj/media/gpu/common/h265_builder.o" \
  "$OUT_DIR/obj/media/parsers/parsers/h265_parser.o" \
  "$OUT_DIR/obj/media/parsers/parsers/h265_poc.o" \
  "$OUT_DIR/obj/media/filters/filters/h26x_annex_b_bitstream_builder.o" \
  "$OUT_DIR/obj/media/gpu/common/gpu_video_encode_accelerator_helpers.o" \
  "$OUT_DIR/obj/media/video/video/video_encode_accelerator.o" \
  "$OUT_DIR/obj/media/base/base/video_color_space.o" \
  "$OUT_DIR/obj/media/base/base/video_bitrate_allocation.o" \
  "$OUT_DIR/obj/media/base/base/bitrate.o" \
  "$OUT_DIR/obj/ui/gfx/geometry/geometry/size.o" \
  "$OUT_DIR/obj/ui/gfx/color_space/color_space.o" \
  "$OUT_DIR/obj/ui/gfx/color_space/hdr_metadata.o" \
  "$OUT_DIR/obj/ui/gfx/color_space/hdr_static_metadata.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/track_event_internal.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/event_context.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/tracing_muxer_impl.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/track.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/data_source.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/client_api_without_backends/track_event_category_registry.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/tracing/common/trace_writer_base.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/protozero/protozero/message.o" \
  "$OUT_DIR/obj/third_party/perfetto/src/protozero/protozero/scattered_stream_writer.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/float_conversion.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/parser.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/arg.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/output.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/extension.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/str_format_internal/bind.o" \
  "$OUT_DIR/obj/third_party/abseil-cpp/absl/strings/strings/numbers.o" \
  "$OUT_DIR/obj/base/libbase.a" \
  "$OUT_DIR/obj/base/allocator/partition_allocator/src/partition_alloc/libraw_ptr.a" \
  "$OUT_DIR/obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_base.a" \
  "$OUT_DIR/obj/base/allocator/partition_allocator/src/partition_alloc/liballocator_core.a" \
  "$OUT_DIR/obj/buildtools/third_party/libc++/libc++.a" \
  "$OUT_DIR/obj/buildtools/third_party/libc++abi/libc++abi.a" \
  -ldl \
  -lpthread \
  -lm

echo "$OUT_SO"
