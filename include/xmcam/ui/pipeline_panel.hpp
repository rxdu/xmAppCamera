/*
 * @file pipeline_panel.hpp
 * @brief Network-stream control. Simple mode: URL + latency, codec-agnostic
 *        pipeline generated (decodebin3 — works for H.264/H.265/MJPEG cameras
 *        alike). Advanced mode: raw GStreamer pipeline editing with Validate.
 *        Live connection state (CONNECTING/LIVE/ERROR/EOS) surfaced from the
 *        pipeline bus, with optional auto-reconnect.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_PIPELINE_PANEL_HPP
#define XMCAM_UI_PIPELINE_PANEL_HPP

#include <string>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class PipelinePanel : public quickviz::Panel {
 public:
  explicit PipelinePanel(AppController* app);
  void Draw() override;

 private:
  std::string BuildSimplePipeline() const;
  void Play(const std::string& pipeline);

  AppController* app_;
  bool advanced_ = false;
  char url_[512];
  int latency_ms_ = 50;
  bool auto_reconnect_ = true;
  char buffer_[2048];  // advanced raw pipeline
  std::string validate_msg_;
  bool validate_ok_ = false;
};

}  // namespace xmotion

#endif  // XMCAM_UI_PIPELINE_PANEL_HPP
