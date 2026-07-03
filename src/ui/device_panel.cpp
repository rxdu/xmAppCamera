/*
 * @file device_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/device_panel.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "xmcam/ui/widgets.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {
namespace {

std::string NodeName(const std::string& device) {
  const auto slash = device.rfind('/');
  return slash == std::string::npos ? device : device.substr(slash + 1);
}

std::string DeviceLabel(const DeviceInfo& d) {
  return d.card + " (" + NodeName(d.device) + ")";
}

}  // namespace

DevicePanel::DevicePanel(AppController* app)
    : quickviz::Panel("Device"), app_(app) {
  this->SetAutoLayout(false);
  slots_.emplace_back();  // start with one camera block
}

const DeviceInfo* DevicePanel::FindDevice(const std::string& device) const {
  for (const auto& d : app_->devices())
    if (d.device == device) return &d;
  return nullptr;
}

bool DevicePanel::ClaimedByOther(const std::string& device,
                                 int self_index) const {
  for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
    if (i != self_index && slots_[i].device == device) return true;
  return false;
}

bool DevicePanel::DrawSlot(Slot& slot, int index) {
  // Auto-assign the first unclaimed device to a fresh slot.
  if (slot.device.empty() || !FindDevice(slot.device)) {
    for (const auto& d : app_->devices()) {
      if (!ClaimedByOther(d.device, index)) {
        slot.device = d.device;
        break;
      }
    }
  }
  const DeviceInfo* dev = FindDevice(slot.device);
  AppController::Session* session =
      dev ? app_->FindSession(dev->device) : nullptr;
  const bool running = session && session->IsRunning();
  const bool is_selected = dev && app_->selected_key() == dev->device;

  char header[160];
  snprintf(header, sizeof header, "Camera %d - %s%s###slot%d", index + 1,
           dev ? DeviceLabel(*dev).c_str() : "(no device)",
           running ? "  [LIVE]" : "", index);
  if (is_selected)
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.32f, 0.45f, 1.0f));
  const bool open =
      ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
  if (is_selected) ImGui::PopStyleColor();
  if (ImGui::IsItemClicked(0) && dev) app_->Select(dev->device);

  bool keep = true;
  if (!open) return keep;

  ImGui::PushID(index);


  // Device picker: enumerated devices not claimed by another slot.
  FieldLabel("Device");
  if (ImGui::BeginCombo("##device", dev ? DeviceLabel(*dev).c_str() : "-")) {
    int di = 0;
    for (const auto& d : app_->devices()) {
      if (ClaimedByOther(d.device, index)) continue;
      ImGui::PushID(di++);
      if (ImGui::Selectable(DeviceLabel(d).c_str(),
                            slot.device == d.device)) {
        if (slot.device != d.device && running)
          app_->StopSession(slot.device);  // switching device stops old stream
        slot.device = d.device;
        slot.fmt = slot.size = slot.fps = 0;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ItemTip("Which physical camera this block drives (cameras claimed by\n"
          "other blocks are hidden)");

  if (!dev) {
    ImGui::TextDisabled("no unclaimed camera available - plug one in and "
                        "Refresh");
  } else {
    const auto& caps = dev->caps;
    if (slot.fmt >= static_cast<int>(caps.formats.size())) slot.fmt = 0;
    const auto& fmt = caps.formats[slot.fmt];

    FieldLabel("Format");
    if (ImGui::BeginCombo("##format", ToString(fmt.format))) {
      for (int i = 0; i < static_cast<int>(caps.formats.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(ToString(caps.formats[i].format),
                              i == slot.fmt)) {
          slot.fmt = i;
          slot.size = slot.fps = 0;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ItemTip("Pixel format from the camera. MJPEG/H264 reach high fps\n"
            "(decoded on the fly); YUYV is raw but rate-limited by USB");

    if (fmt.sizes.empty()) {
      ImGui::TextDisabled("selected format exposes no frame sizes");
    } else {
      if (slot.size >= static_cast<int>(fmt.sizes.size())) slot.size = 0;
      const auto& sz = fmt.sizes[slot.size];

      char lbl[32];
      snprintf(lbl, sizeof lbl, "%dx%d", sz.width, sz.height);
      FieldLabel("Size");
      if (ImGui::BeginCombo("##size", lbl)) {
        for (int i = 0; i < static_cast<int>(fmt.sizes.size()); ++i) {
          char l2[32];
          snprintf(l2, sizeof l2, "%dx%d", fmt.sizes[i].width,
                   fmt.sizes[i].height);
          ImGui::PushID(i);
          if (ImGui::Selectable(l2, i == slot.size)) {
            slot.size = i;
            slot.fps = 0;
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      double sel_fps = 30.0;
      if (!sz.fps.empty()) {
        if (slot.fps >= static_cast<int>(sz.fps.size())) slot.fps = 0;
        sel_fps = sz.fps[slot.fps];
        char lbl2[24];
        snprintf(lbl2, sizeof lbl2, "%.0f fps", sel_fps);
        FieldLabel("FPS");
        if (ImGui::BeginCombo("##fps", lbl2)) {
          for (int i = 0; i < static_cast<int>(sz.fps.size()); ++i) {
            char l3[24];
            snprintf(l3, sizeof l3, "%.0f fps", sz.fps[i]);
            ImGui::PushID(i);
            if (ImGui::Selectable(l3, i == slot.fps)) slot.fps = i;
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
      }

      // Stateful buttons + Remove.
      const bool dirty =
          running && (session->config.format != fmt.format ||
                      session->config.width != sz.width ||
                      session->config.height != sz.height ||
                      std::fabs(session->config.fps - sel_fps) > 0.1);

      auto start_this = [&] {
        if (Status st = app_->StartV4l2(dev->device, fmt.format, sz.width,
                                        sz.height, sel_fps);
            !st.ok()) {
          XLOG_WARN("start {} failed: {}", dev->device, st.message());
          slot.error = st.message();
        } else {
          slot.error.clear();
        }
      };

      if (!running) {
        if (AccentButton("  Start  ", kBtnStart)) start_this();
        ItemTip("Start streaming with the settings above");
      } else {
        if (dirty) {
          if (AccentButton("  Apply  ", kBtnApply)) start_this();
          ItemTip("Restart the stream with the changed settings");
          ImGui::SameLine();
        }
        if (AccentButton("  Stop  ", kBtnStop))
          app_->StopSession(dev->device);
        ItemTip("Stop this camera's stream");
      }
      ImGui::SameLine();
      // Removing a LIVE camera asks first; an idle block just goes away.
      if (ImGui::Button("Remove")) {
        if (running)
          ImGui::OpenPopup("confirm_remove");
        else
          keep = false;
      }
      ItemTip("Remove this camera block (stops its stream)");
      if (ImGui::BeginPopup("confirm_remove")) {
        ImGui::TextUnformatted("This camera is LIVE. Stop and remove?");
        if (AccentButton(" Stop & remove ", kBtnStop)) {
          keep = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      // Per-camera stats-overlay toggle + right-aligned live status, all on
      // the button row (stats themselves live on the preview tile).
      if (session) {
        ImGui::SameLine();
        ImGui::Checkbox("Stats Overlay", &session->stats_overlay);
        ItemTip("Show this camera's live stats on its preview tile");
      }
      if (running)
        StatusRight(kTextLive, "LIVE");
      else if (slot.error.empty())
        StatusRight(kTextIdle, "IDLE");

      // Below the buttons only what matters: the pending delta and errors
      // (live status sits right-aligned on the button row; stats live on
      // the preview tile overlay).
      session = app_->FindSession(dev->device);
      if (session && session->IsRunning()) {
        if (dirty)
          ImGui::TextColored(kTextPending, "pending: %s %dx%d @%.0f - Apply",
                             ToString(fmt.format), sz.width, sz.height,
                             sel_fps);
      } else if (!slot.error.empty()) {
        StatusLine(kTextError, "ERROR", slot.error.c_str());
      }
    }
  }


  ImGui::PopID();
  return keep;
}

void DevicePanel::Draw() {
  Begin();
  {
    // Auto-refresh the device list every few seconds so a newly plugged
    // camera appears without hunting for the button.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last_scan{};
    if (!enumerated_ || Clock::now() - last_scan > std::chrono::seconds(3)) {
      app_->RefreshDevices();
      last_scan = Clock::now();
      enumerated_ = true;
    }
    if (ImGui::Button("Refresh")) app_->RefreshDevices();
    ItemTip("Re-scan /dev/video* now (the list also refreshes automatically\n"
            "every few seconds)");
    ImGui::SameLine();
    if (AccentButton("+ Add Camera", kBtnStart)) slots_.emplace_back();
    ItemTip("Add another camera block to run a second camera alongside\n"
            "this one - each has its own settings and stream");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu running", app_->RunningCount());
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(slots_.size());) {
      if (DrawSlot(slots_[i], i)) {
        ++i;
      } else {
        // Remove pressed: stop this slot's stream and drop the block.
        if (!slots_[i].device.empty()) app_->StopSession(slots_[i].device);
        slots_.erase(slots_.begin() + i);
      }
    }

  }
  End();
}

}  // namespace xmotion
