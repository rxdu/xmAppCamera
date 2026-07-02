/*
 * @file pipeline_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/pipeline_panel.hpp"

#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "xmcam/ui/stats_view.hpp"
#include "xmcam/ui/widgets.hpp"

#ifdef XMCAM_WITH_GSTREAMER
#include "xmcam/pipeline/gst_source.hpp"
#endif

namespace xmotion {

PipelinePanel::PipelinePanel(AppController* app)
    : quickviz::Panel("Network Stream"), app_(app) {
  this->SetAutoLayout(false);
  url_[0] = '\0';
  buffer_[0] = '\0';
}

std::string PipelinePanel::BuildSimplePipeline() const {
  // decodebin3 is codec-agnostic (H.264/H.265/MJPEG); hardcoding a
  // depayloader silently produces nothing when the camera's codec differs.
  char head[640];
  if (std::strncmp(url_, "rtsp://", 7) == 0) {
    snprintf(head, sizeof head, "rtspsrc location=%s latency=%d", url_,
             latency_ms_);
  } else {
    snprintf(head, sizeof head, "uridecodebin uri=%s", url_);
  }
  return std::string(head) +
         " ! decodebin3 ! videoconvert ! video/x-raw,format=I420 ! "
         "appsink name=sink max-buffers=1 drop=true sync=false";
}

void PipelinePanel::Play(const std::string& pipeline) {
#ifdef XMCAM_WITH_GSTREAMER
  Status st = app_->StartGst(pipeline);
  validate_ok_ = st.ok();
  validate_msg_ = st.ok() ? "" : st.message();
  if (st.ok()) {
    if (AppController::Session* gs = app_->FindSession("network"))
      if (auto* src = dynamic_cast<GstSource*>(gs->source.get()))
        src->SetAutoReconnect(auto_reconnect_);
  }
#else
  (void)pipeline;
#endif
}

void PipelinePanel::Draw() {
  Begin();
  {
#ifndef XMCAM_WITH_GSTREAMER
    ImGui::TextDisabled("built without GStreamer support");
#else
    AppController::Session* gs = app_->FindSession("network");
    const bool gst_running = gs && gs->IsRunning();

    if (ImGui::RadioButton("Simple", !advanced_)) advanced_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Advanced", advanced_)) advanced_ = true;
    ImGui::Separator();

    std::string wanted;  // what Play/Apply would start
    if (!advanced_) {
      FieldLabel("URL");
      ImGui::InputTextWithHint("##url", "rtsp://user:pass@host:554/stream",
                               url_, sizeof url_);
      FieldLabel("Latency");
      ImGui::SliderInt("##latency", &latency_ms_, 0, 500, "%d ms");
      FieldLabel("Reconnect");
      if (ImGui::Checkbox("##reconn", &auto_reconnect_)) {
        if (gs)
          if (auto* src = dynamic_cast<GstSource*>(gs->source.get()))
            src->SetAutoReconnect(auto_reconnect_);
      }
      wanted = url_[0] ? BuildSimplePipeline() : std::string();
    } else {
      ImGui::TextWrapped(
          "Raw GStreamer pipeline ending in 'appsink name=sink':");
      if (ImGui::Button("Template") && url_[0])
        snprintf(buffer_, sizeof buffer_, "%s",
                 BuildSimplePipeline().c_str());
      ImGui::InputTextMultiline("##pipeline", buffer_, sizeof buffer_,
                                ImVec2(-1, ImGui::GetTextLineHeight() * 6));
      if (ImGui::Button("Validate")) {
        Status st = GstSource::Validate(buffer_);
        validate_ok_ = st.ok();
        validate_msg_ = st.ok() ? "pipeline is valid" : st.message();
      }
      wanted = buffer_;
    }

    // Stateful action buttons: [Play] / [Stop] / [Apply][Stop] when edited.
    const bool dirty = gst_running && !wanted.empty() &&
                       gs->pipeline != wanted;
    if (!advanced_) ImGui::Spacing();
    if (!gst_running) {
      const bool can_play = !wanted.empty();
      if (!can_play) ImGui::BeginDisabled();
      if (AccentButton("  Play  ", kBtnStart)) Play(wanted);
      if (!can_play) ImGui::EndDisabled();
    } else {
      if (dirty) {
        if (AccentButton("  Apply  ", kBtnApply)) Play(wanted);
        ImGui::SameLine();
      }
      if (AccentButton("  Stop  ", kBtnStop)) {
        app_->StopSession("network");
        gs = nullptr;
      }
    }

    // Connection state from the pipeline bus: RTSP failures (bad credentials,
    // unreachable host, codec mismatch) arrive here, not at Play time.
    if (gs && gs->source) {
      auto* src = dynamic_cast<GstSource*>(gs->source.get());
      const auto st = src ? src->state() : GstSource::State::kIdle;
      switch (st) {
        case GstSource::State::kConnecting:
          StatusLine(kTextPending, "CONNECTING",
                     "negotiating with the stream...");
          break;
        case GstSource::State::kPlaying:
          StatusLine(kTextLive, "LIVE", "network stream");
          if (dirty)
            ImGui::TextColored(kTextPending,
                               "pending: settings edited - Apply");
          DrawSourceStatsBlock(*gs);
          break;
        case GstSource::State::kError:
          StatusLine(kTextError, "ERROR", src->last_error().c_str());
          if (auto_reconnect_)
            ImGui::TextColored(kTextPending, "retrying with backoff...");
          break;
        case GstSource::State::kEos:
          StatusLine(kTextPending, "STREAM ENDED",
                     auto_reconnect_ ? "reconnecting..." : "");
          break;
        default:
          break;
      }
    }
    if (!validate_msg_.empty())
      ImGui::TextColored(validate_ok_ ? kTextLive : kTextError, "%s",
                         validate_msg_.c_str());
#endif
  }
  End();
}

}  // namespace xmotion
