// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "media/gpu/vaapi/h265_vaapi_video_encoder_delegate.h"

#include <va/va.h>
#include <va/va_enc_hevc.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <stdio.h>
#include <strings.h>

#include "base/bits.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/notimplemented.h"
#include "base/numerics/checked_math.h"
#include "base/numerics/safe_conversions.h"
#include "media/gpu/h265_builder.h"
#include "media/gpu/gpu_video_encode_accelerator_helpers.h"
#include "media/gpu/h265_dpb.h"
#include "media/gpu/macros.h"
#include "media/gpu/vaapi/vaapi_common.h"
#include "media/gpu/vaapi/vaapi_wrapper.h"
#include "media/parsers/h265_parser.h"
#include "media/video/video_encode_accelerator.h"

namespace media {

namespace {

constexpr size_t kLegacyIDRPeriod = 2048;
constexpr size_t kIntelDefaultIntraPeriod = 120;
constexpr size_t kIntelDisplayIntraPeriod = 30;
constexpr uint32_t kCPBWindowSizeMs = 1500;
constexpr uint8_t kLegacyDefaultInitialQp = 26;
constexpr uint8_t kIntelDefaultInitialQp = 20;
constexpr uint8_t kIntelDisplayInitialQp = 18;
constexpr uint32_t kLegacyDefaultHevcLevelIdc = 120;  // Level 4.0.
constexpr uint32_t kIntelDefaultHevcLevelIdc = 93;    // Level 3.1.
constexpr uint32_t kDefaultIPPeriod = 1;
constexpr uint8_t kDefaultLog2MaxPicOrderCntLsbMinus4 = 8;
constexpr int kLegacyMinCodingBlockSizeInPixels = 8;
constexpr int kLegacyCTUSizeInPixels = 64;
constexpr int kIntelSurfaceAlignmentInPixels = 16;
constexpr int kIntelCTUSizeInPixels = 32;

bool EnvValueIsAnyOf(const char* value,
                     const std::initializer_list<const char*>& names) {
  if (!value || !*value) {
    return false;
  }
  for (const char* name : names) {
    if (strcasecmp(value, name) == 0) {
      return true;
    }
  }
  return false;
}

bool IsIntelVaapiImplementation() {
  const char* tuning_override = getenv("CHROME_HEVC_SHIM_INTEL_TUNING");
  if (EnvValueIsAnyOf(tuning_override, {"1", "true", "yes", "intel"})) {
    return true;
  }
  if (EnvValueIsAnyOf(tuning_override,
                      {"0", "false", "no", "legacy", "nvidia"})) {
    return false;
  }

  const char* resolved_driver = getenv("CHROME_H265_RESOLVED_VAAPI_DRIVER");
  if (EnvValueIsAnyOf(resolved_driver, {"intel", "ihd", "i965"})) {
    return true;
  }
  if (EnvValueIsAnyOf(resolved_driver, {"nvidia", "nv", "nvenc"})) {
    return false;
  }

  const char* requested_driver = getenv("CHROME_H265_VAAPI_DRIVER");
  if (EnvValueIsAnyOf(requested_driver, {"intel", "ihd", "i965"})) {
    return true;
  }
  if (EnvValueIsAnyOf(requested_driver, {"nvidia", "nv", "nvenc"})) {
    return false;
  }

  switch (VaapiWrapper::GetImplementationType()) {
    case VAImplementation::kIntelIHD:
    case VAImplementation::kIntelI965:
      return true;
    case VAImplementation::kInvalid:
      // In the injected bundle this static state can be unresolved even while
      // the borrowed Chrome VaapiWrapper object is usable. Preserve the
      // historical Intel-tuned default unless the launcher resolved NVIDIA.
      return true;
    default:
      return false;
  }
}

bool SupportsPackedHeaders(VideoCodecProfile profile) {
  switch (profile) {
    case HEVCPROFILE_MAIN:
    case HEVCPROFILE_MAIN10:
    case HEVCPROFILE_MAIN_STILL_PICTURE:
    case HEVCPROFILE_REXT:
    case HEVCPROFILE_HIGH_THROUGHPUT:
      return true;
    default:
      return false;
  }
}

bool SuppressPackedHeadersForVisibleSizeShmemMain10(
    const VideoEncodeAccelerator::Config& config,
    const gfx::Size& visible_size,
    const gfx::Size& coded_size,
    bool use_intel_hevc_tuning) {
  return config.output_profile == HEVCPROFILE_MAIN10 &&
         (!use_intel_hevc_tuning || config.input_format == PIXEL_FORMAT_I420) &&
         config.storage_type ==
             VideoEncodeAccelerator::Config::StorageType::kShmem &&
         visible_size != coded_size;
}

bool EnvFlagEnabledOrDefault(const char* name, bool default_value) {
  const char* value = getenv(name);
  if (!value || !*value) {
    return default_value;
  }
  return strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0 &&
         strcasecmp(value, "no") != 0;
}

uint32_t EnvUintInRangeOrDefault(const char* name,
                                 uint32_t default_value,
                                 uint32_t max_value) {
  const char* value = getenv(name);
  if (!value || !*value) {
    return default_value;
  }
  char* end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);
  if (!end || *end != '\0' || parsed == 0 || parsed > max_value) {
    return default_value;
  }
  return static_cast<uint32_t>(parsed);
}

void InitVAPictureHEVC(VAPictureHEVC* va_pic) {
  *va_pic = {};
  va_pic->picture_id = VA_INVALID_ID;
  va_pic->flags = VA_PICTURE_HEVC_INVALID;
}

VASurfaceID GetH265SurfaceId(const scoped_refptr<H265Picture>& pic) {
  // The hotpatch path borrows Chrome-created H.264 pictures because the stock
  // Chrome binary has no HEVC EncodeJob allocation path. Store the VA surface
  // id in this otherwise unused H265Picture field and avoid virtual calls into
  // local VaapiH265Picture methods for Chrome-owned objects.
  return pic->pic_latency_count_;
}

void FillVAPictureHEVC(VAPictureHEVC* va_pic,
                       const scoped_refptr<H265Picture>& pic,
                       H265Picture::ReferenceType ref_type) {
  CHECK(va_pic);
  CHECK(pic);

  va_pic->picture_id = GetH265SurfaceId(pic);
  va_pic->pic_order_cnt = pic->pic_order_cnt_val_;
  va_pic->flags = 0;

  switch (ref_type) {
    case H265Picture::kShortTermCurrBefore:
      va_pic->flags |= VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE;
      break;
    case H265Picture::kShortTermCurrAfter:
      va_pic->flags |= VA_PICTURE_HEVC_RPS_ST_CURR_AFTER;
      break;
    case H265Picture::kLongTermCurr:
      va_pic->flags |= VA_PICTURE_HEVC_RPS_LT_CURR;
      break;
    default:
      break;
  }

  if (ref_type == H265Picture::kLongTermCurr ||
      ref_type == H265Picture::kLongTermFoll) {
    va_pic->flags |= VA_PICTURE_HEVC_LONG_TERM_REFERENCE;
  }
}

template <typename VAEncMiscParam>
VAEncMiscParam& AllocateMiscParameterBuffer(
    std::vector<uint8_t>& misc_buffer,
    VAEncMiscParameterType misc_param_type) {
  constexpr size_t kBufferSize =
      sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParam);
  misc_buffer.resize(kBufferSize);
  auto* va_buffer = reinterpret_cast<VAEncMiscParameterBuffer*>(
      misc_buffer.data());
  va_buffer->type = misc_param_type;
  return *reinterpret_cast<VAEncMiscParam*>(va_buffer->data);
}

