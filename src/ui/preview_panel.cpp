/*
 * @file preview_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/preview_panel.hpp"

#include <algorithm>
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

  const ImVec2 vid_min{cell_min.x + 2, cell_min.y + 2};
  const ImVec2 vid_max{cell_max.x - 2, cell_max.y - 2};

  ImVec2 img_min{cell_min.x + 8, cell_min.y + 8};
  bool have_img = false;
  if (tile.have_frame && tile.display_tex != 0 && tile.w > 0) {
    // Aspect-fit the frame into the cell.
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
    img_min = ImVec2(x0, y0);
    have_img = true;
  } else {
    dl->AddText(ImVec2(cell_min.x + 8, cell_min.y + 8),
                ImGui::GetColorU32(kTextIdle), "waiting for frames...");
  }

  // One consolidated OSD card at the image top-left: title, mode line and a
  // structured label/value table (nothing floats in the letterbox band).
  if (have_img) {
    const float lh = ImGui::GetTextLineHeight();
    const float pad = 7.0f;
    const ImVec2 origin{img_min.x + 8, img_min.y + 8};
    const ImU32 col_txt = IM_COL32(225, 225, 225, 255);
    const ImU32 col_dim = IM_COL32(160, 160, 160, 255);
    const ImU32 col_title = ImGui::GetColorU32(kTextLive);

    char sub[96];
    snprintf(sub, sizeof sub, "%dx%d", tile.w, tile.h);

    if (!s.stats_overlay) {
      // Minimal card: identity only.
      char line[160];
      snprintf(line, sizeof line, "%s  %s  %.1f fps", s.label.c_str(), sub,
               s.display_stats.display_fps);
      const ImVec2 ts = ImGui::CalcTextSize(line);
      dl->AddRectFilled(origin,
                        ImVec2(origin.x + ts.x + 2 * pad,
                               origin.y + lh + 2 * pad - 2),
                        IM_COL32(0, 0, 0, 190), 4.0f);
      dl->AddText(ImVec2(origin.x + pad, origin.y + pad - 1), col_title,
                  line);
    } else if (s.source) {
      const SourceStats st = s.source->GetStats();
      const AppController::DisplayStats& d = s.display_stats;
      const uint64_t dropped =
          st.frames > d.frames_shown ? st.frames - d.frames_shown : 0;

      char mode[96];
      snprintf(mode, sizeof mode, "%s   %s", sub,
               st.decoder.empty() ? "-" : st.decoder.c_str());

      struct Row {
        const char* k;
        char v[32];
      };
      Row rows[6];
      int n = 0;
      snprintf(rows[n].v, sizeof rows[n].v, "%.1f fps", st.capture_fps);
      rows[n++].k = "capture";
      snprintf(rows[n].v, sizeof rows[n].v, "%.1f fps", d.display_fps);
      rows[n++].k = "display";
      snprintf(rows[n].v, sizeof rows[n].v, "%llu",
               static_cast<unsigned long long>(dropped));
      rows[n++].k = "dropped";
      snprintf(rows[n].v, sizeof rows[n].v, "%.1f ms", st.decode_ms);
      rows[n++].k = "decode";
      snprintf(rows[n].v, sizeof rows[n].v, "%.2f ms", d.upload_ms);
      rows[n++].k = "upload";
      if (d.latency_ms > 0) {
        snprintf(rows[n].v, sizeof rows[n].v, "%.0f ms", d.latency_ms);
        rows[n++].k = "latency";
      }

      // Column geometry: labels left, values right-aligned in their column.
      float key_w = 0, val_w = 0;
      for (int i = 0; i < n; ++i) {
        key_w = std::max(key_w, ImGui::CalcTextSize(rows[i].k).x);
        val_w = std::max(val_w, ImGui::CalcTextSize(rows[i].v).x);
      }
      const float gap = 14.0f;
      float card_w = std::max(
          {ImGui::CalcTextSize(s.label.c_str()).x,
           ImGui::CalcTextSize(mode).x, key_w + gap + val_w});
      const char* extra = nullptr;
      if (st.device_lost)
        extra = "DEVICE LOST - recovering";
      else if (st.generation > 0)
        extra = "recovered after replug";
      if (extra)
        card_w = std::max(card_w, ImGui::CalcTextSize(extra).x);

      const int total_lines = 2 + n + (extra ? 1 : 0);
      const float line_gap = 3.0f;
      dl->AddRectFilled(
          origin,
          ImVec2(origin.x + card_w + 2 * pad,
                 origin.y + total_lines * (lh + line_gap) + 2 * pad),
          IM_COL32(0, 0, 0, 190), 5.0f);

      float y = origin.y + pad;
      dl->AddText(ImVec2(origin.x + pad, y), col_title, s.label.c_str());
      y += lh + line_gap;
      dl->AddText(ImVec2(origin.x + pad, y), col_dim, mode);
      y += lh + line_gap + 2.0f;
      for (int i = 0; i < n; ++i) {
        dl->AddText(ImVec2(origin.x + pad, y), col_dim, rows[i].k);
        const float vw = ImGui::CalcTextSize(rows[i].v).x;
        dl->AddText(
            ImVec2(origin.x + pad + key_w + gap + (val_w - vw), y),
            col_txt, rows[i].v);
        y += lh + line_gap;
      }
      if (extra)
        dl->AddText(ImVec2(origin.x + pad, y),
                    ImGui::GetColorU32(st.device_lost ? kTextError
                                                      : kTextPending),
                    extra);
    }
  }

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

    // Gesture hints: the tile interactions are otherwise invisible.
    if (live.size() > 1) {
      if (!solo_key_.empty())
        Caption("double-click: back to grid");
      else
        Caption("click: select (drives Controls/Qualify/Export) - "
                "double-click: solo");
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
