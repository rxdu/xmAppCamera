/*
 * @file yuv_converter.hpp
 * @brief GPU YUV->RGB: uploads raw I420/NV12 planes and converts to an RGBA
 *        texture in a fragment shader (offscreen FBO). Lets the decode path emit
 *        native I420 (no GStreamer videoconvert) and moves the colorspace math
 *        onto the GPU. Render-thread only.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_YUV_CONVERTER_HPP
#define XMCAM_UI_YUV_CONVERTER_HPP

#include "xmcam/core/video_frame.hpp"

namespace xmotion {

class YuvConverter {
 public:
  YuvConverter() = default;
  ~YuvConverter();

  // True for formats this converter accepts (I420, NV12).
  static bool Supports(PixelFormat f);

  // Convert `f` to RGBA in an offscreen texture; returns that texture id (0 on
  // failure). The returned texture is owned by the converter and reused.
  unsigned Convert(const VideoFrame& f);

 private:
  bool EnsureResources();
  void EnsureFbo(int w, int h);
  void UploadPlane(unsigned tex, int unit, const uint8_t* data, int w, int h,
                   int stride, unsigned gl_format);

  bool init_ = false;
  bool failed_ = false;
  unsigned program_ = 0;
  unsigned vao_ = 0;
  unsigned fbo_ = 0;
  unsigned rgba_tex_ = 0;
  unsigned y_tex_ = 0;
  unsigned u_tex_ = 0;  // I420 U, or NV12 interleaved UV
  unsigned v_tex_ = 0;  // I420 V
  int fbo_w_ = 0;
  int fbo_h_ = 0;
  int loc_mode_ = -1;
};

}  // namespace xmotion

#endif  // XMCAM_UI_YUV_CONVERTER_HPP
