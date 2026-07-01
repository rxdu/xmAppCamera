/*
 * @file video_source.hpp
 * @brief Abstract producer of VideoFrames. Concrete sources (V4l2Source,
 *        GstSource) normalize any input to native VideoFrames delivered via a
 *        latest-only DataStream. The display path is source-agnostic.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_PIPELINE_VIDEO_SOURCE_HPP
#define XMCAM_PIPELINE_VIDEO_SOURCE_HPP

#include <string>

#include "core/data_stream.hpp"  // quickviz header-only DataStream<T>

#include "xmcam/core/result.hpp"
#include "xmcam/core/source_caps.hpp"
#include "xmcam/core/source_descriptor.hpp"
#include "xmcam/core/source_stats.hpp"
#include "xmcam/core/video_frame.hpp"

namespace xmotion {

// DataStream is quickviz's header-only latest-only channel; alias it into our
// namespace so call sites read naturally.
using quickviz::DataStream;

class ControlSet;  // forward decl (V4L2 control surface, Phase 2)

// Discovered device (V4L2 enumeration).
struct DeviceInfo {
  std::string device;   // /dev/videoN actually opened
  std::string by_id;    // stable /dev/v4l/by-id/... path (may be empty)
  std::string card;     // VIDIOC_QUERYCAP card name
  std::string bus;      // bus_info
  SourceCaps caps;
};

class VideoSource {
 public:
  virtual ~VideoSource() = default;

  virtual Status Open(const SourceDescriptor& desc) = 0;
  virtual Status Start() = 0;  // spawns producer thread feeding Frames()
  virtual void Stop() = 0;
  virtual void Close() = 0;
  virtual bool IsRunning() const = 0;

  virtual SourceCaps GetCaps() const = 0;
  virtual SourceStats GetStats() const = 0;

  virtual DataStream<VideoFrame>& Frames() = 0;

  // Non-null only for sources with tunable controls (V4L2).
  virtual ControlSet* Controls() { return nullptr; }
};

}  // namespace xmotion

#endif  // XMCAM_PIPELINE_VIDEO_SOURCE_HPP
