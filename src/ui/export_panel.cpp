/*
 * @file export_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/export_panel.hpp"

#include "imgui.h"

#include "xmcam/ui/widgets.hpp"

namespace xmotion {

ExportPanel::ExportPanel(AppController* app)
    : quickviz::Panel("Export"), app_(app) {
  this->SetAutoLayout(false);
}

void ExportPanel::Draw() {
  Begin();
  {
#ifndef XMCAM_WITH_RTSP_SERVER
    ImGui::TextDisabled("built without gst-rtsp-server");
#else
    // Per-session export, targeting the globally-selected session; running
    // exports of other sessions keep serving.
    AppController::Session* sel = app_->selected();
    ImGui::TextWrapped("Re-export the selected source as RTSP/H.264.");
    ImGui::Separator();
    FieldLabel("Source");
    ImGui::TextUnformatted(sel ? sel->label.c_str() : "-");

    if (!sel || !sel->IsRunning()) {
      StatusLine(kTextIdle, "IDLE",
                 "select a running source (preview tile / device block)");
    } else if (!sel->rtsp || !sel->rtsp->running()) {
      FieldLabel("Port");
      ImGui::InputInt("##port", &port_);
      if (AccentButton("  Start export  ", kBtnStart)) {
        Status st = app_->StartRtspExport(*sel, port_, "/cam");
        error_ = st.ok() ? "" : st.message();
      }
      if (!error_.empty()) StatusLine(kTextError, "ERROR", error_.c_str());
    } else {
      if (AccentButton("  Stop export  ", kBtnStop))
        app_->StopRtspExport(*sel);
      else
        StatusLine(kTextLive, "SERVING", sel->rtsp->url().c_str());
    }

    // Overview of every session currently exporting.
    bool any = false;
    for (auto& s : app_->sessions()) {
      if (s->rtsp && s->rtsp->running()) {
        if (!any) {
          ImGui::Separator();
          ImGui::TextDisabled("active exports:");
          any = true;
        }
        ImGui::Bullet();
        ImGui::Text("%s -> %s", s->label.c_str(), s->rtsp->url().c_str());
      }
    }
#endif
  }
  End();
}

}  // namespace xmotion
