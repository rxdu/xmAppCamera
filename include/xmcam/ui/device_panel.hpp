/*
 * @file device_panel.hpp
 * @brief Camera slot manager: starts with one camera-settings block; "+ Add
 *        Camera" appends more as needed. Each slot picks its own device (from
 *        the devices not claimed by other slots), configures format/size/fps,
 *        and has stateful Start/Apply/Stop + inline stats. Removing a slot
 *        stops its stream. Clicking a slot selects that camera globally.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_DEVICE_PANEL_HPP
#define XMCAM_UI_DEVICE_PANEL_HPP

#include <string>
#include <vector>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class DevicePanel : public quickviz::Panel {
 public:
  explicit DevicePanel(AppController* app);
  void Draw() override;

 private:
  struct Slot {
    std::string device;  // chosen device path ("" until picked)
    int fmt = 0;
    int size = 0;
    int fps = 0;
    std::string error;  // last start failure for this slot
  };

  // Returns false if the slot's Remove button was pressed.
  bool DrawSlot(Slot& slot, int index);
  const DeviceInfo* FindDevice(const std::string& device) const;
  bool ClaimedByOther(const std::string& device, int self_index) const;

  AppController* app_;
  bool enumerated_ = false;
  std::vector<Slot> slots_;
};

}  // namespace xmotion

#endif  // XMCAM_UI_DEVICE_PANEL_HPP
