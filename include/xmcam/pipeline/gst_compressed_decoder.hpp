/*
 * @file gst_compressed_decoder.hpp
 * @brief Decoder for compressed USB capture formats, used by V4l2Source when
 *        the camera streams MJPEG or H.264 — the common high-framerate modes.
 *        One compressed buffer in, one I420 VideoFrame out (H.264 may need a
 *        few input buffers before the first output: SPS/PPS + first IDR).
 *
 *   MJPEG: appsrc(image/jpeg) ! jpegdec ! videoconvert ! I420 ! appsink
 *   H264:  appsrc(video/x-h264 byte-stream) ! h264parse ! avdec_h264 !
 *          videoconvert ! I420 ! appsink
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_GST_COMPRESSED_DECODER_HPP
#define XMCAM_PIPELINE_GST_COMPRESSED_DECODER_HPP

#include <cstddef>
#include <cstdint>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include "xmcam/core/result.hpp"
#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class GstCompressedDecoder {
 public:
  GstCompressedDecoder() = default;
  ~GstCompressedDecoder();

  static bool Supports(PixelFormat f) {
    return f == PixelFormat::kMjpeg || f == PixelFormat::kH264;
  }

  // Build + start the decode pipeline for `format` (kMjpeg or kH264).
  Status Open(PixelFormat format);
  void Close();

  // Feed one compressed capture buffer; on success `out` receives a decoded
  // I420 VideoFrame. Returns kTimeout when the decoder produced no frame yet —
  // EXPECTED for the first H.264 buffers before an IDR arrives; callers should
  // treat it as "skip this frame", not an error.
  Status Decode(const uint8_t* data, std::size_t len, int64_t pts_ns,
                uint64_t seq, VideoFrame* out);

  const char* name() const;  // "mjpeg->i420" / "h264->i420"

 private:
  PixelFormat format_ = PixelFormat::kUnknown;
  GstElement* pipeline_ = nullptr;
  GstAppSrc* src_ = nullptr;
  GstAppSink* sink_ = nullptr;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_GST_COMPRESSED_DECODER_HPP
