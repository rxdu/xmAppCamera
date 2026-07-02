/*
 * @file export_panel.hpp
 * @brief RTSP re-export tab: publish the selected running session (camera or
 *        network stream) as an RTSP/H.264 stream. Several sessions can export
 *        simultaneously (each hosts its own server/port).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_EXPORT_PANEL_HPP
#define XMCAM_UI_EXPORT_PANEL_HPP

#include <string>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class ExportPanel : public quickviz::Panel {
 public:
  explicit ExportPanel(AppController* app);
  void Draw() override;

 private:
  AppController* app_;
  int port_ = 8554;
  std::string error_;
};

}  // namespace xmotion

#endif  // XMCAM_UI_EXPORT_PANEL_HPP
