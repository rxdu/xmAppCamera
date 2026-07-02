# Export: ConfigWriter / ConfigReader (YAML), plus RtspSink (re-export a source
# as RTSP/H.264) when gst-rtsp-server is available.
set(XMCAM_EXPORT_SRCS
  config_writer.cpp
  config_reader.cpp)
if(XMCAM_WITH_GSTREAMER)
  list(APPEND XMCAM_EXPORT_SRCS file_sink.cpp)
endif()
if(XMCAM_WITH_RTSP_SERVER)
  list(APPEND XMCAM_EXPORT_SRCS rtsp_sink.cpp)
endif()
add_library(xmcam_export ${XMCAM_EXPORT_SRCS})

target_include_directories(xmcam_export PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# yaml-cpp 0.7 exports the `yaml-cpp` target; newer packages also expose the
# namespaced `yaml-cpp::yaml-cpp`. Prefer the namespaced one when present.
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
  set(XMCAM_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
  set(XMCAM_YAML_CPP_TARGET yaml-cpp)
endif()

target_link_libraries(xmcam_export PUBLIC xmcam_core ${XMCAM_YAML_CPP_TARGET})

if(XMCAM_WITH_GSTREAMER)
  target_link_libraries(xmcam_export PUBLIC PkgConfig::GST)
endif()
if(XMCAM_WITH_RTSP_SERVER)
  # XMCAM_WITH_RTSP_SERVER is defined globally in the root CMake.
  target_link_libraries(xmcam_export PUBLIC PkgConfig::GST_RTSP)
endif()

add_library(xmcam::export ALIAS xmcam_export)
