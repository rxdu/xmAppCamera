/*
 * @file frame_sink.hpp
 * @brief A consumer that a VideoSource tees each frame to (in addition to the
 *        latest-only preview stream). Used by RtspSink to re-export a source.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_FRAME_SINK_HPP
#define XMCAM_CORE_FRAME_SINK_HPP

#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class FrameSink {
 public:
  virtual ~FrameSink() = default;
  // Called on the source's producer thread for every captured/decoded frame.
  // Implementations must not block the producer.
  virtual void OnFrame(const VideoFrame& frame) = 0;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_FRAME_SINK_HPP
