// Copyright 2026
//
// C ABI bridge used by the runtime trampoline.  The actual delegate
// implementation is linked from the patched Electron/Chromium object files.

#include "media/gpu/vaapi/h265_vaapi_video_encoder_delegate.h"

#include <stdint.h>
#include <string.h>
#include <new>
#include <ostream>
#include <stdio.h>
#include <stdlib.h>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "media/base/bitrate.h"
#include "media/base/video_types.h"
#include "media/gpu/vaapi/vaapi_video_encoder_delegate.h"
#include "media/gpu/vaapi/vaapi_wrapper.h"

namespace {

int EnvInt(const char* name, int fallback) {
  const char* value = getenv(name);
  if (!value || !*value) {
    return fallback;
  }
  return atoi(value);
}

uint32_t EnvUint(const char* name, uint32_t fallback) {
  int value = EnvInt(name, (int)fallback);
  return value > 0 ? (uint32_t)value : fallback;
}

gfx::Size GuessVisibleSizeFromChromeConfig(
    const media::VideoEncodeAccelerator::Config& chrome_config) {
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&chrome_config);
  for (size_t off = 0; off + 8 <= 512; off += 4) {
    int32_t width = 0;
    int32_t height = 0;
    memcpy(&width, raw + off, sizeof(width));
    memcpy(&height, raw + off + 4, sizeof(height));
    if (width >= 16 && width <= 8192 && height >= 16 && height <= 8192 &&
        (width % 2) == 0 && (height % 2) == 0) {
      fprintf(stderr,
              "chrome_hevc_shim: guessed visible size at +0x%zx: %dx%d\n",
              off, width, height);
      return gfx::Size(width, height);
    }
  }

  int width = EnvInt("CHROME_HEVC_SHIM_WIDTH", 1520);
  int height = EnvInt("CHROME_HEVC_SHIM_HEIGHT", 608);
  fprintf(stderr,
          "chrome_hevc_shim: using fallback visible size: %dx%d\n", width,
          height);
  return gfx::Size(width, height);
}

uint32_t GuessFramerateFromChromeConfig(
    const media::VideoEncodeAccelerator::Config& chrome_config) {
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&chrome_config);
  for (size_t off = 0; off + 4 <= 512; off += 4) {
    uint32_t value = 0;
    memcpy(&value, raw + off, sizeof(value));
    if (value >= 1 && value <= 240) {
      if (value == 30 || value == 60) {
        fprintf(stderr, "chrome_hevc_shim: guessed framerate at +0x%zx: %u\n",
                off, value);
        return value;
      }
    }
  }
  return EnvUint("CHROME_HEVC_SHIM_FRAMERATE", 30);
}

uint32_t GuessBitrateFromChromeConfig(
    const media::VideoEncodeAccelerator::Config& chrome_config) {
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&chrome_config);
  for (size_t off = 0; off + 4 <= 512; off += 4) {
    uint32_t value = 0;
    memcpy(&value, raw + off, sizeof(value));
    if (value >= 100000 && value <= 200000000) {
      fprintf(stderr, "chrome_hevc_shim: guessed bitrate at +0x%zx: %u\n",
              off, value);
      return value;
    }
  }
  return EnvUint("CHROME_HEVC_SHIM_BITRATE", 1837392);
}

media::VideoPixelFormat GuessInputFormatFromChromeConfig(
    const media::VideoEncodeAccelerator::Config& chrome_config) {
  int32_t value = 0;
  memcpy(&value, reinterpret_cast<const uint8_t*>(&chrome_config),
         sizeof(value));
  if (value == media::PIXEL_FORMAT_I420 || value == media::PIXEL_FORMAT_NV12) {
    fprintf(stderr, "chrome_hevc_shim: guessed input format at +0x0: %d\n",
            value);
    return static_cast<media::VideoPixelFormat>(value);
  }
  int fallback = EnvInt("CHROME_HEVC_SHIM_INPUT_FORMAT",
                        static_cast<int>(media::PIXEL_FORMAT_NV12));
  return static_cast<media::VideoPixelFormat>(fallback);
}

