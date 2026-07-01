# Pipeline: VideoSource interface + V4L2 source (raw capture). GStreamer source
# and MJPEG decode branch are added when XMCAM_WITH_GSTREAMER is ON (later).
add_library(xmcam_pipeline
  v4l2_device.cpp
  v4l2_source.cpp)

target_include_directories(xmcam_pipeline PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  # quickviz core is header-only for DataStream/DoubleBuffer — no GUI needed.
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/quickviz/src/core/include>)

target_link_libraries(xmcam_pipeline PUBLIC xmcam_core Threads::Threads)

add_library(xmcam::pipeline ALIAS xmcam_pipeline)
