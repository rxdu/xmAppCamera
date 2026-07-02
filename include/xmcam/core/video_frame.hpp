/*
 * @file video_frame.hpp
 * @brief Native decoded-frame handle. Cheap to copy (refcounts the underlying
 *        buffer); never copies pixels. NO cv::Mat — see docs/adr/0001.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_VIDEO_FRAME_HPP
#define XMCAM_CORE_VIDEO_FRAME_HPP

#include <cstdint>
#include <memory>

#include "xmcam/core/pixel_format.hpp"

namespace xmotion {

// A handle to one decoded frame. The pixel memory is owned by `owner` (a
// GstSample ref, a V4L2 mmap-slot lease, or a heap block) whose custom deleter
// unmaps / re-queues / frees on last release. Copying a VideoFrame bumps the
// refcount only — it does not copy pixels — so it flows through
// DataStream<VideoFrame> without allocation on the hot path.
struct VideoFrame {
  int width = 0;
  int height = 0;
  PixelFormat format = PixelFormat::kUnknown;

  // Plane 0 (packed formats use only this).
  const uint8_t* data = nullptr;
  int stride = 0;  // bytes per row of plane 0

  // Planes 1/2 (e.g. U/V for I420, interleaved UV for NV12 uses only plane1);
  // nullptr for packed formats.
  const uint8_t* plane1 = nullptr;
  int stride1 = 0;
  const uint8_t* plane2 = nullptr;
  int stride2 = 0;

  int64_t pts_ns = 0;   // presentation timestamp, for latency accounting
  uint64_t seq = 0;     // monotonically increasing capture sequence

  std::shared_ptr<void> owner;  // keeps the pixel buffer alive

  bool valid() const {
    return data != nullptr && width > 0 && height > 0 &&
           format != PixelFormat::kUnknown;
  }
};

}  // namespace xmotion

#endif  // XMCAM_CORE_VIDEO_FRAME_HPP
