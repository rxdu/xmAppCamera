/*
 * @file control_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/control_panel.hpp"

#include "imgui.h"

namespace xmotion {

ControlPanel::ControlPanel(AppController* app)
    : quickviz::Panel("Controls"), app_(app) {
  this->SetAutoLayout(false);
}

void ControlPanel::ReloadValues(ControlSet* cs) {
  values_.clear();
  for (const auto& c : cs->controls()) {
    int64_t v = c.default_value;
    cs->Get(c.id, &v);
    values_[c.id] = v;
  }
}

void ControlPanel::Draw() {
  Begin();
  {
    ControlSet* cs = app_->Controls();
    if (!cs) {
      ImGui::TextDisabled("start a USB camera to tune controls");
      End();
      return;
    }
    // Reload cached values when the control set is rebuilt (device change or
    // hot-plug recovery).
    if (loaded_epoch_ != app_->controls_epoch()) {
      loaded_epoch_ = app_->controls_epoch();
      ReloadValues(cs);
    }

    if (ImGui::Button("Reset to defaults")) {
      for (const auto& c : cs->controls())
        if (!c.IsReadOnly() && !c.IsInactive() && !c.IsDisabled())
          cs->Set(c.id, c.default_value);
      ReloadValues(cs);
    }
    ImGui::Separator();

    bool changed = false;
    for (const auto& c : cs->controls()) {
      if (c.IsDisabled()) continue;
      // Scope widget IDs by the control id: names can repeat across control
      // classes on some drivers, which would collide ImGui IDs.
      ImGui::PushID(static_cast<int>(c.id));
      const bool locked = c.IsInactive() || c.IsReadOnly();
      if (locked) ImGui::BeginDisabled();

      int64_t& val = values_[c.id];
      switch (c.type) {
        case ControlType::kBoolean: {
          bool b = val != 0;
          if (ImGui::Checkbox(c.name.c_str(), &b)) {
            val = b ? 1 : 0;
            changed |= cs->Set(c.id, val).ok();
          }
          break;
        }
        case ControlType::kMenu:
        case ControlType::kIntegerMenu: {
          // Find current label.
          const char* cur = "?";
          for (const auto& m : c.menu)
            if (m.value == val) cur = m.label.c_str();
          if (ImGui::BeginCombo(c.name.c_str(), cur)) {
            int mi = 0;
            for (const auto& m : c.menu) {
              ImGui::PushID(mi++);
              if (ImGui::Selectable(m.label.c_str(), m.value == val)) {
                val = m.value;
                changed |= cs->Set(c.id, val).ok();
              }
              ImGui::PopID();
            }
            ImGui::EndCombo();
          }
          break;
        }
        case ControlType::kButton: {
          if (ImGui::Button(c.name.c_str())) cs->Set(c.id, 1);
          break;
        }
        default: {  // integer-like
          int iv = static_cast<int>(val);
          if (ImGui::SliderInt(c.name.c_str(), &iv,
                               static_cast<int>(c.minimum),
                               static_cast<int>(c.maximum))) {
            val = iv;
            changed |= cs->Set(c.id, val).ok();
          }
          break;
        }
      }
      if (locked) ImGui::EndDisabled();
      ImGui::PopID();
    }

    // A write may have flipped dependent controls' active state + values.
    if (changed) ReloadValues(cs);
  }
  End();
}

}  // namespace xmotion
