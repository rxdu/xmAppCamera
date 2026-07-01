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
    ImGui::Text("status   : %s", app_->status().c_str());
    ImGui::Separator();
    ImGui::Text("capture  : %.1f fps", s.capture_fps);
    ImGui::Text("frames   : %llu", static_cast<unsigned long long>(s.frames));
    ImGui::Text("dropped  : %llu", static_cast<unsigned long long>(s.dropped));
    ImGui::Text("decode   : %.2f ms", s.decode_ms);
    ImGui::Text("decoder  : %s", s.decoder.empty() ? "-" : s.decoder.c_str());
  }
  End();
}

}  // namespace xmotion
