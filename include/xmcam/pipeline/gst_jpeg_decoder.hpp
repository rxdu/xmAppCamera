/*
 * @file gst_jpeg_decoder.hpp
 * @brief Synchronous MJPEG decoder (appsrc ! jpegdec ! videoconvert ! appsink)
 *        used by V4l2Source when the camera streams compressed MJPEG — its
 *        primary high-framerate format. One JPEG in, one RGBA VideoFrame out.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_GST_JPEG_DECODER_HPP
#define XMCAM_PIPELINE_GST_JPEG_DECODER_HPP

#include <cstddef>
#include <cstdint>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include "xmcam/core/result.hpp"
#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class GstJpegDecoder {
 public:
  GstJpegDecoder() = default;
  ~GstJpegDecoder();

  Status Open();  // builds + starts the decode pipeline
  void Close();

  // Decode one JPEG frame. `out` receives an RGBA VideoFrame (owning the
  // decoded gst buffer). Blocking with a bounded timeout.
  Status Decode(const uint8_t* jpeg, std::size_t len, int64_t pts_ns,
                uint64_t seq, VideoFrame* out);

 private:
  GstElement* pipeline_ = nullptr;
  GstAppSrc* src_ = nullptr;
  GstAppSink* sink_ = nullptr;
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_GST_JPEG_DECODER_HPP
