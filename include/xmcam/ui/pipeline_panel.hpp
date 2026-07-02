/*
 * @file pipeline_panel.hpp
 * @brief Network-stream composer: presets + editable raw GStreamer pipeline,
 *        Validate (parse-only) and Play/Stop.
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
  AppController* app_;
  char buffer_[2048];
  std::string validate_msg_;
  bool validate_ok_ = false;
  std::string rtsp_msg_;  // last RTSP-export failure, shown in the panel
  int export_port_ = 8554;
};

}  // namespace xmotion

#endif  // XMCAM_UI_PIPELINE_PANEL_HPP
