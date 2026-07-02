/*
 * @file preview_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/preview_panel.hpp"

#include "imgui.h"

namespace xmotion {

PreviewPanel::PreviewPanel(AppController* app)
    : quickviz::Panel("Preview"), app_(app) {
  this->SetAutoLayout(false);
  this->SetNoBackground(true);
}

void PreviewPanel::Draw() {
  Begin();
  {
    // Drain to the newest frame (latest-only) and upload it once.
    VideoFrame f;
    bool got = false;
    while (app_->PullFrame(&f)) got = true;  // coalesce any backlog
    if (got && f.valid()) {
      texture_.Upload(f);
      have_frame_ = true;
      last_w_ = f.width;
      last_h_ = f.height;
      display_rate_.Tick();
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (have_frame_ && texture_.valid()) {
      // Aspect-fit into the available content region.
      const float ar = static_cast<float>(last_w_) / last_h_;
      float w = avail.x;
      float h = avail.x / ar;
      if (h > avail.y) {
        h = avail.y;
        w = avail.y * ar;
      }
      const ImVec2 cursor = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - w) * 0.5f,
                                 cursor.y + (avail.y - h) * 0.5f));
      ImGui::Image(reinterpret_cast<void*>(
                       static_cast<intptr_t>(texture_.id())),
                   ImVec2(w, h));
      // Small HUD overlay.
      ImGui::SetCursorPos(ImVec2(cursor.x + 8, cursor.y + 8));
      ImGui::TextColored(ImVec4(0, 1, 0, 1), "%dx%d  %.1f fps disp",
                         last_w_, last_h_, display_rate_.rate());
    } else {
      ImGui::TextDisabled("no signal - select a device or start a stream");
    }
  }
  End();
}

}  // namespace xmotion
