/*
 * @file preview_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/preview_panel.hpp"

#include <chrono>
#include <cmath>
#include <vector>

#include "imgui.h"

#include "xmcam/core/util/scope_timer.hpp"
#include "xmcam/ui/widgets.hpp"

namespace xmotion {
namespace {

// V4L2 stamps CLOCK_MONOTONIC, matching steady_clock on Linux; GStreamer PTS
// is pipeline-relative, so only trust deltas that look sane (< 5 s).
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

void PreviewPanel::UpdateTile(AppController::Session& s, Tile& tile) {
  VideoFrame f;
  bool got = false;
  while (app_->PullFrame(s, &f)) got = true;  // coalesce to newest
  if (!got || !f.valid()) return;

  double upload_ms = 0.0;
  {
    ScopeTimer t("upload", &upload_ms);
    tile.display_tex = YuvConverter::Supports(f.format)
                           ? tile.yuv.Convert(f)
                           : tile.texture.Upload(f);
  }
  tile.have_frame = tile.display_tex != 0;
  tile.w = f.width;
  tile.h = f.height;
  const double fps = tile.display_rate.Tick();
  ++tile.frames_shown;

  AppController::DisplayStats& ds = s.display_stats;
  ds.display_fps = fps;
  ds.upload_ms = upload_ms;
  ds.latency_ms = GlassToGlassMs(f.pts_ns);
  ds.frames_shown = tile.frames_shown;
  ds.width = f.width;
  ds.height = f.height;
}

void PreviewPanel::DrawTile(AppController::Session& s, Tile& tile,
                            ImVec2 cell_min, ImVec2 cell_max,
                            bool selectable) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool selected = selectable && app_->selected_key() == s.key;

  // Click/double-click target covering the whole cell. Clicking the already-
  // selected tile deselects it (Controls/Qualify fall back to the first
  // running camera), so the highlight can be dismissed.
  ImGui::SetCursorScreenPos(cell_min);
  ImGui::PushID(s.id);
  ImGui::InvisibleButton("tile", ImVec2(cell_max.x - cell_min.x,
                                        cell_max.y - cell_min.y));
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
    solo_key_ = (solo_key_ == s.key) ? std::string() : s.key;
  else if (selectable && ImGui::IsItemClicked(0))
    app_->Select(selected ? std::string() : s.key);
  ImGui::PopID();

  const float header_h = ImGui::GetTextLineHeight() + 4.0f;
  const ImVec2 vid_min{cell_min.x + 2, cell_min.y + header_h};
  const ImVec2 vid_max{cell_max.x - 2, cell_max.y - 2};

  if (tile.have_frame && tile.display_tex != 0 && tile.w > 0) {
    // Aspect-fit the frame into the video region.
    const float avail_w = vid_max.x - vid_min.x;
    const float avail_h = vid_max.y - vid_min.y;
    const float ar = static_cast<float>(tile.w) / tile.h;
    float w = avail_w, h = avail_w / ar;
    if (h > avail_h) {
      h = avail_h;
      w = avail_h * ar;
    }
    const float x0 = vid_min.x + (avail_w - w) * 0.5f;
    const float y0 = vid_min.y + (avail_h - h) * 0.5f;
    dl->AddImage(reinterpret_cast<void*>(
                     static_cast<intptr_t>(tile.display_tex)),
                 ImVec2(x0, y0), ImVec2(x0 + w, y0 + h));
  } else {
    dl->AddText(ImVec2(cell_min.x + 8, cell_min.y + header_h + 8),
                ImGui::GetColorU32(kTextIdle), "waiting for frames...");
  }

  // Header: label left, resolution+fps right.
  char right[64];
  snprintf(right, sizeof right, "%dx%d  %.1f fps", tile.w, tile.h,
           s.display_stats.display_fps);
  dl->AddText(ImVec2(cell_min.x + 6, cell_min.y + 2),
              ImGui::GetColorU32(kTextLive), s.label.c_str());
  const ImVec2 rsz = ImGui::CalcTextSize(right);
  dl->AddText(ImVec2(cell_max.x - rsz.x - 6, cell_min.y + 2),
              ImGui::GetColorU32(kTextLive), right);

  // Selection border (thin neutral border otherwise).
  dl->AddRect(cell_min, cell_max,
              ImGui::GetColorU32(selected ? kTextPending
                                          : ImVec4(0.3f, 0.3f, 0.3f, 1.0f)),
              0.0f, 0, selected ? 2.0f : 1.0f);
}

void PreviewPanel::Draw() {
  Begin();
  {
    app_->MaintainRecovery();

    // Live sessions to display (solo filter applies after collection).
    std::vector<AppController::Session*> live;
    for (auto& s : app_->sessions())
      if (s->source) live.push_back(s.get());

    // Drop GPU state of sessions that no longer exist.
    for (auto it = tiles_.begin(); it != tiles_.end();) {
      bool found = false;
      for (auto* s : live) found |= (s->id == it->first);
      it = found ? std::next(it) : tiles_.erase(it);
    }
    if (!solo_key_.empty()) {
      bool solo_alive = false;
      for (auto* s : live) solo_alive |= (s->key == solo_key_);
      if (!solo_alive) solo_key_.clear();
    }

    // Upload every live session's newest frame (even off-grid solos keep
    // consuming, so streams never back up).
    for (auto* s : live) {
      auto& tile = tiles_[s->id];
      if (!tile) tile = std::make_unique<Tile>();
      UpdateTile(*s, *tile);
    }

    if (live.empty()) {
      ImGui::TextDisabled("no signal - start a camera or a network stream");
      End();
      return;
    }

    std::vector<AppController::Session*> shown;
    if (!solo_key_.empty()) {
      for (auto* s : live)
        if (s->key == solo_key_) shown.push_back(s);
    } else {
      shown = live;
    }

    // Auto grid: cols = ceil(sqrt(n)).
    const int n = static_cast<int>(shown.size());
    const int cols = static_cast<int>(std::ceil(std::sqrt(n)));
    const int rows = (n + cols - 1) / cols;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cw = avail.x / cols;
    const float ch = avail.y / rows;

    for (int i = 0; i < n; ++i) {
      const int c = i % cols;
      const int r = i / cols;
      const ImVec2 cmin{origin.x + c * cw + 2, origin.y + r * ch + 2};
      const ImVec2 cmax{origin.x + (c + 1) * cw - 2,
                        origin.y + (r + 1) * ch - 2};
      DrawTile(*shown[i], *tiles_[shown[i]->id], cmin, cmax, n > 1);
    }
  }
  End();
}

}  // namespace xmotion
