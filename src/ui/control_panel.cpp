/*
 * @file control_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/control_panel.hpp"

#include <algorithm>

#include "imgui.h"

#include "xmcam/ui/widgets.hpp"
#include "xmsigma/logging/xlogger.hpp"

namespace xmotion {

ControlPanel::ControlPanel(AppController* app)
    : quickviz::Panel("Controls"), app_(app) {
  this->SetAutoLayout(false);
}

void ControlPanel::ReloadValues(ControlSet* cs) {
  values_.clear();
  for (const auto& c : cs->controls()) {
    int64_t v = c.default_value;
    // On read failure the default remains as the display value.
    if (Status st = cs->Get(c.id, &v); !st.ok())
      XLOG_DEBUG("control '{}' read failed: {}", c.name, st.message());
    values_[c.id] = v;
  }
}

void ControlPanel::Draw() {
  Begin();
  {
    // Target: the selected camera, falling back to the first running one —
    // tuning works right after Start with no dropdown step. The combo only
    // appears when there is more than one camera to choose between.
    AppController::Session* target = app_->SelectedCamera();
    int n_cams = 0;
    for (auto& s : app_->sessions())
      if (s->controls) ++n_cams;
    // The combo shows when there is a choice to make OR when the user has
    // deselected (target null) and needs a way back in.
    if (n_cams > 1 || (n_cams >= 1 && !target)) {
      FieldLabel("Camera");
      if (ImGui::BeginCombo("##camera",
                            target ? target->label.c_str() : "-")) {
        for (auto& s : app_->sessions()) {
          if (!s->controls) continue;
          ImGui::PushID(s->id);
          if (ImGui::Selectable(s->label.c_str(), target == s.get()))
            app_->Select(s->key);
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    } else if (target) {
      ImGui::TextDisabled("%s", target->label.c_str());
    }

    ControlSet* cs = app_->Controls();
    if (!cs) {
      if (n_cams >= 1)
        ImGui::TextDisabled(
            "no camera selected - pick one above, or click a preview tile");
      else
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
      for (const auto& c : cs->controls()) {
        if (c.IsReadOnly() || c.IsInactive() || c.IsDisabled()) continue;
        if (Status st = cs->Set(c.id, c.default_value); !st.ok())
          XLOG_WARN("reset '{}' failed: {}", c.name, st.message());
      }
      ReloadValues(cs);
    }
    ImGui::Separator();

    // Left-side labels for the control widgets too. Names vary widely
    // ("Hue" vs "White Balance Temperature"), so size the label column to
    // the longest visible name, capped at ~half the panel width.
    float label_col = 0.0f;
    for (const auto& c : cs->controls())
      if (!c.IsDisabled())
        label_col =
            std::max(label_col, ImGui::CalcTextSize(c.name.c_str()).x);
    // SameLine(x) is window-absolute: offset by the content start (window
    // padding) so the label room is what we actually measured.
    label_col = ImGui::GetCursorPosX() +
                std::min(label_col + 12.0f,
                         ImGui::GetContentRegionAvail().x * 0.55f);

    bool changed = false;
    for (const auto& c : cs->controls()) {
      if (c.IsDisabled()) continue;
      // Scope widget IDs by the control id: names can repeat across control
      // classes on some drivers, which would collide ImGui IDs.
      ImGui::PushID(static_cast<int>(c.id));
      const bool locked = c.IsInactive() || c.IsReadOnly();
      if (locked) ImGui::BeginDisabled();

      FieldLabel(c.name.c_str(), label_col);
      int64_t& val = values_[c.id];
      switch (c.type) {
        case ControlType::kBoolean: {
          bool b = val != 0;
          if (ImGui::Checkbox("##v", &b)) {
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
          if (ImGui::BeginCombo("##v", cur)) {
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
          if (ImGui::Button("Trigger##v")) {
            if (Status st = cs->Set(c.id, 1); !st.ok())
              XLOG_WARN("control '{}' trigger failed: {}", c.name,
                        st.message());
          }
          break;
        }
        default: {  // integer-like
          int iv = static_cast<int>(val);
          if (ImGui::SliderInt("##v", &iv, static_cast<int>(c.minimum),
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
