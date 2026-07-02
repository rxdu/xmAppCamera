add_library(xmcam_app_lib app_controller.cpp)
target_include_directories(xmcam_app_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
target_link_libraries(xmcam_app_lib PUBLIC xmcam_pipeline xmcam_control xmcam_export)
add_library(xmcam::app ALIAS xmcam_app_lib)

add_executable(xmAppCamera main.cpp)
target_link_libraries(xmAppCamera PRIVATE xmcam_app_lib xmcam_ui viewer)
