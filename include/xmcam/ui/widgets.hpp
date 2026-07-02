/*
 * @file widgets.hpp
 * @brief Small shared ImGui helpers for the xmcam panels.
 *
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#ifndef XMCAM_UI_WIDGETS_HPP
#define XMCAM_UI_WIDGETS_HPP

#include "imgui.h"

namespace xmotion {

// Color-accented action button (state is conveyed by label + color).
inline bool AccentButton(const char* label, const ImVec4& base) {
  ImGui::PushStyleColor(ImGuiCol_Button, base);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(base.x * 1.15f, base.y * 1.15f, base.z * 1.15f,
                               base.w));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(base.x * 0.85f, base.y * 0.85f, base.z * 0.85f,
                               base.w));
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(3);
  return clicked;
}

inline const ImVec4 kBtnStart{0.13f, 0.45f, 0.16f, 1.0f};  // green
inline const ImVec4 kBtnStop{0.55f, 0.16f, 0.14f, 1.0f};   // red
inline const ImVec4 kBtnApply{0.62f, 0.44f, 0.10f, 1.0f};  // amber

inline const ImVec4 kTextLive{0.3f, 1.0f, 0.3f, 1.0f};
inline const ImVec4 kTextPending{1.0f, 0.75f, 0.25f, 1.0f};
inline const ImVec4 kTextError{1.0f, 0.4f, 0.4f, 1.0f};
inline const ImVec4 kTextIdle{0.55f, 0.55f, 0.55f, 1.0f};

// Status line with a drawn indicator dot (the bundled font has no U+25CF
// glyph, so the dot is rendered via the draw list): `(o) LABEL  detail`.
inline void StatusLine(const ImVec4& color, const char* label,
                       const char* detail = nullptr) {
  const float h = ImGui::GetTextLineHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float r = h * 0.28f;
  ImGui::GetWindowDrawList()->AddCircleFilled(
      ImVec2(p.x + r + 1.0f, p.y + h * 0.55f), r, ImGui::GetColorU32(color));
  ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, h));
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::TextColored(color, "%s", label);
  if (detail && detail[0]) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", detail);
  }
}

}  // namespace xmotion

#endif  // XMCAM_UI_WIDGETS_HPP
