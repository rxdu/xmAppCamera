/*
 * @file preview_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/preview_panel.hpp"

#include <chrono>

#include "imgui.h"

#include "xmcam/core/util/scope_timer.hpp"

namespace xmotion {
namespace {
// V4L2 stamps CLOCK_MONOTONIC, matching steady_clock on Linux; GStreamer PTS is
// pipeline-relative, so only trust deltas that look sane (< 5 s).
double GlassToGlassMs(int64_t pts_ns) {
  if (pts_ns <= 0) return 0.0;
  const int64_t now =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  const double ms = (now - pts_ns) / 1e6;
  return (ms >= 0.0 && ms < 5000.0) ? ms : 0.0;
}
}  // namespace

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
      double upload_ms = 0.0;
      {
        ScopeTimer t("upload", &upload_ms);
        display_tex_ = YuvConverter::Supports(f.format) ? yuv_.Convert(f)
                                                        : texture_.Upload(f);
      }
      have_frame_ = display_tex_ != 0;
      last_w_ = f.width;
      last_h_ = f.height;
      const double fps = display_rate_.Tick();
      ++frames_shown_;

      AppController::DisplayStats ds;
      ds.display_fps = fps;
      ds.upload_ms = upload_ms;
      ds.latency_ms = GlassToGlassMs(f.pts_ns);
      ds.frames_shown = frames_shown_;
      ds.width = f.width;
      ds.height = f.height;
      app_->SetDisplayStats(ds);
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (have_frame_ && display_tex_ != 0) {
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
                       static_cast<intptr_t>(display_tex_)),
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
