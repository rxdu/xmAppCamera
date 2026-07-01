/*
 * @file frame_texture.hpp
 * @brief Owns a GL texture and uploads native VideoFrames into it (render
 *        thread only). Packed RGB/BGR/GRAY upload directly; YUYV/NV12/I420 are
 *        converted to RGBA on the CPU for now (a GPU YUV shader is the Phase 4
 *        optimization behind this same seam).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_FRAME_TEXTURE_HPP
#define XMCAM_UI_FRAME_TEXTURE_HPP

#include <cstdint>
#include <vector>

#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class FrameTexture {
 public:
  FrameTexture() = default;
  ~FrameTexture();

  // Upload `f` to the GL texture; returns the texture id (0 if nothing valid
  // has been uploaded yet). Must be called on the render thread.
  unsigned Upload(const VideoFrame& f);

  unsigned id() const { return tex_; }
  int width() const { return w_; }
  int height() const { return h_; }
  bool valid() const { return tex_ != 0 && w_ > 0 && h_ > 0; }

 private:
  void EnsureTexture();

  unsigned tex_ = 0;
  int w_ = 0;
  int h_ = 0;
  PixelFormat fmt_ = PixelFormat::kUnknown;
  std::vector<uint8_t> rgba_;  // scratch for CPU conversions
};

}  // namespace xmotion

#endif  // XMCAM_UI_FRAME_TEXTURE_HPP
