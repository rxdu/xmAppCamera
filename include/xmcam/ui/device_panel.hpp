/*
 * @file device_panel.hpp
 * @brief USB camera manager: one collapsing block per camera, each with its
 *        own format/size/fps combos, stateful Start/Apply/Stop buttons, and
 *        inline live stats. Multiple cameras stream concurrently; clicking a
 *        block selects that camera (drives Controls/Qualify targeting).
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_DEVICE_PANEL_HPP
#define XMCAM_UI_DEVICE_PANEL_HPP

#include <string>
#include <unordered_map>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class DevicePanel : public quickviz::Panel {
 public:
  explicit DevicePanel(AppController* app);
  void Draw() override;

 private:
  struct Sel {
    int fmt = 0;
    int size = 0;
    int fps = 0;
  };
  void DrawDeviceBlock(const DeviceInfo& dev, int index);

  AppController* app_;
  bool enumerated_ = false;
  std::unordered_map<std::string, Sel> sel_;  // per device path
  std::string sel_error_;  // last start failure (shown on selected block)
};

}  // namespace xmotion

#endif  // XMCAM_UI_DEVICE_PANEL_HPP
