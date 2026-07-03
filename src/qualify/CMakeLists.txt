# Qualify: automated checks behind the production-camera qualification
# checklist (control locks, timestamp stability, platform identity, image
# fingerprint) plus YAML/Markdown report writers.
add_library(xmcam_qualify
  frame_tap.cpp
  qual_checks.cpp
  qual_report.cpp
  qual_types.cpp)

target_include_directories(xmcam_qualify PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# yaml-cpp 0.7 exports the `yaml-cpp` target; newer packages also expose the
# namespaced `yaml-cpp::yaml-cpp`. Prefer the namespaced one when present.
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
  set(XMCAM_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
  set(XMCAM_YAML_CPP_TARGET yaml-cpp)
endif()

# xmcam_control transitively brings the pipeline (V4l2Device), core Status
# types and xmBase logging.
target_link_libraries(xmcam_qualify PUBLIC xmcam_control
  ${XMCAM_YAML_CPP_TARGET})

add_library(xmcam::qualify ALIAS xmcam_qualify)
