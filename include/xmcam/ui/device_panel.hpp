/*
 * @file device_panel.hpp
 * @brief USB camera picker: enumerate devices, choose format/resolution/fps,
 *        start/stop capture.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_DEVICE_PANEL_HPP
#define XMCAM_UI_DEVICE_PANEL_HPP

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class DevicePanel : public quickviz::Panel {
 public:
  explicit DevicePanel(AppController* app);
  void Draw() override;

 private:
  AppController* app_;
  bool enumerated_ = false;
  int sel_dev_ = 0;
  int sel_fmt_ = 0;
  int sel_size_ = 0;
  int sel_fps_ = 0;
};

}  // namespace xmotion

#endif  // XMCAM_UI_DEVICE_PANEL_HPP
