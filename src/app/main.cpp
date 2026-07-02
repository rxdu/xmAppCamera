/*
 * @file main.cpp
 * @brief xmAppCamera entry point: builds the docked window and panels.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include <cstdlib>
#include <memory>

#include "imgui.h"

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
  // quickviz's EnableDocking turns on shift-gated docking, which makes
  // re-docking an undocked panel look impossible (no drop overlay without
  // Shift). Plain drag-to-dock is the expected behavior here.
  ImGui::GetIO().ConfigDockingWithShift = false;
  viewer.ApplyDarkColorScheme();

  // One root panel fills the window and lays out the sidebar + preview.
  viewer.AddSceneObject(std::make_shared<MainDockingPanel>(&app));

  // Demo/headless-test hook: auto-start the first camera when XMCAM_AUTOSTART
  // is set, so the preview path can be exercised without clicking.
  if (std::getenv("XMCAM_AUTOSTART")) {
    if (auto st = app.StartFirstDevice(); !st.ok())
      XLOG_WARN("autostart: {}", st.message());
    // Optional second hook: also publish the selected source via RTSP.
    if (const char* port = std::getenv("XMCAM_AUTORTSP")) {
      if (AppController::Session* s = app.selected()) {
        if (auto st = app.StartRtspExport(*s, std::atoi(port), "/cam");
            !st.ok())
          XLOG_WARN("auto rtsp export: {}", st.message());
      }
    }
  }
  // Headless-test hook: add a synthetic network-stream session so the tiled
  // preview can be exercised without a second physical camera.
  if (std::getenv("XMCAM_AUTOGST")) {
    if (auto st = app.StartGst(
            "videotestsrc is-live=true pattern=smpte ! "
            "video/x-raw,width=640,height=480,framerate=30/1 ! videoconvert "
            "! video/x-raw,format=I420 ! "
            "appsink name=sink max-buffers=1 drop=true sync=false");
        !st.ok())
      XLOG_WARN("auto gst: {}", st.message());
  }

  viewer.Show();  // blocking render loop

  XLOG_INFO("xmAppCamera exiting");
  return 0;
}
