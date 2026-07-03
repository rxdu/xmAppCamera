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
namespace {

// "rtsp://user:pass@host:554/x" -> "host" (for the session label).
std::string HostOf(const char* url) {
  std::string s(url);
  auto p = s.find("://");
  if (p != std::string::npos) s = s.substr(p + 3);
  auto at = s.find('@');
  if (at != std::string::npos) s = s.substr(at + 1);
  auto end = s.find_first_of(":/");
  if (end != std::string::npos) s = s.substr(0, end);
  return s;
}

}  // namespace

PipelinePanel::PipelinePanel(AppController* app)
    : quickviz::Panel("Network Stream"), app_(app) {
  this->SetAutoLayout(false);
  AddSlot();  // start with one stream block
}

void PipelinePanel::AddSlot() {
  Slot s;
  s.key = "net" + std::to_string(next_key_++);
  slots_.push_back(std::move(s));
}

std::string PipelinePanel::BuildSimplePipeline(const Slot& slot) {
  // decodebin3 is codec-agnostic (H.264/H.265/MJPEG); hardcoding a
  // depayloader silently produces nothing when the camera's codec differs.
  char head[640];
  if (std::strncmp(slot.url, "rtsp://", 7) == 0) {
    snprintf(head, sizeof head, "rtspsrc location=%s latency=%d", slot.url,
             slot.latency_ms);
  } else {
    snprintf(head, sizeof head, "uridecodebin uri=%s", slot.url);
  }
  return std::string(head) +
         " ! decodebin3 ! videoconvert ! video/x-raw,format=I420 ! "
         "appsink name=sink max-buffers=1 drop=true sync=false";
}

void PipelinePanel::Play(Slot& slot, const std::string& pipeline) {
#ifdef XMCAM_WITH_GSTREAMER
  Status st = app_->StartGst(slot.key, pipeline);
  slot.validate_ok = st.ok();
  slot.validate_msg = st.ok() ? "" : st.message();
  if (st.ok()) {
    if (AppController::Session* gs = app_->FindSession(slot.key)) {
      gs->label = slot.advanced || !slot.url[0]
                      ? slot.key + " (pipeline)"
                      : slot.key + " (" + HostOf(slot.url) + ")";
      if (auto* src = dynamic_cast<GstSource*>(gs->source.get()))
        src->SetAutoReconnect(slot.auto_reconnect);
    }
  }
#else
  (void)slot;
  (void)pipeline;
#endif
}

bool PipelinePanel::DrawSlot(Slot& slot, int index) {
  bool keep = true;
#ifdef XMCAM_WITH_GSTREAMER
  AppController::Session* gs = app_->FindSession(slot.key);
  const bool running = gs && gs->IsRunning();
  const bool is_selected = app_->selected_key() == slot.key;

  char header[128];
  snprintf(header, sizeof header, "Stream %d%s%s###netslot%s", index + 1,
           gs ? (" - " + gs->label).c_str() : "",
           running ? "  [LIVE]" : "", slot.key.c_str());
  if (is_selected)
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.32f, 0.45f, 1.0f));
  const bool open =
      ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
  if (is_selected) ImGui::PopStyleColor();
  if (ImGui::IsItemClicked(0) && gs) app_->Select(slot.key);
  if (!open) return keep;

  ImGui::PushID(slot.key.c_str());

  if (ImGui::RadioButton("Simple", !slot.advanced)) slot.advanced = false;
  ImGui::SameLine();
  if (ImGui::RadioButton("Advanced", slot.advanced)) slot.advanced = true;

  std::string wanted;  // what Play/Apply would start
  if (!slot.advanced) {
    FieldLabel("URL");
    ImGui::InputTextWithHint("##url", "rtsp://user:pass@host:554/stream",
                             slot.url, sizeof slot.url);
    ItemTip("Stream address. Any codec works (H.264/H.265/MJPEG) -\n"
            "the right decoder is picked automatically");
    FieldLabel("Latency");
    ImGui::SliderInt("##latency", &slot.latency_ms, 0, 500, "%d ms");
    ItemTip("Network jitter buffer: lower = less delay, higher = smoother\n"
            "on flaky networks");
    FieldLabel("Reconnect");
    if (ImGui::Checkbox("##reconn", &slot.auto_reconnect)) {
      if (gs)
        if (auto* src = dynamic_cast<GstSource*>(gs->source.get()))
          src->SetAutoReconnect(slot.auto_reconnect);
    }
    ItemTip("Automatically retry (with backoff) when the stream drops");
    wanted = slot.url[0] ? BuildSimplePipeline(slot) : std::string();
  } else {
    ImGui::TextWrapped(
        "Raw GStreamer pipeline ending in 'appsink name=sink':");
    if (ImGui::Button("Template") && slot.url[0])
      snprintf(slot.buffer, sizeof slot.buffer, "%s",
               BuildSimplePipeline(slot).c_str());
    ImGui::InputTextMultiline("##pipeline", slot.buffer, sizeof slot.buffer,
                              ImVec2(-1, ImGui::GetTextLineHeight() * 6));
    if (ImGui::Button("Validate")) {
      Status st = GstSource::Validate(slot.buffer);
      slot.validate_ok = st.ok();
      slot.validate_msg = st.ok() ? "pipeline is valid" : st.message();
    }
    wanted = slot.buffer;
  }

  // Stateful action buttons + Remove.
  const bool dirty = running && !wanted.empty() && gs->pipeline != wanted;
  if (!running) {
    const bool can_play = !wanted.empty();
    if (!can_play) ImGui::BeginDisabled();
    if (AccentButton("  Play  ", kBtnStart)) Play(slot, wanted);
    if (!can_play) ImGui::EndDisabled();
  } else {
    if (dirty) {
      if (AccentButton("  Apply  ", kBtnApply)) Play(slot, wanted);
      ImGui::SameLine();
    }
    if (AccentButton("  Stop  ", kBtnStop)) {
      app_->StopSession(slot.key);
      gs = nullptr;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Remove")) keep = false;

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
        StatusLine(kTextLive, "LIVE", HostOf(slot.url).c_str());
        if (dirty)
          ImGui::TextColored(kTextPending,
                             "pending: settings edited - Apply");
        if (!app_->stats_overlay) DrawSourceStatsBlock(*gs);
        break;
      case GstSource::State::kError:
        StatusLine(kTextError, "ERROR", src->last_error().c_str());
        if (slot.auto_reconnect)
          ImGui::TextColored(kTextPending, "retrying with backoff...");
        break;
      case GstSource::State::kEos:
        StatusLine(kTextPending, "STREAM ENDED",
                   slot.auto_reconnect ? "reconnecting..." : "");
        break;
      default:
        break;
    }
  }
  if (!slot.validate_msg.empty())
    ImGui::TextColored(slot.validate_ok ? kTextLive : kTextError, "%s",
                       slot.validate_msg.c_str());
  ImGui::PopID();
#else
  (void)slot;
  (void)index;
  ImGui::TextDisabled("built without GStreamer support");
#endif
  return keep;
}

void PipelinePanel::Draw() {
  Begin();
  {
#ifndef XMCAM_WITH_GSTREAMER
    ImGui::TextDisabled("built without GStreamer support");
#else
    if (AccentButton("+ Add Stream", kBtnStart)) AddSlot();
    ItemTip("Add another network-stream block (RTSP/UDP/HTTP source)");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(slots_.size());) {
      if (DrawSlot(slots_[i], i)) {
        ++i;
      } else {
        app_->StopSession(slots_[i].key);
        slots_.erase(slots_.begin() + i);
      }
    }
#endif
  }
  End();
}

}  // namespace xmotion
