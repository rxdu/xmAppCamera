/*
 * @file export_panel.hpp
 * @brief Export tab: lists every active camera/stream session. Per session:
 *        RTSP re-export (bind interface, port, mount suffix, enable/disable)
 *        and file recording (raw Y4M / lossless FFV1 / H.264) — both can run
 *        at once thanks to the frame fanout.
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
    // File recording
    int rec_format = 0;          // 0=raw y4m, 1=lossless mkv, 2=h264 mkv
    char rec_dir[256] = "recordings";
    std::string rec_error;
  };
  void DrawRow(AppController::Session& s);

  AppController* app_;
  std::unordered_map<std::string, Cfg> cfg_;  // by session key
  int next_port_ = 8554;
};

}  // namespace xmotion

#endif  // XMCAM_UI_EXPORT_PANEL_HPP
