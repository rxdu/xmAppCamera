/*
 * @file stats_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/stats_panel.hpp"

#include "imgui.h"

namespace xmotion {

StatsPanel::StatsPanel(AppController* app)
    : quickviz::Panel("Stats"), app_(app) {
  this->SetAutoLayout(false);
}

void StatsPanel::Draw() {
  Begin();
  {
    const SourceStats s = app_->Stats();
    const AppController::DisplayStats d = app_->display_stats();
    // Frames captured but never shown (coalesced by the latest-only stream).
    const uint64_t dropped =
        s.frames > d.frames_shown ? s.frames - d.frames_shown : 0;

    ImGui::Text("status   : %s", app_->status().c_str());
    if (d.width > 0) ImGui::Text("size     : %dx%d", d.width, d.height);
    ImGui::Separator();
    ImGui::Text("capture  : %.1f fps", s.capture_fps);
    ImGui::Text("display  : %.1f fps", d.display_fps);
    ImGui::Text("captured : %llu", static_cast<unsigned long long>(s.frames));
    ImGui::Text("shown    : %llu",
                static_cast<unsigned long long>(d.frames_shown));
    ImGui::Text("dropped  : %llu", static_cast<unsigned long long>(dropped));
    ImGui::Separator();
    ImGui::Text("decode   : %.2f ms", s.decode_ms);
    ImGui::Text("upload   : %.2f ms", d.upload_ms);
    if (d.latency_ms > 0)
      ImGui::Text("latency  : %.1f ms", d.latency_ms);
    ImGui::Text("decoder  : %s", s.decoder.empty() ? "-" : s.decoder.c_str());
  }
  End();
}

}  // namespace xmotion
