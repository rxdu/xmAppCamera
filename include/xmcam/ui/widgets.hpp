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

}  // namespace xmotion

#endif  // XMCAM_UI_WIDGETS_HPP
