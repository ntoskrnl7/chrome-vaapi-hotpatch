// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_VAAPI_H265_VAAPI_VIDEO_ENCODER_DELEGATE_H_
#define MEDIA_GPU_VAAPI_H265_VAAPI_VIDEO_ENCODER_DELEGATE_H_

#include <optional>

#include "media/filters/h26x_annex_b_bitstream_builder.h"
#include "media/gpu/vaapi/vaapi_video_encoder_delegate.h"
#include "media/parsers/h265_parser.h"

namespace media {
class H265Picture;

class H265VaapiVideoEncoderDelegate : public VaapiVideoEncoderDelegate {
 public:
  H265VaapiVideoEncoderDelegate(scoped_refptr<VaapiWrapper> vaapi_wrapper,
                                base::RepeatingClosure error_cb);
  H265VaapiVideoEncoderDelegate(const H265VaapiVideoEncoderDelegate&) = delete;
  H265VaapiVideoEncoderDelegate& operator=(
      const H265VaapiVideoEncoderDelegate&) = delete;
  ~H265VaapiVideoEncoderDelegate() override;

  bool Initialize(const VideoEncodeAccelerator::Config& config,
                  const VaapiVideoEncoderDelegate::Config& ave_config) override;
  bool UpdateRates(const VideoBitrateAllocation& bitrate_allocation,
                   uint32_t framerate) override;
  gfx::Size GetCodedSize() const override;
  size_t GetMaxNumOfRefFrames() const override;
  std::vector<gfx::Size> GetSVCLayerResolutions() override;

 private:
  PrepareEncodeJobResult PrepareEncodeJob(EncodeJob& encode_job) override;
  BitstreamBufferMetadata GetMetadata(const EncodeJob& encode_job,
                                      size_t payload_size) override;
  void BitrateControlUpdate(
      const BitstreamBufferMetadata& metadata) override;

  void BuildParameterSets(H265VPS& vps, H265SPS& sps, H265PPS& pps) const;
  size_t GetIntraPeriod() const;
  bool UpdatePackedHeaders();
  bool SubmitFrameParameters(EncodeJob& encode_job,
                             const scoped_refptr<H265Picture>& pic,
                             bool idr);

  VideoCodecProfile profile_ = VIDEO_CODEC_PROFILE_UNKNOWN;
  std::optional<H26xAnnexBBitstreamBuilder> packed_vps_;
  std::optional<H26xAnnexBBitstreamBuilder> packed_sps_;
  std::optional<H26xAnnexBBitstreamBuilder> packed_pps_;
  std::optional<H265SPS> packed_header_sps_;
  std::optional<H265PPS> packed_header_pps_;
  std::vector<uint8_t> packed_sequence_data_;
  bool submit_packed_headers_ = false;
  bool use_intel_hevc_tuning_ = false;
  bool uses_constant_quantization_parameter_ = false;
  VideoEncodeAccelerator::Config::ContentType content_type_ =
      VideoEncodeAccelerator::Config::ContentType::kCamera;
  gfx::Size visible_size_;
  gfx::Size coded_size_;
  VideoBitrateAllocation bitrate_allocation_{Bitrate::Mode::kConstant};
  uint32_t framerate_ = 0;
  uint32_t cpb_size_bits_ = 0;
  uint32_t level_idc_ = 0;
  uint8_t initial_qp_ = 26;
  size_t frame_num_ = 0;
  scoped_refptr<H265Picture> last_ref_pic_;
};

}  // namespace media

#endif  // MEDIA_GPU_VAAPI_H265_VAAPI_VIDEO_ENCODER_DELEGATE_H_
