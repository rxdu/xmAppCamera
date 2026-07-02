/*
 * @file preview_panel.hpp
 * @brief Live video preview. Pulls the latest VideoFrame from the active source
 *        (render thread) and uploads it to a GL texture drawn with ImGui.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_PREVIEW_PANEL_HPP
#define XMCAM_UI_PREVIEW_PANEL_HPP

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/core/util/rate_counter.hpp"
#include "xmcam/ui/frame_texture.hpp"
#include "xmcam/ui/yuv_converter.hpp"

namespace xmotion {

class PreviewPanel : public quickviz::Panel {
 public:
  explicit PreviewPanel(AppController* app);
  void Draw() override;

 private:
  AppController* app_;
  FrameTexture texture_;     // packed RGB/BGR/GRAY (+ CPU YUYV fallback)
  YuvConverter yuv_;         // GPU I420/NV12 -> RGBA
  unsigned display_tex_ = 0;
  RateCounter display_rate_;
  bool have_frame_ = false;
  int last_w_ = 0;
  int last_h_ = 0;
  uint64_t frames_shown_ = 0;
};

}  // namespace xmotion

#endif  // XMCAM_UI_PREVIEW_PANEL_HPP
