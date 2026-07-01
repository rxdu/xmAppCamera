add_library(xmcam_ui
  frame_texture.cpp
  main_docking_panel.cpp
  preview_panel.cpp
  device_panel.cpp
  control_panel.cpp
  pipeline_panel.cpp
  stats_panel.cpp)

target_include_directories(xmcam_ui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# quickviz (INTERFACE) provides imgui/glad/viewer headers + GL; pipeline/control
# provide the backend types the panels talk to.
target_link_libraries(xmcam_ui PUBLIC xmcam_pipeline xmcam_control quickviz)

add_library(xmcam::ui ALIAS xmcam_ui)
