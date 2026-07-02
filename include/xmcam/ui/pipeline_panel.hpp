/*
 * @file pipeline_panel.hpp
 * @brief Network-stream slot manager (mirrors the camera slots): starts with
 *        one stream block; "+ Add Stream" appends more; each slot has Simple
 *        (URL + latency, codec-agnostic decodebin3 pipeline) and Advanced
 *        (raw pipeline) modes, stateful Play/Apply/Stop + Remove, and live
 *        connection state (CONNECTING/LIVE/ERROR/EOS) from the pipeline bus
 *        with optional auto-reconnect.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_PIPELINE_PANEL_HPP
#define XMCAM_UI_PIPELINE_PANEL_HPP

#include <string>
#include <vector>

#include "viewer/panel.hpp"

#include "xmcam/app/app_controller.hpp"

namespace xmotion {

class PipelinePanel : public quickviz::Panel {
 public:
  explicit PipelinePanel(AppController* app);
  void Draw() override;

 private:
  struct Slot {
    std::string key;  // session key ("net<N>")
    bool advanced = false;
    char url[512] = "";
    int latency_ms = 50;
    bool auto_reconnect = true;
    char buffer[2048] = "";  // advanced raw pipeline
    std::string validate_msg;
    bool validate_ok = false;
  };

  void AddSlot();
  // Returns false if the slot's Remove button was pressed.
  bool DrawSlot(Slot& slot, int index);
  static std::string BuildSimplePipeline(const Slot& slot);
  void Play(Slot& slot, const std::string& pipeline);

  AppController* app_;
  std::vector<Slot> slots_;
  int next_key_ = 1;
};

}  // namespace xmotion

#endif  // XMCAM_UI_PIPELINE_PANEL_HPP
