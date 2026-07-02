/*
 * @file main_docking_panel.hpp
 * @brief Root panel that fills the window and lays out the app like the
 *        RealSense Viewer: a fixed-fraction left control sidebar (device,
 *        controls, network, stats) and a dominant central preview. Rebuilt from
 *        the viewport size every frame, so it scales on resize/maximize.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_MAIN_DOCKING_PANEL_HPP
#define XMCAM_UI_MAIN_DOCKING_PANEL_HPP

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/ui/control_panel.hpp"
#include "xmcam/ui/device_panel.hpp"
#include "xmcam/ui/pipeline_panel.hpp"
#include "xmcam/ui/preview_panel.hpp"
#include "xmcam/ui/qualify_panel.hpp"
#include "xmcam/ui/stats_panel.hpp"

namespace xmotion {

class MainDockingPanel : public quickviz::Panel {
 public:
  explicit MainDockingPanel(AppController* app);
  void Draw() override;

 private:
  DevicePanel device_;
  PreviewPanel preview_;
  ControlPanel control_;
  PipelinePanel pipeline_;
  StatsPanel stats_;
  QualifyPanel qualify_;

  unsigned dockspace_id_ = 0;
  bool layout_initialized_ = false;
};

}  // namespace xmotion

#endif  // XMCAM_UI_MAIN_DOCKING_PANEL_HPP
