/*
 * @file stats_view.hpp
 * @brief Compact live-stats block for one session, rendered under the Stop
 *        button of the panel that owns it (Device blocks, Network Stream).
 *        Short lines, so narrow sidebars never clip.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_STATS_VIEW_HPP
#define XMCAM_UI_STATS_VIEW_HPP

#include "imgui.h"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/ui/widgets.hpp"

namespace xmotion {

inline void DrawSourceStatsBlock(AppController::Session& s) {
  const SourceStats st = s.source ? s.source->GetStats() : SourceStats{};
  const AppController::DisplayStats& d = s.display_stats;
  const uint64_t dropped =
      st.frames > d.frames_shown ? st.frames - d.frames_shown : 0;

  if (st.device_lost)
    ImGui::TextColored(kTextError, "DEVICE LOST - recovering...");

  ImGui::Bullet();
  ImGui::Text("capture : %.1f fps", st.capture_fps);
  ImGui::Bullet();
  ImGui::Text("display : %.1f fps", d.display_fps);
  ImGui::Bullet();
  ImGui::Text("frames  : %llu shown / %llu drop",
              static_cast<unsigned long long>(d.frames_shown),
              static_cast<unsigned long long>(dropped));
  ImGui::Bullet();
  ImGui::Text("decode  : %.2f ms", st.decode_ms);
  ImGui::Bullet();
  ImGui::Text("upload  : %.2f ms", d.upload_ms);
  if (d.latency_ms > 0) {
    ImGui::Bullet();
    ImGui::Text("latency : %.1f ms", d.latency_ms);
  }
  ImGui::Bullet();
  ImGui::Text("decoder : %s", st.decoder.empty() ? "-" : st.decoder.c_str());
  if (st.generation > 0) {
    ImGui::Bullet();
    ImGui::Text("recovered %u time(s)", st.generation);
  }
}

}  // namespace xmotion

#endif  // XMCAM_UI_STATS_VIEW_HPP
