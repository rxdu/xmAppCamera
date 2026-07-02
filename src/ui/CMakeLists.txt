add_library(xmcam_ui
  frame_texture.cpp
  yuv_converter.cpp
  main_docking_panel.cpp
  preview_panel.cpp
  device_panel.cpp
  control_panel.cpp
  pipeline_panel.cpp
  qualify_panel.cpp)

target_include_directories(xmcam_ui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# Link quickviz's `viewer` module only (Panel/Viewer/ImGui/glad/GL) — NOT the
# `quickviz` aggregate, which drags in the C++20-only `scene` module we don't
# use. pipeline/control provide the backend types the panels talk to.
target_link_libraries(xmcam_ui PUBLIC xmcam_pipeline xmcam_control
                                       xmcam_qualify viewer)

add_library(xmcam::ui ALIAS xmcam_ui)
