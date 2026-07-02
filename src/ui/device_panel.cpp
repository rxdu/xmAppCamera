/*
 * @file device_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/device_panel.hpp"

#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "xmcam/ui/stats_view.hpp"
#include "xmcam/ui/widgets.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

std::string NodeName(const std::string& device) {
  const auto slash = device.rfind('/');
  return slash == std::string::npos ? device : device.substr(slash + 1);
}

}  // namespace

DevicePanel::DevicePanel(AppController* app)
    : quickviz::Panel("Device"), app_(app) {
  this->SetAutoLayout(false);
}

void DevicePanel::DrawDeviceBlock(const DeviceInfo& dev, int index) {
  AppController::Session* session = app_->FindSession(dev.device);
  const bool running = session && session->IsRunning();
  const bool is_selected = app_->selected_key() == dev.device;
  Sel& sel = sel_[dev.device];

  // Header: "card (videoN)" with a LIVE marker; selected block tinted.
  char header[160];
  snprintf(header, sizeof header, "%s (%s)%s###dev%d", dev.card.c_str(),
           NodeName(dev.device).c_str(), running ? "  [LIVE]" : "", index);
  if (is_selected)
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.32f, 0.45f, 1.0f));
  const bool open =
      ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
  if (is_selected) ImGui::PopStyleColor();
  if (ImGui::IsItemClicked(0)) app_->Select(dev.device);
  if (!open) return;

  ImGui::PushID(index);
  ImGui::Indent();

  const auto& caps = dev.caps;
  if (caps.formats.empty()) {
    ImGui::TextDisabled("device exposes no formats");
    ImGui::Unindent();
    ImGui::PopID();
    return;
  }
  if (sel.fmt >= static_cast<int>(caps.formats.size())) sel.fmt = 0;
  const auto& fmt = caps.formats[sel.fmt];

  // Format / Size / FPS combos (per device).
  if (ImGui::BeginCombo("Format", ToString(fmt.format))) {
    for (int i = 0; i < static_cast<int>(caps.formats.size()); ++i) {
      ImGui::PushID(i);
      if (ImGui::Selectable(ToString(caps.formats[i].format), i == sel.fmt)) {
        sel.fmt = i;
        sel.size = sel.fps = 0;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  if (fmt.sizes.empty()) {
    ImGui::TextDisabled("selected format exposes no frame sizes");
    ImGui::Unindent();
    ImGui::PopID();
    return;
  }
  if (sel.size >= static_cast<int>(fmt.sizes.size())) sel.size = 0;
  const auto& sz = fmt.sizes[sel.size];

  char lbl[32];
  snprintf(lbl, sizeof lbl, "%dx%d", sz.width, sz.height);
  if (ImGui::BeginCombo("Size", lbl)) {
    for (int i = 0; i < static_cast<int>(fmt.sizes.size()); ++i) {
      char l2[32];
      snprintf(l2, sizeof l2, "%dx%d", fmt.sizes[i].width,
               fmt.sizes[i].height);
      ImGui::PushID(i);
      if (ImGui::Selectable(l2, i == sel.size)) {
        sel.size = i;
        sel.fps = 0;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  double sel_fps = 30.0;
  if (!sz.fps.empty()) {
    if (sel.fps >= static_cast<int>(sz.fps.size())) sel.fps = 0;
    sel_fps = sz.fps[sel.fps];
    char lbl2[24];
    snprintf(lbl2, sizeof lbl2, "%.0f fps", sel_fps);
    if (ImGui::BeginCombo("FPS", lbl2)) {
      for (int i = 0; i < static_cast<int>(sz.fps.size()); ++i) {
        char l3[24];
        snprintf(l3, sizeof l3, "%.0f fps", sz.fps[i]);
        ImGui::PushID(i);
        if (ImGui::Selectable(l3, i == sel.fps)) sel.fps = i;
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
  }

  // Stateful buttons: idle [Start] / clean [Stop] / dirty [Apply][Stop].
  const bool dirty =
      running && (session->config.format != fmt.format ||
                  session->config.width != sz.width ||
                  session->config.height != sz.height ||
                  std::fabs(session->config.fps - sel_fps) > 0.1);

  auto start_this = [&] {
    if (Status st = app_->StartV4l2(dev.device, fmt.format, sz.width,
                                    sz.height, sel_fps);
        !st.ok()) {
      XLOG_WARN("start {} failed: {}", dev.device, st.message());
      sel_error_ = st.message();
    } else {
      sel_error_.clear();
    }
  };

  if (!running) {
    if (AccentButton("  Start  ", kBtnStart)) start_this();
  } else {
    if (dirty) {
      if (AccentButton("  Apply  ", kBtnApply)) start_this();
      ImGui::SameLine();
    }
    if (AccentButton("  Stop  ", kBtnStop)) app_->StopSession(dev.device);
  }

  // Status + live stats for THIS camera.
  session = app_->FindSession(dev.device);  // may have changed above
  if (session && session->IsRunning()) {
    StatusLine(kTextLive, "LIVE", NodeName(dev.device).c_str());
    ImGui::Bullet();
    ImGui::Text("%s %dx%d @ %.0f fps", ToString(session->config.format),
                session->config.width, session->config.height,
                session->config.fps);
    if (dirty)
      ImGui::TextColored(kTextPending, "pending: %s %dx%d @%.0f - Apply",
                         ToString(fmt.format), sz.width, sz.height, sel_fps);
    DrawSourceStatsBlock(*session);
  } else if (!sel_error_.empty() && is_selected) {
    StatusLine(kTextError, "ERROR", sel_error_.c_str());
  } else {
    StatusLine(kTextIdle, "IDLE");
  }

  ImGui::Unindent();
  ImGui::PopID();
}

void DevicePanel::Draw() {
  Begin();
  {
    if (!enumerated_) {
      app_->RefreshDevices();
      enumerated_ = true;
    }
    if (ImGui::Button("Refresh")) app_->RefreshDevices();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu running", app_->RunningCount());
    ImGui::Separator();

    const auto& devices = app_->devices();
    if (devices.empty()) {
      ImGui::TextDisabled("no V4L2 capture devices found");
    } else {
      for (int i = 0; i < static_cast<int>(devices.size()); ++i)
        DrawDeviceBlock(devices[i], i);
    }
  }
  End();
}

}  // namespace xmotion
