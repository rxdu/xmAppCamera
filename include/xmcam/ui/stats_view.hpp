/*
 * @file stats_view.hpp
 * @brief Compact live-stats block rendered under the Stop button of whichever
 *        panel owns the running source (Device for V4L2, Network Stream for
 *        GStreamer). Short lines, so narrow sidebars never clip.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_STATS_VIEW_HPP
#define XMCAM_UI_STATS_VIEW_HPP

#include "imgui.h"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/ui/widgets.hpp"

namespace xmotion {

inline void DrawSourceStatsBlock(AppController* app) {
  const SourceStats s = app->Stats();
  const AppController::DisplayStats d = app->display_stats();
  const uint64_t dropped =
      s.frames > d.frames_shown ? s.frames - d.frames_shown : 0;

  if (s.device_lost)
    ImGui::TextColored(kTextError, "DEVICE LOST - recovering...");

  ImGui::Bullet();
  ImGui::Text("capture : %.1f fps", s.capture_fps);
  ImGui::Bullet();
  ImGui::Text("display : %.1f fps", d.display_fps);
  ImGui::Bullet();
  ImGui::Text("frames  : %llu shown / %llu drop",
              static_cast<unsigned long long>(d.frames_shown),
              static_cast<unsigned long long>(dropped));
  ImGui::Bullet();
  ImGui::Text("decode  : %.2f ms", s.decode_ms);
  ImGui::Bullet();
  ImGui::Text("upload  : %.2f ms", d.upload_ms);
  if (d.latency_ms > 0) {
    ImGui::Bullet();
    ImGui::Text("latency : %.1f ms", d.latency_ms);
  }
  ImGui::Bullet();
  ImGui::Text("decoder : %s", s.decoder.empty() ? "-" : s.decoder.c_str());
  if (s.generation > 0) {
    ImGui::Bullet();
    ImGui::Text("recovered %u time(s)", s.generation);
  }
}

}  // namespace xmotion

#endif  // XMCAM_UI_STATS_VIEW_HPP
