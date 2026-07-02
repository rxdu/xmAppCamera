/*
 * @file control_panel.hpp
 * @brief Live camera-control tuner. Renders each V4L2 control by type; disables
 *        inactive/read-only ones; re-reads values after a write so auto/manual
 *        dependencies (e.g. exposure) update immediately.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_CONTROL_PANEL_HPP
#define XMCAM_UI_CONTROL_PANEL_HPP

#include <cstdint>
#include <string>
#include <unordered_map>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class ControlPanel : public quickviz::Panel {
 public:
  explicit ControlPanel(AppController* app);
  void Draw() override;

 private:
  void ReloadValues(ControlSet* cs);

  AppController* app_;
  int loaded_epoch_ = -1;
  std::unordered_map<uint32_t, int64_t> values_;
};

}  // namespace xmotion

#endif  // XMCAM_UI_CONTROL_PANEL_HPP
