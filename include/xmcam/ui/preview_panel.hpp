/*
 * @file preview_panel.hpp
 * @brief Tiled live preview of all running sessions. Auto grid (1x1, 2x1,
 *        2x2, ...), per-tile header + fps overlay and selection border.
 *        Click a tile to select its session (drives Controls/Qualify);
 *        double-click to solo it, double-click again to return to the grid.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_PREVIEW_PANEL_HPP
#define XMCAM_UI_PREVIEW_PANEL_HPP

#include <memory>
#include <string>
#include <unordered_map>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"
#include "xmcam/core/util/rate_counter.hpp"
#include "xmcam/ui/frame_texture.hpp"
#include "xmcam/ui/yuv_converter.hpp"

namespace xmotion {

class PreviewPanel : public quickviz::Panel {
 public:
  explicit PreviewPanel(AppController* app);
  void Draw() override;

 private:
  // Per-session GPU upload state (textures are render-thread resources).
  struct Tile {
    FrameTexture texture;  // packed formats (+ CPU YUYV fallback)
    YuvConverter yuv;      // GPU I420/NV12 -> RGBA
    unsigned display_tex = 0;
    RateCounter display_rate;
    uint64_t frames_shown = 0;
    int w = 0;
    int h = 0;
    bool have_frame = false;
  };

  void UpdateTile(AppController::Session& s, Tile& tile);
  // `selectable`: with a single visible tile the selection border is noise
  // (nothing to disambiguate) and clicks don't toggle selection.
  void DrawTile(AppController::Session& s, Tile& tile, ImVec2 cell_min,
                ImVec2 cell_max, bool selectable);

  AppController* app_;
  std::unordered_map<int, std::unique_ptr<Tile>> tiles_;  // by session id
  std::string solo_key_;  // non-empty: show only this session
};

}  // namespace xmotion

#endif  // XMCAM_UI_PREVIEW_PANEL_HPP
