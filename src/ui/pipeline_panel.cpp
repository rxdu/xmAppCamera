/*
 * @file pipeline_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/pipeline_panel.hpp"

#include <cstring>

#include "imgui.h"

#include "xmcam/ui/stats_view.hpp"
#include "xmcam/ui/widgets.hpp"

#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/pipeline/gst_source.hpp"
#endif

namespace xmotion {
namespace {
constexpr const char* kPresetRtsp =
    "rtspsrc location=rtsp://127.0.0.1:8554/stream latency=50 ! "
    "rtph264depay ! decodebin3 ! videoconvert ! video/x-raw,format=I420 ! "
    "appsink name=sink max-buffers=1 drop=true sync=false";
constexpr const char* kPresetUdp =
    "udpsrc port=5004 caps=application/x-rtp,media=video,encoding-name=H264,"
    "payload=96,clock-rate=90000 ! rtpjitterbuffer latency=50 ! "
    "rtph264depay ! avdec_h264 ! videoconvert ! video/x-raw,format=I420 ! "
    "appsink name=sink max-buffers=1 drop=true sync=false";
}  // namespace

PipelinePanel::PipelinePanel(AppController* app)
    : quickviz::Panel("Network Stream"), app_(app) {
  this->SetAutoLayout(false);
  buffer_[0] = '\0';
}

void PipelinePanel::Draw() {
  Begin();
  {
#ifndef XMCAM_WITH_GSTREAMER
    ImGui::TextDisabled("built without GStreamer support");
#else
    ImGui::TextWrapped(
        "Compose a GStreamer pipeline ending in 'appsink name=sink'.");
    if (ImGui::Button("RTSP preset"))
      std::snprintf(buffer_, sizeof buffer_, "%s", kPresetRtsp);
    ImGui::SameLine();
    if (ImGui::Button("UDP preset"))
      std::snprintf(buffer_, sizeof buffer_, "%s", kPresetUdp);

    ImGui::InputTextMultiline("##pipeline", buffer_, sizeof buffer_,
                              ImVec2(-1, ImGui::GetTextLineHeight() * 6));

    // Stateful action buttons (mirrors the Device panel):
    //   idle           -> [Play]
    //   playing, clean -> [Stop]
    //   playing, edited-> [Apply] [Stop]
    const bool gst_running =
        app_->active_kind() == AppController::ActiveKind::kGst;
    const bool dirty = gst_running && app_->active_pipeline() != buffer_;

    auto play_current = [&] {
      Status st = app_->StartGst(buffer_);
      validate_ok_ = st.ok();
      validate_msg_ = st.ok() ? "" : st.message();
    };

    if (ImGui::Button("Validate")) {
      Status st = GstSource::Validate(buffer_);
      validate_ok_ = st.ok();
      validate_msg_ = st.ok() ? "valid" : st.message();
    }
    ImGui::SameLine();
    if (!gst_running) {
      if (AccentButton("  Play  ", kBtnStart)) play_current();
    } else {
      if (dirty) {
        if (AccentButton("  Apply  ", kBtnApply)) play_current();
        ImGui::SameLine();
      }
      if (AccentButton("  Stop  ", kBtnStop)) app_->StopSource();
    }

    if (gst_running) {
      ImGui::TextColored(kTextLive, "* playing");
      if (dirty)
        ImGui::TextColored(kTextPending, "pending: pipeline edited - Apply");
      DrawSourceStatsBlock(app_);
    }
    if (!validate_msg_.empty()) {
      ImGui::TextColored(validate_ok_ ? kTextLive : kTextError, "%s",
                         validate_msg_.c_str());
    }
#endif

    ImGui::Separator();
    ImGui::TextWrapped("Re-export the active source as RTSP/H.264:");
#ifdef XMCAM_WITH_RTSP_SERVER
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("port", &export_port_);
    if (!app_->RtspExporting()) {
      if (ImGui::Button("Start RTSP export")) {
        Status st = app_->StartRtspExport(export_port_, "/cam");
        rtsp_msg_ = st.ok() ? "" : st.message();
      }
      if (!rtsp_msg_.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", rtsp_msg_.c_str());
    } else {
      if (ImGui::Button("Stop RTSP export")) app_->StopRtspExport();
      ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", app_->RtspUrl().c_str());
    }
#else
    ImGui::TextDisabled("built without gst-rtsp-server");
#endif
  }
  End();
}

}  // namespace xmotion
