/*
 * @file source_descriptor.hpp
 * @brief What to open: a V4L2 device + requested format, or a GStreamer URI /
 *        explicit pipeline.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_SOURCE_DESCRIPTOR_HPP
#define XMCAM_CORE_SOURCE_DESCRIPTOR_HPP

#include <cstdint>
#include <string>

#include "xmcam/core/pixel_format.hpp"

namespace xmotion {

struct SourceDescriptor {
  enum class Type { kV4l2, kGstreamer };

  Type type = Type::kV4l2;

  // V4L2: device node or /dev/v4l/by-id/... path.
  std::string device;

  // GStreamer: source URI (rtsp://, udp://...) OR a full explicit pipeline
  // (pipeline takes precedence when non-empty).
  std::string uri;
  std::string pipeline;

  // Requested capture format (V4L2). 0/0 means "pick a sensible default".
  PixelFormat format = PixelFormat::kUnknown;
  int width = 0;
  int height = 0;
  double fps = 0.0;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_SOURCE_DESCRIPTOR_HPP
