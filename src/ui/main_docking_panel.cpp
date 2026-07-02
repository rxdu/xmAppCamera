/*
 * @file main_docking_panel.cpp
 * Copyright (c) 2026 Ruixiang Du (rdu)
 */
#include "xmcam/ui/main_docking_panel.hpp"

#include "imgui.h"
#include "imgui_internal.h"

namespace xmotion {

MainDockingPanel::MainDockingPanel(AppController* app)
    : quickviz::Panel("MainDock"),
      device_(app),
      preview_(app),
      control_(app),
      pipeline_(app),
      qualify_(app) {
  this->SetAutoLayout(false);
  this->SetNoResize(true);
  this->SetNoMove(true);
  this->SetNoTitleBar(true);
  this->SetNoBackground(true);
  this->SetNoBringToFrontOnFocus(true);
  this->SetNoDocking(true);
}

void MainDockingPanel::Draw() {
  // Fill the OS window; rebuilt each frame so maximize/resize just work.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);

  // The invisible dock host stays flush: the global WindowPadding is meant
  // for the visible panels, not the dockspace container.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  Begin();
  ImGui::PopStyleVar();
  {
    dockspace_id_ = ImGui::GetID("xmCamDockSpace");
    ImGui::DockSpace(dockspace_id_, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    if (!layout_initialized_) {
      layout_initialized_ = true;
      ImGui::DockBuilderRemoveNode(dockspace_id_);
      ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspace_id_, vp->WorkSize);

      // Left control sidebar (~24%) | central preview (~76%).
      ImGuiID left, center;
      ImGui::DockBuilderSplitNode(dockspace_id_, ImGuiDir_Left, 0.24f, &left,
                                  &center);
      // Split the sidebar: device/network (info) on top, controls/qualify
      // (actions) below; live stats render inside the owning source panel.
      ImGuiID left_top, left_bottom;
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.42f, &left_top,
                                  &left_bottom);

      ImGui::DockBuilderDockWindow("Device", left_top);
      ImGui::DockBuilderDockWindow("Network Stream", left_top);
      ImGui::DockBuilderDockWindow("Controls", left_bottom);
      ImGui::DockBuilderDockWindow("Qualify", left_bottom);
      ImGui::DockBuilderDockWindow("Preview", center);
      ImGui::DockBuilderFinish(dockspace_id_);
    }
  }
  End();

  // Child windows dock themselves into the nodes above by name.
  device_.Draw();
  pipeline_.Draw();
  control_.Draw();
  qualify_.Draw();
  preview_.Draw();
}

}  // namespace xmotion
