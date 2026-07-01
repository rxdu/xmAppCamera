# Pipeline: VideoSource interface + V4L2 source (raw capture). GStreamer source
# and the V4L2 MJPEG decode branch compile in only when XMCAM_WITH_GSTREAMER.
set(XMCAM_PIPELINE_SRCS
  v4l2_device.cpp
  v4l2_source.cpp)
if(XMCAM_WITH_GSTREAMER)
  list(APPEND XMCAM_PIPELINE_SRCS
    gst_util.cpp
    gst_source.cpp)
endif()

add_library(xmcam_pipeline ${XMCAM_PIPELINE_SRCS})

target_include_directories(xmcam_pipeline PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  # quickviz core is header-only for DataStream/DoubleBuffer — no GUI needed.
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/quickviz/src/core/include>)

target_link_libraries(xmcam_pipeline PUBLIC xmcam_core Threads::Threads)

if(XMCAM_WITH_GSTREAMER)
  target_link_libraries(xmcam_pipeline PUBLIC PkgConfig::GST)
  target_compile_definitions(xmcam_pipeline PUBLIC XMCAM_WITH_GSTREAMER=1)
endif()

add_library(xmcam::pipeline ALIAS xmcam_pipeline)
