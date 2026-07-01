/*
 * @file stats_panel.hpp
 * @brief Live performance readout: capture fps, frames, drops, decode ms,
 *        active decoder, and source status.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_STATS_PANEL_HPP
#define XMCAM_UI_STATS_PANEL_HPP

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class StatsPanel : public quickviz::Panel {
 public:
  explicit StatsPanel(AppController* app);
  void Draw() override;

 private:
  AppController* app_;
};

}  // namespace xmotion

#endif  // XMCAM_UI_STATS_PANEL_HPP
