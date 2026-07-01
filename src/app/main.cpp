/*
 * @file main.cpp
 * @brief xmAppCamera entry point: builds the docked window and panels.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <memory>

#include "viewer/viewer.hpp"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/ui/main_docking_panel.hpp"
#include "xmsigma/logging/xlogger.hpp"

int main() {
  using namespace quickviz;
  using namespace xmotion;

  XLOG_INFO("xmAppCamera starting");

  // Outlives the viewer/panels (declared first, destroyed last).
  AppController app;

  Viewer viewer("xmAppCamera", 1600, 900);
  viewer.EnableDocking(true);
  viewer.ApplyDarkColorScheme();

  // One root panel fills the window and lays out the sidebar + preview.
  viewer.AddSceneObject(std::make_shared<MainDockingPanel>(&app));

  viewer.Show();  // blocking render loop

  XLOG_INFO("xmAppCamera exiting");
  return 0;
}