media::VideoEncodeAccelerator::Config::StorageType GuessStorageTypeFromChromeConfig(
    const media::VideoEncodeAccelerator::Config& chrome_config) {
  int32_t value = 0;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&chrome_config);
  memcpy(&value, raw + 0x20, sizeof(value));
  if (value == 0 || value == 1) {
    fprintf(stderr, "chrome_hevc_shim: guessed storage type at +0x20: %d\n",
            value);
    return static_cast<media::VideoEncodeAccelerator::Config::StorageType>(
        value);
  }
  int fallback = EnvInt("CHROME_HEVC_SHIM_STORAGE_TYPE", 1);
  return static_cast<media::VideoEncodeAccelerator::Config::StorageType>(
      fallback);
}

class H265VaapiVideoEncoderDelegateShim
    : public media::H265VaapiVideoEncoderDelegate {
 public:
  H265VaapiVideoEncoderDelegateShim(scoped_refptr<media::VaapiWrapper> wrapper,
                                    base::RepeatingClosure error_cb)
      : media::H265VaapiVideoEncoderDelegate(std::move(wrapper), error_cb) {}

  bool Initialize(
      const media::VideoEncodeAccelerator::Config& chrome_config,
      const media::VaapiVideoEncoderDelegate::Config& ave_config) override {
    gfx::Size visible_size = GuessVisibleSizeFromChromeConfig(chrome_config);
    uint32_t framerate = GuessFramerateFromChromeConfig(chrome_config);
    uint32_t bitrate = GuessBitrateFromChromeConfig(chrome_config);
    media::VideoPixelFormat input_format =
        GuessInputFormatFromChromeConfig(chrome_config);
    media::VideoEncodeAccelerator::Config::StorageType storage_type =
        GuessStorageTypeFromChromeConfig(chrome_config);

    media::VideoEncodeAccelerator::Config local_config(
        input_format, visible_size, media::HEVCPROFILE_MAIN,
        media::Bitrate::ConstantBitrate(bitrate), framerate, storage_type,
        media::VideoEncodeAccelerator::Config::ContentType::kCamera);
    local_config.gop_length = 10000;
    int hevc_level = EnvInt("CHROME_HEVC_SHIM_LEVEL", 0);
    if (hevc_level > 0 && hevc_level <= 255) {
      local_config.hevc_output_level = (uint8_t)hevc_level;
    }

    fprintf(stderr,
            "chrome_hevc_shim: Initialize local size=%dx%d bitrate=%u "
            "framerate=%u level=%d input_format=%d storage=%d\n",
            visible_size.width(), visible_size.height(), bitrate, framerate,
            hevc_level, static_cast<int>(input_format),
            static_cast<int>(storage_type));

    bool ok =
        media::H265VaapiVideoEncoderDelegate::Initialize(local_config,
                                                         ave_config);
    if (ok) {
      coded_size_ = media::H265VaapiVideoEncoderDelegate::GetCodedSize();
      fprintf(stderr, "chrome_hevc_shim: Initialize ok coded=%dx%d\n",
              coded_size_.width(), coded_size_.height());
    } else {
      fprintf(stderr, "chrome_hevc_shim: Initialize failed\n");
    }
    return ok;
  }

  bool UpdateRates(const media::VideoBitrateAllocation& bitrate_allocation,
                   uint32_t framerate) override {
    fprintf(stderr, "chrome_hevc_shim: UpdateRates framerate=%u\n",
            framerate);
    return media::H265VaapiVideoEncoderDelegate::UpdateRates(
        bitrate_allocation, framerate);
  }

  gfx::Size GetCodedSize() const override {
    fprintf(stderr, "chrome_hevc_shim: GetCodedSize -> %dx%d\n",
            coded_size_.width(), coded_size_.height());
    return coded_size_;
  }

  size_t GetBitstreamBufferSize() const override {
    size_t size = EnvUint("CHROME_HEVC_SHIM_BITSTREAM_BUFFER_SIZE", 2 * 1024 * 1024);
    fprintf(stderr, "chrome_hevc_shim: GetBitstreamBufferSize -> %zu\n", size);
    return size;
  }

  size_t GetMaxNumOfRefFrames() const override {
    fprintf(stderr, "chrome_hevc_shim: GetMaxNumOfRefFrames -> 1\n");
    return 1;
  }

  std::vector<gfx::Size> GetSVCLayerResolutions() override {
    fprintf(stderr, "chrome_hevc_shim: GetSVCLayerResolutions -> %dx%d\n",
            coded_size_.width(), coded_size_.height());
    return {coded_size_};
  }

  gfx::Size CodedSizeForAbiShim() const { return coded_size_; }

 private:
  gfx::Size coded_size_;
};

}  // namespace