void CreateVAEncRateControlParams(
    uint32_t bits_per_second,
    uint32_t target_percentage,
    uint32_t window_size,
    uint32_t framerate,
    uint32_t buffer_size,
    base::span<std::vector<uint8_t>, 3> misc_buffers) {
  auto& rate_control_param =
      AllocateMiscParameterBuffer<VAEncMiscParameterRateControl>(
          misc_buffers[0], VAEncMiscParameterTypeRateControl);
  rate_control_param.bits_per_second = bits_per_second;
  rate_control_param.target_percentage = target_percentage;
  rate_control_param.window_size = window_size;

  auto& hrd_param = AllocateMiscParameterBuffer<VAEncMiscParameterHRD>(
      misc_buffers[1], VAEncMiscParameterTypeHRD);
  hrd_param.buffer_size = buffer_size;
  hrd_param.initial_buffer_fullness = (buffer_size * 3) / 4;

  auto& frame_rate_param =
      AllocateMiscParameterBuffer<VAEncMiscParameterFrameRate>(
          misc_buffers[2], VAEncMiscParameterTypeFrameRate);
  frame_rate_param.framerate = (1u << 16) | framerate;
}

std::optional<uint8_t> ToGeneralProfileIdc(VideoCodecProfile profile) {
  switch (profile) {
    case HEVCPROFILE_MAIN:
      return H265ProfileTierLevel::kProfileIdcMain;
    case HEVCPROFILE_MAIN10:
      return H265ProfileTierLevel::kProfileIdcMain10;
    case HEVCPROFILE_MAIN_STILL_PICTURE:
      return H265ProfileTierLevel::kProfileIdcMainStill;
    case HEVCPROFILE_REXT:
      return H265ProfileTierLevel::kProfileIdcRangeExtensions;
    case HEVCPROFILE_HIGH_THROUGHPUT:
      return H265ProfileTierLevel::kProfileIdcHighThroughput;
    default:
      return std::nullopt;
  }
}

uint32_t MakeGeneralProfileCompatibilityFlags(uint8_t profile_idc) {
  auto set_profile_flag = [](uint32_t* flags, uint8_t idc) {
    CHECK_LT(idc, 32u);
    *flags |= 1u << (31 - idc);
  };

  uint32_t flags = 0;
  set_profile_flag(&flags, profile_idc);

  // Match FFmpeg's HEVC compatibility flag handling for the common profiles
  // used by this path.
  if (profile_idc == H265ProfileTierLevel::kProfileIdcMain) {
    set_profile_flag(&flags, H265ProfileTierLevel::kProfileIdcMain10);
  } else if (profile_idc == H265ProfileTierLevel::kProfileIdcMainStill) {
    set_profile_flag(&flags, H265ProfileTierLevel::kProfileIdcMain);
    set_profile_flag(&flags, H265ProfileTierLevel::kProfileIdcMain10);
  }

  return flags;
}

uint8_t GetBitDepthMinus8(VideoCodecProfile profile) {
  switch (profile) {
    case HEVCPROFILE_MAIN10:
    case HEVCPROFILE_HIGH_THROUGHPUT:
      return 2;
    default:
      return 0;
  }
}

uint8_t GetChromaFormatIdc(VideoCodecProfile profile) {
  switch (profile) {
    case HEVCPROFILE_REXT:
    case HEVCPROFILE_HIGH_THROUGHPUT:
      return 3;
    default:
      return 1;
  }
}

std::pair<int, int> GetConformanceWindowUnitSizes(uint8_t chroma_format_idc) {
  switch (chroma_format_idc) {
    case 0:
      return {1, 1};
    case 1:
      return {2, 2};
    case 2:
      return {2, 1};
    case 3:
      return {1, 1};
    default:
      NOTREACHED();
  }
}

scoped_refptr<H265Picture> GetH265Picture(
    const VaapiVideoEncoderDelegate::EncodeJob& job) {
  auto* codec_picture = job.picture().get();
  if (!codec_picture) {
    return nullptr;
  }

  constexpr size_t kChrome146VaapiH264PictureSurfacePtrOffset = 0x378;
  const auto* raw_picture = reinterpret_cast<const uint8_t*>(codec_picture);
  const uint8_t* surface_handle = nullptr;
  std::memcpy(&surface_handle,
              raw_picture + kChrome146VaapiH264PictureSurfacePtrOffset,
              sizeof(surface_handle));

  VASurfaceID borrowed_surface_id = VA_INVALID_ID;
  if (surface_handle) {
    std::memcpy(&borrowed_surface_id, surface_handle,
                sizeof(borrowed_surface_id));
  }

  VASurfaceID surface_id = job.input_surface_id();
  fprintf(stderr,
          "chrome_hevc_shim: Chrome H264 picture raw offset=0x%zx "
          "borrowed_surface=%u input_surface=%u picture=%p handle=%p\n",
          kChrome146VaapiH264PictureSurfacePtrOffset, borrowed_surface_id,
          surface_id, codec_picture, surface_handle);

  if (surface_id != VA_INVALID_ID) {
    auto borrowed = base::MakeRefCounted<H265Picture>();
    borrowed->pic_latency_count_ = surface_id;
    return borrowed;
  }

  return nullptr;
}

bool SubmitChromeVaBuffers(
    VaapiWrapper* wrapper,
    const std::vector<VaapiWrapper::VABufferDescriptor>& va_buffers) {
  if (!wrapper) {
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(wrapper);
  auto submit_buffer_locked =
      reinterpret_cast<bool (*)(VaapiWrapper*,
                                const VaapiWrapper::VABufferDescriptor&)>(
          vtable[0x98 / sizeof(void*)]);
  fprintf(stderr,
          "chrome_hevc_shim: SubmitChromeVaBuffers wrapper=%p "
          "SubmitBuffer_Locked=%p buffers=%zu\n",
          wrapper, reinterpret_cast<void*>(submit_buffer_locked),
          va_buffers.size());

  for (const auto& va_buffer : va_buffers) {
    fprintf(stderr,
            "chrome_hevc_shim: SubmitChromeVaBuffers type=%d size=%zu "
            "data=%p\n",
            va_buffer.type, va_buffer.size, va_buffer.data.get());
    if (!submit_buffer_locked(wrapper, va_buffer)) {
      fprintf(stderr,
              "chrome_hevc_shim: SubmitChromeVaBuffers failed type=%d\n",
              va_buffer.type);
      return false;
    }
  }
  return true;
}

}  // namespace

H265VaapiVideoEncoderDelegate::H265VaapiVideoEncoderDelegate(
    scoped_refptr<VaapiWrapper> vaapi_wrapper,
    base::RepeatingClosure error_cb)
    : VaapiVideoEncoderDelegate(std::move(vaapi_wrapper), error_cb) {}

H265VaapiVideoEncoderDelegate::~H265VaapiVideoEncoderDelegate() = default;

