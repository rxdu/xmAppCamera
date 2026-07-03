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
  snprintf(grp_dir, sizeof grp_dir, "%s", DefaultRecordingDir().c_str());
}

void ExportPanel::DrawRow(AppController::Session& s) {
#ifdef XMCAM_WITH_RTSP_SERVER
  Cfg& cfg = cfg_[s.key];
  if (cfg.port == 0) cfg.port = next_port_++;  // stable per-session default
  if (!cfg.rec_dir[0])
    snprintf(cfg.rec_dir, sizeof cfg.rec_dir, "%s",
             DefaultRecordingDir().c_str());
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
  ItemTip("Network interface to serve on. 0.0.0.0 = every interface;\n"
          "use a specific IP to restrict (e.g. an internal robot network)");
  FieldLabel("Port");
  ImGui::InputInt("##port", &cfg.port);
  ItemTip("TCP port for this stream's RTSP server (each export gets its own)");
  FieldLabel("Path");
  ImGui::InputText("##mount", cfg.mount, sizeof cfg.mount);
  ItemTip("URL path: /cam serves rtsp://<interface>:<port>/cam");
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
  ItemTip("H.264: small files, everyday captures (default)\n"
          "Passthrough: the camera's own bitstream, zero re-encode -\n"
          "  the strongest fidelity for USB cameras in MJPEG/H264 mode\n"
          "Lossless FFV1: mathematically lossless from decoded frames\n"
          "Raw Y4M: bit-exact uncompressed - huge files (~100+ MB/s)");
  FieldLabel("Directory");
  ImGui::InputText("##recdir", cfg.rec_dir, sizeof cfg.rec_dir);
  ItemTip("Output folder (created if missing); files are named\n"
          "<source>_<timestamp> automatically");
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
    Caption("Share or save what a source is capturing: serve it as an RTSP "
            "stream on the network, or record it to a file - both can run "
            "at the same time.");
    ImGui::Separator();

    // --- Synchronized recording: one button, all running sources, shared
    // capture-clock epoch so the files align frame-for-frame. ---
    ImGui::TextUnformatted("Synchronized recording");
    ItemTip("Starts/stops recording of EVERY running source together.\n"
            "USB cameras share one capture-clock epoch, so their files are\n"
            "temporally aligned; a manifest.yaml describes the group.");
    const bool grp_rec = app_->GroupRecording();
    if (grp_rec) ImGui::BeginDisabled();
    FieldLabel("Format");
    const char* kGrpFormats[] = {
        "H.264 MKV (default)",
        "Passthrough MKV (camera bitstream, no re-encode)",
        "Lossless MKV (FFV1)", "Raw Y4M (bit-exact, large)"};
    ImGui::Combo("##grpfmt", &grp_format, kGrpFormats, 4);
    ItemTip("Applied to every source; passthrough falls back to H.264 for\n"
            "sources without a compressed USB bitstream");
    FieldLabel("Directory");
    ImGui::InputText("##grpdir", grp_dir, sizeof grp_dir);
    ItemTip("Each run creates <directory>/<timestamp>/ with one file per\n"
            "source plus manifest.yaml");
    if (grp_rec) ImGui::EndDisabled();

    if (!grp_rec) {
      if (AccentButton("  Start all  ", kBtnStart)) {
        const FileSink::Format fmt =
            grp_format == 0   ? FileSink::Format::kH264Mkv
            : grp_format == 1 ? FileSink::Format::kPassthroughMkv
            : grp_format == 2 ? FileSink::Format::kFfv1Mkv
                              : FileSink::Format::kY4m;
        Status st = app_->StartRecordingGroup(grp_dir, fmt);
        grp_error = st.ok() ? "" : st.message();
      }
      if (!grp_error.empty())
        StatusLine(kTextError, "ERROR", grp_error.c_str());
    } else {
      if (AccentButton("  Stop all  ", kBtnStop)) {
        app_->StopRecordingGroup();
      } else {
        StatusLine(kTextError, "REC", app_->group_dir().c_str());
        for (auto& s : app_->sessions()) {
          if (!s->recorder || !s->recorder->running()) continue;
          const auto rst = s->recorder->stats();
          ImGui::Bullet();
          ImGui::Text("%s: %llu written / %llu dropped", s->label.c_str(),
                      static_cast<unsigned long long>(rst.frames_written),
                      static_cast<unsigned long long>(rst.frames_dropped));
        }
      }
    }
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
