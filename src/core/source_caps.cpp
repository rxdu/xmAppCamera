/*
 * @file source_caps.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/core/source_caps.hpp"

namespace xmotion {

FormatDesc& SourceCaps::AddFormat(PixelFormat format, uint32_t fourcc,
                                  std::string description, bool compressed) {
  for (auto& f : formats) {
    if (f.format == format && f.fourcc == fourcc) return f;
  }
  formats.push_back(FormatDesc{format, fourcc, std::move(description),
                               compressed, {}});
  return formats.back();
}

const FormatDesc* SourceCaps::Find(PixelFormat format) const {
  for (const auto& f : formats) {
    if (f.format == format) return &f;
  }
  return nullptr;
}

}  // namespace xmotion