bool H265VaapiVideoEncoderDelegate::Initialize(
    const VideoEncodeAccelerator::Config& config,
    const VaapiVideoEncoderDelegate::Config& ave_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  profile_ = config.output_profile;
  if (!ToGeneralProfileIdc(profile_).has_value()) {
    NOTIMPLEMENTED() << "Unsupported H265 profile "
                     << GetProfileName(config.output_profile);
    return false;
  }

  if (config.input_visible_size.IsEmpty()) {
    DVLOGF(1) << "Input visible size could not be empty";
    return false;
  }

  if (config.HasSpatialLayer()) {
    DVLOGF(1) << "Spatial layer encoding is not supported for H265";
    return false;
  }

  if (GetChromaFormatIdc(profile_) == 1 &&
      ((config.input_visible_size.width() % 2 != 0) ||
       (config.input_visible_size.height() % 2 != 0))) {
    DVLOGF(1) << "Input visible size should be even for 4:2:0";
    return false;
  }

  visible_size_ = config.input_visible_size;
  use_intel_hevc_tuning_ = IsIntelVaapiImplementation();
  content_type_ = config.content_type;
  level_idc_ = EnvUintInRangeOrDefault(
      "CHROME_HEVC_SHIM_LEVEL",
      use_intel_hevc_tuning_ ? kIntelDefaultHevcLevelIdc
                             : kLegacyDefaultHevcLevelIdc,
      255);
  const int surface_alignment =
      use_intel_hevc_tuning_ ? kIntelSurfaceAlignmentInPixels
                             : kLegacyMinCodingBlockSizeInPixels;
  coded_size_ = gfx::Size(
      base::bits::AlignUpDeprecatedDoNotUse(visible_size_.width(),
                                            surface_alignment),
      base::bits::AlignUpDeprecatedDoNotUse(visible_size_.height(),
                                            surface_alignment));

  if (ave_config.max_num_ref_frames == 0) {
    DVLOGF(1) << "Invalid max reference frame setting from VEA";
    return false;
  }

  initial_qp_ =
      use_intel_hevc_tuning_
          ? (content_type_ ==
                     VideoEncodeAccelerator::Config::ContentType::kDisplay
                 ? kIntelDisplayInitialQp
                 : kIntelDefaultInitialQp)
          : kLegacyDefaultInitialQp;
  frame_num_ = 0;
  last_ref_pic_.reset();
  submit_packed_headers_ = false;
  packed_vps_.reset();
  packed_sps_.reset();
  packed_pps_.reset();
  packed_header_sps_.reset();
  packed_header_pps_.reset();
  packed_sequence_data_.clear();
  bitrate_allocation_ = VideoBitrateAllocation(config.bitrate.mode());
  uses_constant_quantization_parameter_ =
      ave_config.uses_constant_quantization_parameter;
  DVLOGF(1) << "HEVC encoder init uses_cqp="
            << uses_constant_quantization_parameter_
            << " intel_tuning=" << use_intel_hevc_tuning_
            << " initial_qp=" << static_cast<int>(initial_qp_)
            << " content_type=" << static_cast<int>(content_type_);

  if (SupportsPackedHeaders(profile_)) {
    bool submit_packed_sequence = false;
    bool submit_packed_picture = false;
    bool submit_packed_slice = false;
    bool support_packed_raw = false;
    if (!vaapi_wrapper_->GetSupportedPackedHeaders(
            config.output_profile, submit_packed_sequence,
            submit_packed_picture, submit_packed_slice, support_packed_raw)) {
      DVLOGF(2) << "Failed getting supported packed headers for H265";
    } else {
      submit_packed_headers_ =
          use_intel_hevc_tuning_
              ? (submit_packed_sequence && submit_packed_slice)
              : (submit_packed_sequence && submit_packed_picture);
    }
    if (submit_packed_headers_ &&
        SuppressPackedHeadersForVisibleSizeShmemMain10(config, visible_size_,
                                                       coded_size_,
                                                       use_intel_hevc_tuning_)) {
      // Main10 SHMEM keeps the client/input surface at visible size. Let the
      // backend emit SPS/PPS that match that effective encode size instead of
      // prepending packed headers built from the aligned coded size.
      submit_packed_headers_ = false;
    }
    if (submit_packed_headers_) {
      if (use_intel_hevc_tuning_) {
        packed_vps_.emplace(true);
        packed_sps_.emplace(true);
        packed_pps_.emplace(true);
      } else {
        packed_vps_.emplace();
        packed_sps_.emplace();
        packed_pps_.emplace();
      }
      if (!UpdatePackedHeaders()) {
        return false;
      }
    } else if (visible_size_ != coded_size_) {
      DVLOGF(1) << "Packed HEVC headers unsupported; conformance window won't "
                   "be signaled for "
                << visible_size_.ToString() << " -> "
                << coded_size_.ToString();
    }
  }

  return UpdateRates(AllocateBitrateForDefaultEncoding(config), config.framerate);
}

bool H265VaapiVideoEncoderDelegate::UpdateRates(
    const VideoBitrateAllocation& bitrate_allocation,
    uint32_t framerate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (bitrate_allocation.GetMode() != bitrate_allocation_.GetMode()) {
    DVLOGF(1) << "Unexpected bitrate mode update for H265";
    return false;
  }

  if (bitrate_allocation.GetSumBps() == 0 || framerate == 0) {
    return false;
  }

  bitrate_allocation_ = bitrate_allocation;
  framerate_ = framerate;

  base::CheckedNumeric<uint64_t> checked_cpb_size(bitrate_allocation.GetSumBps());
  checked_cpb_size *= kCPBWindowSizeMs;
  checked_cpb_size /= 1000;
  if (!checked_cpb_size.AssignIfValid(&cpb_size_bits_)) {
    DVLOGF(1) << "Invalid CPB size";
    return false;
  }

  if (use_intel_hevc_tuning_ && submit_packed_headers_ &&
      !UpdatePackedHeaders()) {
    DVLOGF(1) << "Failed updating packed H265 headers after rate change";
    return false;
  }

  return true;
}

