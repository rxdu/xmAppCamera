/*
 * @file export_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/export_panel.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "imgui.h"

#include "xmcam/ui/widgets.hpp"

namespace xmotion {

ExportPanel::ExportPanel(AppController* app)
    : quickviz::Panel("Export"), app_(app) {
  this->SetAutoLayout(false);
}

void ExportPanel::DrawRow(AppController::Session& s) {
#ifdef XMCAM_WITH_RTSP_SERVER
  Cfg& cfg = cfg_[s.key];
  if (cfg.port == 0) cfg.port = next_port_++;  // stable per-session default
  const bool serving = s.rtsp && s.rtsp->running();

  char header[160];
  snprintf(header, sizeof header, "%s%s###exp_%s", s.label.c_str(),
           serving ? "  [SERVING]" : "", s.key.c_str());
  if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
    return;

  ImGui::PushID(s.key.c_str());
  ImGui::TextDisabled("RTSP");
  // Settings are frozen while serving; stop the export to change them.
  if (serving) ImGui::BeginDisabled();
  FieldLabel("Interface");
  ImGui::InputTextWithHint("##addr", "0.0.0.0 (all interfaces)", cfg.address,
                           sizeof cfg.address);
  FieldLabel("Port");
  ImGui::InputInt("##port", &cfg.port);
  FieldLabel("Suffix");
  ImGui::InputText("##mount", cfg.mount, sizeof cfg.mount);
  if (serving) ImGui::EndDisabled();

  if (!serving) {
    if (AccentButton("  Enable export  ", kBtnStart)) {
      std::string mount = cfg.mount;
      if (mount.empty() || mount[0] != '/') mount = "/" + mount;
      snprintf(cfg.mount, sizeof cfg.mount, "%s", mount.c_str());
      Status st = app_->StartRtspExport(s, cfg.address, cfg.port, mount);
      cfg.error = st.ok() ? "" : st.message();
    }
    if (!cfg.error.empty()) StatusLine(kTextError, "ERROR", cfg.error.c_str());
  } else {
    if (AccentButton("  Disable export  ", kBtnStop)) {
      app_->StopRtspExport(s);
    } else {
      const bool all_if = std::strcmp(cfg.address, "0.0.0.0") == 0;
      StatusLine(kTextLive, "SERVING", s.rtsp->url().c_str());
      if (all_if)
        ImGui::TextDisabled("bound to all interfaces - reachable via any "
                            "host IP");
    }
  }

  // --- File recording (concurrent with RTSP thanks to the fanout) ---
  ImGui::Spacing();
  ImGui::TextDisabled("Record to file");
  const bool recording = s.recorder && s.recorder->running();
  if (recording) ImGui::BeginDisabled();
  FieldLabel("Format");
  const char* kFormats[] = {"H.264 MKV (default)",
                            "Passthrough MKV (camera bitstream, no re-encode)",
                            "Lossless MKV (FFV1)",
                            "Raw Y4M (bit-exact, large)"};
  ImGui::Combo("##recfmt", &cfg.rec_format, kFormats, 4);
  FieldLabel("Directory");
  ImGui::InputText("##recdir", cfg.rec_dir, sizeof cfg.rec_dir);
  if (recording) ImGui::EndDisabled();

  if (!recording) {
    if (AccentButton("  Start recording  ", kBtnStart)) {
      const FileSink::Format fmt =
          cfg.rec_format == 0   ? FileSink::Format::kH264Mkv
          : cfg.rec_format == 1 ? FileSink::Format::kPassthroughMkv
          : cfg.rec_format == 2 ? FileSink::Format::kFfv1Mkv
                                : FileSink::Format::kY4m;
      // Passthrough saves the camera's own bitstream — only meaningful for
      // USB cameras running a compressed mode.
      if (fmt == FileSink::Format::kPassthroughMkv &&
          !(s.kind == AppController::SourceKind::kV4l2 &&
            IsCompressed(s.config.format))) {
        cfg.rec_error =
            "passthrough needs a USB camera in MJPEG/H264 mode";
        ImGui::PopID();
        return;
      }
      // recordings/<key>_<timestamp>.<ext> — key sanitized for a filename.
      std::string name = s.key;
      for (auto& ch : name)
        if (ch == '/' || ch == ':') ch = '_';
      char ts[32];
      std::time_t t = std::time(nullptr);
      std::strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", std::localtime(&t));
      const std::string path = std::string(cfg.rec_dir) + "/" + name + "_" +
                               ts + "." + FileSink::Extension(fmt);
      Status st = app_->StartRecording(s, path, fmt);
      cfg.rec_error = st.ok() ? "" : st.message();
    }
    if (!cfg.rec_error.empty())
      StatusLine(kTextError, "ERROR", cfg.rec_error.c_str());
  } else {
    if (AccentButton("  Stop recording  ", kBtnStop)) {
      app_->StopRecording(s);
    } else {
      const auto rst = s.recorder->stats();
      StatusLine(kTextError, "REC", s.recorder->path().c_str());
      ImGui::Bullet();
      ImGui::Text("%llu frames written / %llu dropped",
                  static_cast<unsigned long long>(rst.frames_written),
                  static_cast<unsigned long long>(rst.frames_dropped));
    }
  }
  ImGui::PopID();
#else
  (void)s;
#endif
}

void ExportPanel::Draw() {
  Begin();
  {
#ifndef XMCAM_WITH_RTSP_SERVER
    ImGui::TextDisabled("built without gst-rtsp-server");
#else
    ImGui::TextWrapped("Re-export active sources as RTSP/H.264 streams.");
    ImGui::Separator();

    bool any = false;
    for (auto& s : app_->sessions()) {
      if (!s->IsRunning()) continue;
      any = true;
      DrawRow(*s);
    }
    if (!any)
      ImGui::TextDisabled(
          "no active sources - start a camera or network stream first");

    // Prune settings of sessions that no longer exist.
    for (auto it = cfg_.begin(); it != cfg_.end();) {
      it = app_->FindSession(it->first) ? std::next(it) : cfg_.erase(it);
    }
#endif
  }
  End();
}

}  // namespace xmotion