static H265VaapiVideoEncoderDelegateShim* g_delegate_for_abi_slots;

extern "C" __attribute__((visibility("default"))) void
chrome_hevc_get_svc_layer_resolutions_abi(
    std::vector<gfx::Size>* out,
    H265VaapiVideoEncoderDelegateShim* self) {
  if (!out) {
    return;
  }
  new (out) std::vector<gfx::Size>();
  gfx::Size coded_size(EnvInt("CHROME_HEVC_SHIM_WIDTH", 1520),
                       EnvInt("CHROME_HEVC_SHIM_HEIGHT", 608));
  if (self) {
    gfx::Size from_delegate = self->CodedSizeForAbiShim();
    if (!from_delegate.IsEmpty()) {
      coded_size = from_delegate;
    }
  }
  out->push_back(coded_size);
  fprintf(stderr,
          "chrome_hevc_shim: GetSVCLayerResolutions ABI slot -> %dx%d "
          "self=%p out=%p\n",
          coded_size.width(), coded_size.height(), self, out);
}

extern "C" __attribute__((visibility("default"))) uintptr_t
chrome_hevc_svc_or_getmax_abi(void* first, void* second) {
  if (first && first == g_delegate_for_abi_slots) {
    fprintf(stderr,
            "chrome_hevc_shim: vtable[7] compatibility call as GetMax -> 1 "
            "self=%p\n",
            first);
    return 1;
  }

  chrome_hevc_get_svc_layer_resolutions_abi(
      reinterpret_cast<std::vector<gfx::Size>*>(first),
      reinterpret_cast<H265VaapiVideoEncoderDelegateShim*>(second));
  return reinterpret_cast<uintptr_t>(first);
}

static void PatchOfficialChromeVtableSlots(
    H265VaapiVideoEncoderDelegateShim* delegate) {
  if (!delegate) {
    return;
  }

  void*** object_vptr = reinterpret_cast<void***>(delegate);
  void** original_vtable = *object_vptr;
  if (!original_vtable) {
    return;
  }

  constexpr size_t kCopiedVtableEntries = 64;
  void** patched_vtable = new void*[kCopiedVtableEntries];
  memcpy(patched_vtable, original_vtable,
         sizeof(void*) * kCopiedVtableEntries);

  // Official Chrome 146 has no ResetEncodingState virtual slot. Keep the
  // injected local source aligned with that vtable and patch only known ABI
  // sensitive calls.
  constexpr size_t kElectronH265PrepareEncodeJobIndex = 9;
  constexpr size_t kOfficialChrome146PrepareEncodeJobIndex = 9;
  void* old_prepare_entry =
      patched_vtable[kOfficialChrome146PrepareEncodeJobIndex];
  patched_vtable[kOfficialChrome146PrepareEncodeJobIndex] =
      original_vtable[kElectronH265PrepareEncodeJobIndex];

  constexpr size_t kOfficialChrome146GetSvcLayerResolutionsIndex = 0x38 / 8;
  void* old_entry =
      patched_vtable[kOfficialChrome146GetSvcLayerResolutionsIndex];
  patched_vtable[kOfficialChrome146GetSvcLayerResolutionsIndex] =
      reinterpret_cast<void*>(&chrome_hevc_svc_or_getmax_abi);
  *object_vptr = patched_vtable;
  g_delegate_for_abi_slots = delegate;

  fprintf(stderr,
          "chrome_hevc_shim: patched vtable[%zu] for official Chrome "
          "PrepareEncodeJob slot old=%p new=%p source_index=%zu\n",
          kOfficialChrome146PrepareEncodeJobIndex, old_prepare_entry,
          patched_vtable[kOfficialChrome146PrepareEncodeJobIndex],
          kElectronH265PrepareEncodeJobIndex);
  fprintf(stderr,
          "chrome_hevc_shim: patched vtable[%zu] for official Chrome SVC "
          "slot old=%p new=%p delegate=%p vtable=%p\n",
          kOfficialChrome146GetSvcLayerResolutionsIndex, old_entry,
          patched_vtable[kOfficialChrome146GetSvcLayerResolutionsIndex],
          delegate, patched_vtable);
}

