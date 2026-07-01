/*
 * @file source_caps.hpp
 * @brief Capability tree of a source: {format -> {WxH -> [fps]}}.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_CORE_SOURCE_CAPS_HPP
#define XMCAM_CORE_SOURCE_CAPS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "xmcam/core/pixel_format.hpp"

namespace xmotion {

struct FrameSize {
  int width = 0;
  int height = 0;
  std::vector<double> fps;  // discrete frame rates, descending
};

struct FormatDesc {
  PixelFormat format = PixelFormat::kUnknown;
  uint32_t fourcc = 0;
  std::string description;
  bool compressed = false;
  std::vector<FrameSize> sizes;
};

// Ordered list of formats a source supports, with their sizes/rates.
struct SourceCaps {
  std::vector<FormatDesc> formats;

  bool empty() const { return formats.empty(); }

  // Add (or extend) an entry. Returns the FormatDesc for further population.
  FormatDesc& AddFormat(PixelFormat format, uint32_t fourcc,
                        std::string description, bool compressed);

  // Find a format entry by PixelFormat; nullptr if absent.
  const FormatDesc* Find(PixelFormat format) const;
};

}  // namespace xmotion

#endif  // XMCAM_CORE_SOURCE_CAPS_HPP
