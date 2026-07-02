/*
 * @file device_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/device_panel.hpp"

#include <cmath>
#include <vector>

#include "imgui.h"

#include "xmcam/ui/stats_view.hpp"
#include "xmcam/ui/widgets.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

DevicePanel::DevicePanel(AppController* app)
    : quickviz::Panel("Device"), app_(app) {
  this->SetAutoLayout(false);
}

void DevicePanel::Draw() {
  Begin();
  {
    if (!enumerated_) {
      app_->RefreshDevices();
      enumerated_ = true;
    }
    if (ImGui::Button("Refresh")) app_->RefreshDevices();

    const auto& devices = app_->devices();
    if (devices.empty()) {
      ImGui::TextDisabled("no V4L2 capture devices found");
      End();
      return;
    }
    if (sel_dev_ >= static_cast<int>(devices.size())) sel_dev_ = 0;

    // Device combo. Identical units share a card name (and possibly a USB
    // serial), so the label carries the node and each item is PushID'd —
    // otherwise ImGui sees duplicate IDs and clicks land on the wrong entry.
    auto device_label = [](const DeviceInfo& d) {
      const auto slash = d.device.rfind('/');
      return d.card + " (" +
             (slash == std::string::npos ? d.device
                                         : d.device.substr(slash + 1)) +
             ")";
    };
    if (ImGui::BeginCombo("Device", device_label(devices[sel_dev_]).c_str())) {
      for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
        const bool selected = (i == sel_dev_);
        ImGui::PushID(i);
        if (ImGui::Selectable(device_label(devices[i]).c_str(), selected)) {
          sel_dev_ = i;
          sel_fmt_ = sel_size_ = sel_fps_ = 0;
        }
        if (selected) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }

    const auto& caps = devices[sel_dev_].caps;
    if (caps.formats.empty()) {
      ImGui::TextDisabled("device exposes no formats");
      End();
      return;
    }
    if (sel_fmt_ >= static_cast<int>(caps.formats.size())) sel_fmt_ = 0;

    // Format combo.
    if (ImGui::BeginCombo("Format",
                          ToString(caps.formats[sel_fmt_].format))) {
      for (int i = 0; i < static_cast<int>(caps.formats.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(ToString(caps.formats[i].format),
                              i == sel_fmt_)) {
          sel_fmt_ = i;
          sel_size_ = sel_fps_ = 0;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }

    const auto& fmt = caps.formats[sel_fmt_];
    if (sel_size_ >= static_cast<int>(fmt.sizes.size())) sel_size_ = 0;

    // Resolution combo.
    if (!fmt.sizes.empty()) {
      char lbl[32];
      snprintf(lbl, sizeof lbl, "%dx%d", fmt.sizes[sel_size_].width,
               fmt.sizes[sel_size_].height);
      if (ImGui::BeginCombo("Size", lbl)) {
        for (int i = 0; i < static_cast<int>(fmt.sizes.size()); ++i) {
          char l2[32];
          snprintf(l2, sizeof l2, "%dx%d", fmt.sizes[i].width,
                   fmt.sizes[i].height);
          ImGui::PushID(i);
          if (ImGui::Selectable(l2, i == sel_size_)) {
            sel_size_ = i;
            sel_fps_ = 0;
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }

      // FPS combo.
      const auto& sz = fmt.sizes[sel_size_];
      if (!sz.fps.empty()) {
        if (sel_fps_ >= static_cast<int>(sz.fps.size())) sel_fps_ = 0;
        char lbl2[24];
        snprintf(lbl2, sizeof lbl2, "%.0f fps", sz.fps[sel_fps_]);
        if (ImGui::BeginCombo("FPS", lbl2)) {
          for (int i = 0; i < static_cast<int>(sz.fps.size()); ++i) {
            char l3[24];
            snprintf(l3, sizeof l3, "%.0f fps", sz.fps[i]);
            ImGui::PushID(i);
            if (ImGui::Selectable(l3, i == sel_fps_)) sel_fps_ = i;
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
      }
    }

    ImGui::Separator();

    // Stateful action buttons: the label IS the running-state indicator.
    //   idle              -> [Start]
    //   running, clean    -> [Stop]
    //   running, dirty    -> [Apply] [Stop]   (selection != active config)
    if (fmt.sizes.empty()) {
      ImGui::TextDisabled("selected format exposes no frame sizes");
      End();
      return;
    }
    const auto& sz = fmt.sizes[sel_size_];
    const double sel_fps = sz.fps.empty() ? 30.0 : sz.fps[sel_fps_];
    const bool v4l2_running =
        app_->active_kind() == AppController::ActiveKind::kV4l2;
    const auto& active = app_->active_config();
    const bool dirty =
        v4l2_running &&
        (active.device != devices[sel_dev_].device ||
         active.format != fmt.format || active.width != sz.width ||
         active.height != sz.height || std::fabs(active.fps - sel_fps) > 0.1);

    auto start_selected = [&] {
      if (Status st = app_->StartV4l2(devices[sel_dev_].device, fmt.format,
                                      sz.width, sz.height, sel_fps);
          !st.ok()) {
        XLOG_WARN("start {} failed: {}", devices[sel_dev_].device,
                  st.message());
      }
    };

    if (!v4l2_running) {
      if (AccentButton("  Start  ", kBtnStart)) start_selected();
      if (app_->active_kind() == AppController::ActiveKind::kGst) {
        ImGui::SameLine();
        ImGui::TextDisabled("(network stream active - Start replaces it)");
      }
    } else {
      if (dirty) {
        if (AccentButton("  Apply  ", kBtnApply)) start_selected();
        ImGui::SameLine();
      }
      if (AccentButton("  Stop  ", kBtnStop)) app_->StopSource();
    }

    // Status under the buttons: short lines so a narrow sidebar never clips.
    // Header + config bullet, pending delta, then the live stats block
    // (the Stats content lives here; the bottom tabs stay action-oriented).
    if (v4l2_running) {
      const auto slash = active.device.rfind('/');
      ImGui::TextColored(kTextLive, "* streaming (%s)",
                         slash == std::string::npos
                             ? active.device.c_str()
                             : active.device.c_str() + slash + 1);
      ImGui::Bullet();
      ImGui::Text("%s %dx%d @%.0f", ToString(active.format), active.width,
                  active.height, active.fps);
      if (dirty)
        ImGui::TextColored(kTextPending, "pending: %s %dx%d @%.0f - Apply",
                           ToString(fmt.format), sz.width, sz.height, sel_fps);
      DrawSourceStatsBlock(app_);
    } else {
      const bool failed =
          app_->status().find("failed") != std::string::npos;
      if (failed)
        ImGui::TextColored(kTextError, "%s", app_->status().c_str());
      else
        ImGui::TextWrapped("%s", app_->status().c_str());
    }
  }
  End();
}

}  // namespace xmotion