void H265VaapiVideoEncoderDelegate::BuildParameterSets(H265VPS& vps,
                                                       H265SPS& sps,
                                                       H265PPS& pps) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::optional<uint8_t> profile_idc = ToGeneralProfileIdc(profile_);
  CHECK(profile_idc.has_value());

  const uint8_t bit_depth_minus8 = GetBitDepthMinus8(profile_);
  const uint8_t chroma_format_idc = GetChromaFormatIdc(profile_);
  const auto [crop_unit_x, crop_unit_y] =
      GetConformanceWindowUnitSizes(chroma_format_idc);
  const int width_padding = coded_size_.width() - visible_size_.width();
  const int height_padding = coded_size_.height() - visible_size_.height();
  DCHECK_GT(crop_unit_x, 0);
  DCHECK_GT(crop_unit_y, 0);

  if (!use_intel_hevc_tuning_) {
    vps.vps_video_parameter_set_id = 0;
    vps.vps_base_layer_internal_flag = true;
    vps.vps_base_layer_available_flag = true;
    vps.vps_temporal_id_nesting_flag = true;
    vps.profile_tier_level.general_profile_idc = *profile_idc;
    vps.profile_tier_level.general_profile_compatibility_flags =
        1u << (31 - *profile_idc);
    vps.profile_tier_level.general_progressive_source_flag = true;
    vps.profile_tier_level.general_non_packed_constraint_flag = true;
    vps.profile_tier_level.general_frame_only_constraint_flag = true;
    vps.profile_tier_level.general_level_idc = kLegacyDefaultHevcLevelIdc;
    vps.vps_max_dec_pic_buffering_minus1[0] =
        base::checked_cast<int>(GetMaxNumOfRefFrames());
    vps.vps_max_latency_increase_plus1[0] = 1;

    sps.sps_video_parameter_set_id = vps.vps_video_parameter_set_id;
    sps.sps_max_sub_layers_minus1 = vps.vps_max_sub_layers_minus1;
    sps.sps_temporal_id_nesting_flag = vps.vps_temporal_id_nesting_flag;
    sps.profile_tier_level = vps.profile_tier_level;
    sps.sps_seq_parameter_set_id = 0;
    sps.chroma_format_idc = chroma_format_idc;
    sps.pic_width_in_luma_samples = coded_size_.width();
    sps.pic_height_in_luma_samples = coded_size_.height();
    DCHECK_EQ(width_padding % crop_unit_x, 0);
    DCHECK_EQ(height_padding % crop_unit_y, 0);
    sps.conf_win_right_offset = width_padding / crop_unit_x;
    sps.conf_win_bottom_offset = height_padding / crop_unit_y;
    sps.bit_depth_luma_minus8 = bit_depth_minus8;
    sps.bit_depth_chroma_minus8 = bit_depth_minus8;
    sps.log2_max_pic_order_cnt_lsb_minus4 =
        base::bits::Log2Ceiling(kLegacyIDRPeriod * 2) - 4;
    sps.sps_max_dec_pic_buffering_minus1 = vps.vps_max_dec_pic_buffering_minus1;
    sps.sps_max_num_reorder_pics = vps.vps_max_num_reorder_pics;
    std::ranges::copy(vps.vps_max_latency_increase_plus1,
                      sps.sps_max_latency_increase_plus1.begin());
    sps.log2_min_luma_coding_block_size_minus3 = 0;
    sps.log2_diff_max_min_luma_coding_block_size = 3;
    sps.log2_min_luma_transform_block_size_minus2 = 0;
    sps.log2_diff_max_min_luma_transform_block_size = 3;
    sps.max_transform_hierarchy_depth_inter = 0;
    sps.max_transform_hierarchy_depth_intra = 0;
    sps.sample_adaptive_offset_enabled_flag = true;
    sps.strong_intra_smoothing_enabled_flag = true;

    pps.pps_pic_parameter_set_id = 0;
    pps.pps_seq_parameter_set_id = sps.sps_seq_parameter_set_id;
    pps.cabac_init_present_flag = true;
    pps.num_ref_idx_l0_default_active_minus1 = 0;
    pps.num_ref_idx_l1_default_active_minus1 = 0;
    pps.init_qp_minus26 = base::checked_cast<int>(initial_qp_) - 26;
    pps.cu_qp_delta_enabled_flag = true;
    pps.pps_slice_chroma_qp_offsets_present_flag = true;
    pps.pps_loop_filter_across_slices_enabled_flag = true;
    pps.deblocking_filter_control_present_flag = true;
    return;
  }

  vps.vps_video_parameter_set_id = 0;
  vps.vps_base_layer_internal_flag = true;
  vps.vps_base_layer_available_flag = true;
  vps.vps_temporal_id_nesting_flag = true;
  vps.profile_tier_level.general_profile_idc = *profile_idc;
  vps.profile_tier_level.general_profile_compatibility_flags =
      MakeGeneralProfileCompatibilityFlags(*profile_idc);
  vps.profile_tier_level.general_progressive_source_flag = true;
  vps.profile_tier_level.general_non_packed_constraint_flag = true;
  vps.profile_tier_level.general_frame_only_constraint_flag = true;
  vps.profile_tier_level.general_level_idc = level_idc_;
  // Keep these syntax-element values aligned with FFmpeg's working HEVC VAAPI
  // stream layout. In particular, allow buffering for both the current picture
  // and one reference picture, instead of advertising a one-picture DPB.
  vps.vps_max_dec_pic_buffering_minus1[0] =
      base::checked_cast<int>(GetMaxNumOfRefFrames());
  vps.vps_max_latency_increase_plus1[0] = 0;

  sps.sps_video_parameter_set_id = vps.vps_video_parameter_set_id;
  sps.sps_max_sub_layers_minus1 = vps.vps_max_sub_layers_minus1;
  sps.sps_temporal_id_nesting_flag = vps.vps_temporal_id_nesting_flag;
  sps.profile_tier_level = vps.profile_tier_level;
  sps.sps_seq_parameter_set_id = 0;
  sps.chroma_format_idc = chroma_format_idc;
  sps.chroma_array_type =
      sps.separate_colour_plane_flag ? 0 : sps.chroma_format_idc;
  sps.pic_width_in_luma_samples = coded_size_.width();
  sps.pic_height_in_luma_samples = coded_size_.height();
  DCHECK_EQ(width_padding % crop_unit_x, 0);
  DCHECK_EQ(height_padding % crop_unit_y, 0);
  sps.conf_win_right_offset = width_padding / crop_unit_x;
  sps.conf_win_bottom_offset = height_padding / crop_unit_y;
  sps.bit_depth_luma_minus8 = bit_depth_minus8;
  sps.bit_depth_chroma_minus8 = bit_depth_minus8;
  sps.log2_max_pic_order_cnt_lsb_minus4 =
      kDefaultLog2MaxPicOrderCntLsbMinus4;
  sps.sps_max_dec_pic_buffering_minus1 = vps.vps_max_dec_pic_buffering_minus1;
  sps.sps_max_num_reorder_pics = vps.vps_max_num_reorder_pics;
  std::ranges::copy(vps.vps_max_latency_increase_plus1,
                    sps.sps_max_latency_increase_plus1.begin());
  sps.log2_min_luma_coding_block_size_minus3 = 0;
  sps.log2_diff_max_min_luma_coding_block_size = 2;
  sps.log2_min_luma_transform_block_size_minus2 = 0;
  sps.log2_diff_max_min_luma_transform_block_size = 3;
  sps.max_transform_hierarchy_depth_inter = 3;
  sps.max_transform_hierarchy_depth_intra = 3;
  sps.amp_enabled_flag = true;
  sps.sample_adaptive_offset_enabled_flag = false;
  sps.strong_intra_smoothing_enabled_flag = false;
  sps.vui_parameters_present_flag = true;
  // Don't hardcode HEVC colour-description fields here. FFmpeg only emits
  // explicit colour signalling when the source actually specifies it, and the
  // common Chromium H.264 path also avoids forcing BT.709 metadata. Keeping
  // these fields unspecified prevents the encoder from tinting the entire
  // picture via incorrect VUI signalling.
  sps.vui_parameters.video_full_range_flag = false;
  sps.vui_parameters.colour_description_present_flag = false;
  sps.vui_parameters.colour_primaries = 0;
  sps.vui_parameters.transfer_characteristics = 0;
  sps.vui_parameters.matrix_coeffs = 0;
  sps.vui_parameters.bitstream_restriction_flag = true;
  sps.vui_parameters.min_spatial_segmentation_idc = 0;
  sps.vui_parameters.max_bytes_per_pic_denom = 0;
  sps.vui_parameters.max_bits_per_min_cu_denom = 0;
  sps.vui_parameters.log2_max_mv_length_horizontal = 15;
  sps.vui_parameters.log2_max_mv_length_vertical = 15;

  pps.pps_pic_parameter_set_id = 0;
  pps.pps_seq_parameter_set_id = sps.sps_seq_parameter_set_id;
  pps.num_ref_idx_l0_default_active_minus1 = 0;
  pps.num_ref_idx_l1_default_active_minus1 = 0;
  pps.init_qp_minus26 = base::checked_cast<int>(initial_qp_) - 26;
  // The dumped HEVC Annex-B stream trips software decoders with
  // `cu_qp_delta -89 is outside the valid range [-26, 25]`, which means the
  // current PPS/QP-delta signalling does not match the emitted slice payload.
  // Keep per-CU QP deltas disabled until the VAAPI/iHD path can prove a valid
  // bitstream with them enabled.
  pps.cu_qp_delta_enabled_flag = false;
  pps.diff_cu_qp_delta_depth = 0;
  pps.pps_loop_filter_across_slices_enabled_flag = true;
}

