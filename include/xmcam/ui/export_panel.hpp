/*
 * @file export_panel.hpp
 * @brief RTSP re-export tab: lists every active camera/stream session with
 *        per-session bind interface, port and mount suffix, and an
 *        enable/disable toggle. Several sessions can export simultaneously
 *        (each hosts its own server).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_EXPORT_PANEL_HPP
#define XMCAM_UI_EXPORT_PANEL_HPP

#include <string>
#include <unordered_map>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class ExportPanel : public quickviz::Panel {
 public:
  explicit ExportPanel(AppController* app);
  void Draw() override;

 private:
  // Per-session export settings, kept across enable/disable cycles.
  struct Cfg {
    char address[64] = "0.0.0.0";  // bind interface (0.0.0.0 = all)
    int port = 0;                  // assigned on first sight
    char mount[64] = "/cam";       // URL suffix
    std::string error;
  };
  void DrawRow(AppController::Session& s);

  AppController* app_;
  std::unordered_map<std::string, Cfg> cfg_;  // by session key
  int next_port_ = 8554;
};

}  // namespace xmotion

#endif  // XMCAM_UI_EXPORT_PANEL_HPP
