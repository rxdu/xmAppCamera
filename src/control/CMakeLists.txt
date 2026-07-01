# Control: V4L2 control introspection + get/set tuning. Sits on top of the
# pipeline's V4l2Device (RAII fd + ioctl helper) — no capture streaming needed.
add_library(xmcam_control
  control_introspector.cpp
  control_set.cpp)

target_include_directories(xmcam_control PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# V4l2Device lives in xmcam_pipeline; that also transitively brings xmSigma
# (logging) and the core Status types.
target_link_libraries(xmcam_control PUBLIC xmcam_pipeline)

add_library(xmcam::control ALIAS xmcam_control)