extern "C" __attribute__((visibility("default"))) void*
chrome_hevc_create_h265_delegate(void* vaapi_wrapper_raw,
                                 void* error_cb_raw) {
  auto* vaapi_wrapper =
      reinterpret_cast<media::VaapiWrapper*>(vaapi_wrapper_raw);
  auto* error_cb = reinterpret_cast<base::RepeatingClosure*>(error_cb_raw);
  if (!vaapi_wrapper || !error_cb) {
    fprintf(stderr,
            "chrome_hevc_create_h265_delegate: invalid args wrapper=%p cb=%p\n",
            vaapi_wrapper_raw, error_cb_raw);
    return nullptr;
  }

  scoped_refptr<media::VaapiWrapper> vaapi_wrapper_ref(vaapi_wrapper);
  auto* delegate =
      new H265VaapiVideoEncoderDelegateShim(std::move(vaapi_wrapper_ref),
                                            *error_cb);
  PatchOfficialChromeVtableSlots(delegate);
  return delegate;
}

extern "C" __attribute__((visibility("default"))) void*
chrome_hevc_construct_h265_delegate(void* storage,
                                    void* vaapi_wrapper_raw,
                                    void* error_cb_bind_state) {
  auto* vaapi_wrapper =
      reinterpret_cast<media::VaapiWrapper*>(vaapi_wrapper_raw);
  if (!storage || !vaapi_wrapper || !error_cb_bind_state) {
    fprintf(stderr,
            "chrome_hevc_construct_h265_delegate: invalid args storage=%p "
            "wrapper=%p cb=%p\n",
            storage, vaapi_wrapper_raw, error_cb_bind_state);
    return nullptr;
  }

  fprintf(stderr,
          "chrome_hevc_construct_h265_delegate: sizeof(H265)=%zu storage=%p\n",
          sizeof(media::H265VaapiVideoEncoderDelegate), storage);

  base::RepeatingClosure error_cb;
  static_assert(sizeof(error_cb) == sizeof(error_cb_bind_state),
                "RepeatingClosure ABI is expected to be one pointer");
  memcpy(&error_cb, &error_cb_bind_state, sizeof(error_cb_bind_state));

  auto* delegate = new (storage)
      H265VaapiVideoEncoderDelegateShim(base::AdoptRef(vaapi_wrapper),
                                        error_cb);
  PatchOfficialChromeVtableSlots(delegate);
  return delegate;
}

extern "C" __attribute__((visibility("default"))) void*
chrome_hevc_create_h265_delegate_from_abi(void* vaapi_wrapper_raw,
                                          void* error_cb_bind_state) {
  auto* vaapi_wrapper =
      reinterpret_cast<media::VaapiWrapper*>(vaapi_wrapper_raw);
  if (!vaapi_wrapper || !error_cb_bind_state) {
    fprintf(stderr,
            "chrome_hevc_create_h265_delegate_from_abi: invalid args "
            "wrapper=%p cb=%p\n",
            vaapi_wrapper_raw, error_cb_bind_state);
    return nullptr;
  }

  base::RepeatingClosure error_cb;
  static_assert(sizeof(error_cb) == sizeof(error_cb_bind_state),
                "RepeatingClosure ABI is expected to be one pointer");
  memcpy(&error_cb, &error_cb_bind_state, sizeof(error_cb_bind_state));

  auto* delegate =
      new H265VaapiVideoEncoderDelegateShim(base::AdoptRef(vaapi_wrapper),
                                            error_cb);
  PatchOfficialChromeVtableSlots(delegate);
  fprintf(stderr,
          "chrome_hevc_create_h265_delegate_from_abi: sizeof(H265)=%zu "
          "delegate=%p\n",
          sizeof(media::H265VaapiVideoEncoderDelegate), delegate);
  return delegate;
}