size_t H265VaapiVideoEncoderDelegate::GetIntraPeriod() const {
  if (!use_intel_hevc_tuning_) {
    return kLegacyIDRPeriod;
  }
  return content_type_ == VideoEncodeAccelerator::Config::ContentType::kDisplay
             ? kIntelDisplayIntraPeriod
             : kIntelDefaultIntraPeriod;
}

bool H265VaapiVideoEncoderDelegate::UpdatePackedHeaders() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(submit_packed_headers_);
  DCHECK(packed_vps_);
  DCHECK(packed_sps_);
  DCHECK(packed_pps_);

  H265VPS vps;
  H265SPS sps;
  H265PPS pps;
  BuildParameterSets(vps, sps, pps);

  packed_vps_->Reset();
  packed_sps_->Reset();
  packed_pps_->Reset();
  BuildPackedH265VPS(*packed_vps_, vps);
  BuildPackedH265SPS(*packed_sps_, sps);
  BuildPackedH265PPS(*packed_pps_, pps);
  if (!use_intel_hevc_tuning_) {
    return true;
  }
  packed_sequence_data_.clear();
  packed_sequence_data_.reserve(packed_vps_->BytesInBuffer() +
                                packed_sps_->BytesInBuffer() +
                                packed_pps_->BytesInBuffer());
  packed_sequence_data_.insert(packed_sequence_data_.end(),
                               packed_vps_->data().begin(),
                               packed_vps_->data().end());
  packed_sequence_data_.insert(packed_sequence_data_.end(),
                               packed_sps_->data().begin(),
                               packed_sps_->data().end());
  packed_sequence_data_.insert(packed_sequence_data_.end(),
                               packed_pps_->data().begin(),
                               packed_pps_->data().end());
  packed_header_sps_.emplace(std::move(sps));
  packed_header_pps_.emplace(std::move(pps));
  return true;
}

gfx::Size H265VaapiVideoEncoderDelegate::GetCodedSize() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return coded_size_;
}

size_t H265VaapiVideoEncoderDelegate::GetMaxNumOfRefFrames() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return 1;
}

std::vector<gfx::Size> H265VaapiVideoEncoderDelegate::GetSVCLayerResolutions() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return {visible_size_};
}

BitstreamBufferMetadata H265VaapiVideoEncoderDelegate::GetMetadata(
    const EncodeJob& encode_job,
    size_t payload_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!encode_job.IsFrameDropped());
  CHECK_NE(payload_size, 0u);
  fprintf(stderr,
          "chrome_hevc_shim: GetMetadata payload=%zu key=%d timestamp=%lld\n",
          payload_size, encode_job.IsKeyframeRequested(),
          static_cast<long long>(encode_job.timestamp().InMicroseconds()));
  return BitstreamBufferMetadata(
      payload_size, encode_job.IsKeyframeRequested(), encode_job.timestamp());
}

void H265VaapiVideoEncoderDelegate::BitrateControlUpdate(
    const BitstreamBufferMetadata&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  fprintf(stderr, "chrome_hevc_shim: BitrateControlUpdate\n");
}

VaapiVideoEncoderDelegate::PrepareEncodeJobResult
H265VaapiVideoEncoderDelegate::PrepareEncodeJob(EncodeJob& encode_job) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  fprintf(stderr,
          "chrome_hevc_shim: PrepareEncodeJob enter frame_num=%zu key=%d "
          "surface=%u coded_buffer=%u\n",
          frame_num_, encode_job.IsKeyframeRequested(),
          encode_job.input_surface_id(), encode_job.coded_buffer_id());
  scoped_refptr<H265Picture> pic = GetH265Picture(encode_job);
  if (!pic) {
    fprintf(stderr, "chrome_hevc_shim: PrepareEncodeJob invalid picture\n");
    DVLOGF(1) << "Invalid H265 picture";
    return PrepareEncodeJobResult::kFail;
  }

  if (!use_intel_hevc_tuning_) {
    const bool idr = encode_job.IsKeyframeRequested() || frame_num_ == 0 ||
                     ((frame_num_ % kLegacyIDRPeriod) == 0);
    if (idr) {
      encode_job.ProduceKeyframe();
    }

    if (!SubmitFrameParameters(encode_job, pic, idr)) {
      fprintf(stderr, "chrome_hevc_shim: PrepareEncodeJob legacy submit failed\n");
      DVLOGF(1) << "Failed submitting H265 frame parameters";
      return PrepareEncodeJobResult::kFail;
    }

    ++frame_num_;
    frame_num_ %= kLegacyIDRPeriod;
    fprintf(stderr,
            "chrome_hevc_shim: PrepareEncodeJob legacy ok next_frame_num=%zu\n",
            frame_num_);
    return PrepareEncodeJobResult::kSuccess;
  }

  const size_t intra_period = GetIntraPeriod();
  const bool force_all_keyframes =
      EnvFlagEnabledOrDefault("CHROME_HEVC_SHIM_FORCE_ALL_KEYFRAMES", true);
  const bool keyframe_requested =
      force_all_keyframes || encode_job.IsKeyframeRequested();
  bool idr =
      keyframe_requested || frame_num_ == 0 || ((frame_num_ % intra_period) == 0);
  if (!idr && !last_ref_pic_) {
    DVLOGF(2) << "Promoting HEVC frame to IDR because no reference picture "
                 "is available";
    idr = true;
  }
  if (idr) {
    if (force_all_keyframes && !encode_job.IsKeyframeRequested()) {
      fprintf(stderr,
              "chrome_hevc_shim: forcing IDR for Chrome hotpatch surface-pool "
              "compatibility\n");
    }
    last_ref_pic_.reset();
    // Keep HEVC POC numbering relative to the most recent IDR, matching
    // FFmpeg's VAAPI path. A forced keyframe in the middle of a GOP must start
    // a fresh POC sequence at 0 rather than emitting an IDR with a carried-over
    // POC such as 4, 5, ...
    frame_num_ = 0;
    encode_job.ProduceKeyframe();
  }

  if (!SubmitFrameParameters(encode_job, pic, idr)) {
    fprintf(stderr, "chrome_hevc_shim: PrepareEncodeJob submit failed\n");
    DVLOGF(1) << "Failed submitting H265 frame parameters";
    return PrepareEncodeJobResult::kFail;
  }

  pic->ref_ = H265Picture::kShortTermCurrBefore;
  last_ref_pic_ = std::move(pic);

  ++frame_num_;
  frame_num_ %= intra_period;
  fprintf(stderr, "chrome_hevc_shim: PrepareEncodeJob ok next_frame_num=%zu\n",
          frame_num_);
  return PrepareEncodeJobResult::kSuccess;
}

bool H265VaapiVideoEncoderDelegate::SubmitFrameParameters(
    EncodeJob& encode_job,
    const scoped_refptr<H265Picture>& pic,
    bool idr) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(pic);

  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters enter idr=%d surface=%u "
          "coded_buffer=%u\n",
          idr, GetH265SurfaceId(pic), encode_job.coded_buffer_id());
  const std::optional<uint8_t> profile_idc = ToGeneralProfileIdc(profile_);
  if (!profile_idc.has_value()) {
    fprintf(stderr, "chrome_hevc_shim: SubmitFrameParameters invalid profile\n");
    DVLOGF(1) << "Invalid H265 profile";
    return false;
  }
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters profile_idc=%u "
          "profile=%d level=%u coded=%dx%d\n",
          *profile_idc, profile_, level_idc_, coded_size_.width(),
          coded_size_.height());
  if (!use_intel_hevc_tuning_) {
    const uint8_t bit_depth_minus8 = GetBitDepthMinus8(profile_);
    const uint8_t chroma_format_idc = GetChromaFormatIdc(profile_);

    pic->nal_unit_type_ = idr ? H265NALU::IDR_W_RADL : H265NALU::TRAIL_R;
    pic->pic_order_cnt_val_ = base::checked_cast<int>(frame_num_);

    VAEncSequenceParameterBufferHEVC seq_param = {};
    seq_param.general_profile_idc = *profile_idc;
    seq_param.general_level_idc = kLegacyDefaultHevcLevelIdc;
    seq_param.general_tier_flag = 0;
    seq_param.intra_period = kLegacyIDRPeriod;
    seq_param.intra_idr_period = kLegacyIDRPeriod;
    seq_param.ip_period = kDefaultIPPeriod;
    seq_param.bits_per_second = bitrate_allocation_.GetSumBps();
    seq_param.pic_width_in_luma_samples = coded_size_.width();
    seq_param.pic_height_in_luma_samples = coded_size_.height();
    seq_param.seq_fields.bits.chroma_format_idc = chroma_format_idc;
    seq_param.seq_fields.bits.bit_depth_luma_minus8 = bit_depth_minus8;
    seq_param.seq_fields.bits.bit_depth_chroma_minus8 = bit_depth_minus8;
    seq_param.seq_fields.bits.strong_intra_smoothing_enabled_flag = 1;
    seq_param.seq_fields.bits.sample_adaptive_offset_enabled_flag = 1;
    seq_param.seq_fields.bits.low_delay_seq = 1;
    seq_param.log2_min_luma_coding_block_size_minus3 = 0;
    seq_param.log2_diff_max_min_luma_coding_block_size = 3;
    seq_param.log2_min_transform_block_size_minus2 = 0;
    seq_param.log2_diff_max_min_transform_block_size = 3;
    seq_param.max_transform_hierarchy_depth_inter = 0;
    seq_param.max_transform_hierarchy_depth_intra = 0;
    seq_param.pcm_sample_bit_depth_luma_minus1 = 8 + bit_depth_minus8 - 1;
    seq_param.pcm_sample_bit_depth_chroma_minus1 = 8 + bit_depth_minus8 - 1;
    seq_param.log2_min_pcm_luma_coding_block_size_minus3 = 0;
    seq_param.log2_max_pcm_luma_coding_block_size_minus3 = 0;
    seq_param.vui_parameters_present_flag = 1;
    seq_param.vui_fields.bits.vui_timing_info_present_flag = 1;
    seq_param.vui_num_units_in_tick = 1;
    seq_param.vui_time_scale = framerate_;

    VAEncPictureParameterBufferHEVC pic_param = {};
    pic_param.decoded_curr_pic.picture_id = GetH265SurfaceId(pic);
    pic_param.decoded_curr_pic.pic_order_cnt = pic->pic_order_cnt_val_;
    pic_param.coded_buf = encode_job.coded_buffer_id();
    pic_param.collocated_ref_pic_index = 0xFF;
    pic_param.pic_init_qp = initial_qp_;
    pic_param.log2_parallel_merge_level_minus2 = 0;
    pic_param.num_ref_idx_l0_default_active_minus1 = 0;
    pic_param.num_ref_idx_l1_default_active_minus1 = 0;
    pic_param.slice_pic_parameter_set_id = 0;
    pic_param.nal_unit_type = base::checked_cast<uint8_t>(pic->nal_unit_type_);
    pic_param.pic_fields.bits.idr_pic_flag = idr ? 1 : 0;
    pic_param.pic_fields.bits.coding_type = 1;
    pic_param.pic_fields.bits.reference_pic_flag = 0;
    pic_param.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag = 1;
    for (VAPictureHEVC& ref_pic : pic_param.reference_frames) {
      InitVAPictureHEVC(&ref_pic);
    }

    VAEncSliceParameterBufferHEVC slice_param = {};
    slice_param.slice_segment_address = 0;
    const uint32_t ctu_cols =
        base::bits::AlignUp(static_cast<uint32_t>(coded_size_.width()),
                            static_cast<uint32_t>(kLegacyCTUSizeInPixels)) /
        kLegacyCTUSizeInPixels;
    const uint32_t ctu_rows =
        base::bits::AlignUp(static_cast<uint32_t>(coded_size_.height()),
                            static_cast<uint32_t>(kLegacyCTUSizeInPixels)) /
        kLegacyCTUSizeInPixels;
    slice_param.num_ctu_in_slice = std::max(1u, ctu_cols * ctu_rows);
    slice_param.slice_type = H265SliceHeader::kSliceTypeI;
    slice_param.slice_pic_parameter_set_id = 0;
    slice_param.num_ref_idx_l0_active_minus1 = 0;
    slice_param.num_ref_idx_l1_active_minus1 = 0;
    slice_param.max_num_merge_cand = 5;
    slice_param.slice_qp_delta =
        base::checked_cast<int8_t>(static_cast<int>(initial_qp_) - 26);
    slice_param.slice_fields.bits.last_slice_of_pic_flag = 1;
    slice_param.slice_fields.bits.slice_sao_luma_flag = 1;
    slice_param.slice_fields.bits.slice_sao_chroma_flag = 1;
    slice_param.slice_fields.bits.slice_loop_filter_across_slices_enabled_flag =
        1;
    for (VAPictureHEVC& ref_pic : slice_param.ref_pic_list0) {
      InitVAPictureHEVC(&ref_pic);
    }
    for (VAPictureHEVC& ref_pic : slice_param.ref_pic_list1) {
      InitVAPictureHEVC(&ref_pic);
    }

    std::vector<VaapiWrapper::VABufferDescriptor> va_buffers = {
        {VAEncSequenceParameterBufferType, sizeof(seq_param), &seq_param},
        {VAEncPictureParameterBufferType, sizeof(pic_param), &pic_param},
        {VAEncSliceParameterBufferType, sizeof(slice_param), &slice_param},
    };

    std::array<VAEncPackedHeaderParameterBuffer, 3> packed_header_params = {};
    size_t num_packed_headers = 0;
    auto append_packed_header = [&](uint32_t type,
                                    const H26xAnnexBBitstreamBuilder& builder) {
      CHECK_LT(num_packed_headers, packed_header_params.size());
      auto& packed_header_param = packed_header_params[num_packed_headers++];
      packed_header_param.type = type;
      packed_header_param.bit_length = builder.BytesInBuffer() * CHAR_BIT;
      packed_header_param.has_emulation_bytes = 0;
      va_buffers.push_back({VAEncPackedHeaderParameterBufferType,
                            sizeof(packed_header_param), &packed_header_param});
      va_buffers.push_back({VAEncPackedHeaderDataBufferType,
                            builder.BytesInBuffer(), builder.data().data()});
    };

    if (idr && submit_packed_headers_) {
      DCHECK(packed_vps_);
      DCHECK(packed_sps_);
      DCHECK(packed_pps_);
      append_packed_header(VAEncPackedHeaderSequence, *packed_vps_);
      append_packed_header(VAEncPackedHeaderSequence, *packed_sps_);
      append_packed_header(VAEncPackedHeaderPicture, *packed_pps_);
    }

    std::vector<uint8_t> frame_rate_misc_buffer;
    auto& frame_rate_param =
        AllocateMiscParameterBuffer<VAEncMiscParameterFrameRate>(
            frame_rate_misc_buffer, VAEncMiscParameterTypeFrameRate);
    frame_rate_param.framerate = framerate_;
    va_buffers.push_back({VAEncMiscParameterBufferType,
                          frame_rate_misc_buffer.size(),
                          frame_rate_misc_buffer.data()});

    fprintf(stderr,
            "chrome_hevc_shim: SubmitFrameParameters legacy buffers=%zu "
            "surface=%u ctu=%u\n",
            va_buffers.size(), pic_param.decoded_curr_pic.picture_id,
            slice_param.num_ctu_in_slice);
    return SubmitChromeVaBuffers(vaapi_wrapper_.get(), va_buffers);
  }

  const size_t intra_period = GetIntraPeriod();
  const Bitrate bitrate = bitrate_allocation_.GetSumBitrate();
  uint32_t bitrate_bps = bitrate.target_bps();
  uint32_t target_percentage = 100u;
  if (bitrate.mode() == Bitrate::Mode::kVariable) {
    bitrate_bps = bitrate.peak_bps();
    DCHECK_NE(bitrate.peak_bps(), 0u);
    base::CheckedNumeric<uint64_t> checked_percentage =
        base::CheckDiv(base::CheckMul<uint64_t>(bitrate.target_bps(), 100u),
                       bitrate.peak_bps());
    if (!checked_percentage.AssignIfValid(&target_percentage)) {
      DVLOGF(1)
          << "Integer overflow while computing target percentage for bitrate.";
      return false;
    }
  }
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters bitrate target=%u "
          "target_percentage=%u intra_period=%zu cqp=%d packed=%d\n",
          bitrate_bps, target_percentage, intra_period,
          uses_constant_quantization_parameter_, submit_packed_headers_);
  const uint8_t bit_depth_minus8 = GetBitDepthMinus8(profile_);
  const uint8_t chroma_format_idc = GetChromaFormatIdc(profile_);

  pic->nal_unit_type_ = idr ? H265NALU::IDR_W_RADL : H265NALU::TRAIL_R;
  pic->pic_order_cnt_val_ = base::checked_cast<int>(frame_num_);

  VAEncSequenceParameterBufferHEVC seq_param = {};
  seq_param.general_profile_idc = *profile_idc;
  seq_param.general_level_idc = level_idc_;
  seq_param.general_tier_flag = 0;
  seq_param.intra_period = intra_period;
  seq_param.intra_idr_period = intra_period;
  seq_param.ip_period = kDefaultIPPeriod;
  seq_param.bits_per_second = bitrate_bps;
  seq_param.pic_width_in_luma_samples = coded_size_.width();
  seq_param.pic_height_in_luma_samples = coded_size_.height();
  seq_param.seq_fields.bits.chroma_format_idc = chroma_format_idc;
  seq_param.seq_fields.bits.bit_depth_luma_minus8 = bit_depth_minus8;
  seq_param.seq_fields.bits.bit_depth_chroma_minus8 = bit_depth_minus8;
  seq_param.seq_fields.bits.strong_intra_smoothing_enabled_flag = 0;
  seq_param.seq_fields.bits.amp_enabled_flag = 1;
  seq_param.seq_fields.bits.sample_adaptive_offset_enabled_flag = 0;
  seq_param.seq_fields.bits.low_delay_seq = 0;
  seq_param.log2_min_luma_coding_block_size_minus3 = 0;
  seq_param.log2_diff_max_min_luma_coding_block_size = 2;
  seq_param.log2_min_transform_block_size_minus2 = 0;
  seq_param.log2_diff_max_min_transform_block_size = 3;
  seq_param.max_transform_hierarchy_depth_inter = 3;
  seq_param.max_transform_hierarchy_depth_intra = 3;
  seq_param.pcm_sample_bit_depth_luma_minus1 = 0;
  seq_param.pcm_sample_bit_depth_chroma_minus1 = 0;
  seq_param.log2_min_pcm_luma_coding_block_size_minus3 = 0;
  seq_param.log2_max_pcm_luma_coding_block_size_minus3 = 0;
  seq_param.vui_parameters_present_flag = 0;

  VAEncPictureParameterBufferHEVC pic_param = {};
  pic_param.decoded_curr_pic.picture_id = GetH265SurfaceId(pic);
  pic_param.decoded_curr_pic.pic_order_cnt = pic->pic_order_cnt_val_;
  pic_param.coded_buf = encode_job.coded_buffer_id();
  pic_param.collocated_ref_pic_index = 0xFF;
  pic_param.pic_init_qp = initial_qp_;
  pic_param.diff_cu_qp_delta_depth = 0;
  pic_param.pps_cb_qp_offset = 0;
  pic_param.pps_cr_qp_offset = 0;
  pic_param.log2_parallel_merge_level_minus2 = 0;
  pic_param.num_ref_idx_l0_default_active_minus1 = 0;
  pic_param.num_ref_idx_l1_default_active_minus1 = 0;
  pic_param.slice_pic_parameter_set_id = 0;
  pic_param.nal_unit_type = base::checked_cast<uint8_t>(pic->nal_unit_type_);
  pic_param.pic_fields.bits.idr_pic_flag = idr ? 1 : 0;
  pic_param.pic_fields.bits.coding_type = idr ? 1 : 2;
  // This HEVC path currently emits only reference NALU types
  // (IDR_W_RADL / TRAIL_R), so the current picture must be marked as
  // reference for the driver/HAL.
  pic_param.pic_fields.bits.reference_pic_flag = 1;
  pic_param.pic_fields.bits.cu_qp_delta_enabled_flag = 0;
  pic_param.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag = 1;
  for (VAPictureHEVC& ref_pic : pic_param.reference_frames) {
    InitVAPictureHEVC(&ref_pic);
  }
  if (!idr) {
    if (!last_ref_pic_) {
      DVLOGF(1) << "Missing HEVC reference picture for P-frame";
      return false;
    }
    FillVAPictureHEVC(&pic_param.reference_frames[0], last_ref_pic_,
                      H265Picture::kShortTermCurrBefore);
    DVLOGF(2) << "Using HEVC short-term reference poc="
              << last_ref_pic_->pic_order_cnt_val_ << " surface="
              << GetH265SurfaceId(last_ref_pic_);
  }
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters pic_param ready nal=%d "
          "poc=%d coded_buf=%u curr_surface=%u\n",
          pic->nal_unit_type_, pic->pic_order_cnt_val_, pic_param.coded_buf,
          pic_param.decoded_curr_pic.picture_id);

  VAEncSliceParameterBufferHEVC slice_param = {};
  slice_param.slice_segment_address = 0;
  const uint32_t ctu_cols =
      base::bits::AlignUp(static_cast<uint32_t>(coded_size_.width()),
                          static_cast<uint32_t>(kIntelCTUSizeInPixels)) /
      kIntelCTUSizeInPixels;
  const uint32_t ctu_rows =
      base::bits::AlignUp(static_cast<uint32_t>(coded_size_.height()),
                          static_cast<uint32_t>(kIntelCTUSizeInPixels)) /
      kIntelCTUSizeInPixels;
  slice_param.num_ctu_in_slice = std::max(1u, ctu_cols * ctu_rows);
  slice_param.slice_type =
      idr ? H265SliceHeader::kSliceTypeI : H265SliceHeader::kSliceTypeP;
  slice_param.slice_pic_parameter_set_id = 0;
  slice_param.num_ref_idx_l0_active_minus1 = 0;
  slice_param.num_ref_idx_l1_active_minus1 = 0;
  slice_param.max_num_merge_cand = 5;
  // FFmpeg and the HEVC syntax model treat slice_qp_delta as a delta from the
  // PPS init_qp_minus26 value. Keep the initial slice QP anchored to the PPS
  // init QP instead of double-applying the offset here.
  slice_param.slice_qp_delta = 0;
  slice_param.slice_fields.bits.last_slice_of_pic_flag = 1;
  slice_param.slice_fields.bits.slice_sao_luma_flag = 0;
  slice_param.slice_fields.bits.slice_sao_chroma_flag = 0;
  slice_param.slice_fields.bits.slice_loop_filter_across_slices_enabled_flag =
      0;
  for (VAPictureHEVC& ref_pic : slice_param.ref_pic_list0) {
    InitVAPictureHEVC(&ref_pic);
  }
  for (VAPictureHEVC& ref_pic : slice_param.ref_pic_list1) {
    InitVAPictureHEVC(&ref_pic);
  }
  if (!idr) {
    slice_param.ref_pic_list0[0] = pic_param.reference_frames[0];
  }
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters slice ready ctu=%u "
          "slice_type=%u\n",
          slice_param.num_ctu_in_slice, slice_param.slice_type);

  std::vector<VaapiWrapper::VABufferDescriptor> va_buffers = {
      {VAEncSequenceParameterBufferType, sizeof(seq_param), &seq_param},
  };
  fprintf(stderr, "chrome_hevc_shim: SubmitFrameParameters va_buffers init\n");

  std::array<VAEncPackedHeaderParameterBuffer, 2> packed_header_params = {};
  size_t num_packed_headers = 0;
  auto append_packed_header = [&](uint32_t type,
                                  const uint8_t* data,
                                  size_t size,
                                  size_t bit_length,
                                  uint8_t has_emulation_bytes) {
    CHECK_LT(num_packed_headers, packed_header_params.size());
    auto& packed_header_param = packed_header_params[num_packed_headers++];
    packed_header_param.type = type;
    packed_header_param.bit_length = bit_length;
    packed_header_param.has_emulation_bytes = has_emulation_bytes;
    va_buffers.push_back({VAEncPackedHeaderParameterBufferType,
                          sizeof(packed_header_param), &packed_header_param});
    va_buffers.push_back({VAEncPackedHeaderDataBufferType, size, data});
  };

  std::array<std::vector<uint8_t>, 3> misc_buffers;
  std::vector<uint8_t> frame_rate_misc_buffer;
  if (idr) {
    if (uses_constant_quantization_parameter_) {
      auto& frame_rate_param =
          AllocateMiscParameterBuffer<VAEncMiscParameterFrameRate>(
              frame_rate_misc_buffer, VAEncMiscParameterTypeFrameRate);
      frame_rate_param.framerate = (1u << 16) | framerate_;
      va_buffers.push_back({VAEncMiscParameterBufferType,
                            frame_rate_misc_buffer.size(),
                            frame_rate_misc_buffer.data()});
    } else {
      CreateVAEncRateControlParams(bitrate_bps, target_percentage,
                                   kCPBWindowSizeMs, framerate_, cpb_size_bits_,
                                   misc_buffers);
      va_buffers.push_back({VAEncMiscParameterBufferType, misc_buffers[0].size(),
                            misc_buffers[0].data()});
      va_buffers.push_back({VAEncMiscParameterBufferType, misc_buffers[1].size(),
                            misc_buffers[1].data()});
      va_buffers.push_back({VAEncMiscParameterBufferType, misc_buffers[2].size(),
                            misc_buffers[2].data()});
    }
  }
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters misc ready buffers=%zu\n",
          va_buffers.size());

  va_buffers.push_back(
      {VAEncPictureParameterBufferType, sizeof(pic_param), &pic_param});

  if (idr && submit_packed_headers_) {
    DCHECK(packed_vps_);
    DCHECK(packed_sps_);
    DCHECK(packed_pps_);
    append_packed_header(VAEncPackedHeaderSequence,
                         packed_sequence_data_.data(),
                         packed_sequence_data_.size(),
                         packed_sequence_data_.size() * CHAR_BIT,
                         /*has_emulation_bytes=*/1);
    fprintf(stderr,
            "chrome_hevc_shim: SubmitFrameParameters packed sequence bytes=%zu "
            "buffers=%zu\n",
            packed_sequence_data_.size(), va_buffers.size());
  }

  // Chromium 146 does not expose a packed HEVC slice-header builder. Keep the
  // self-contained build on upstream 146 by submitting the packed sequence
  // header and regular VA slice parameters only.

  va_buffers.push_back(
      {VAEncSliceParameterBufferType, sizeof(slice_param), &slice_param});
  fprintf(stderr,
          "chrome_hevc_shim: SubmitFrameParameters before SubmitBuffers "
          "buffers=%zu\n",
          va_buffers.size());

  bool ok = SubmitChromeVaBuffers(vaapi_wrapper_.get(), va_buffers);
  fprintf(stderr, "chrome_hevc_shim: SubmitFrameParameters SubmitBuffers=%d "
                  "buffer_count=%zu\n",
          ok, va_buffers.size());
  return ok;
}

}  // namespace media
