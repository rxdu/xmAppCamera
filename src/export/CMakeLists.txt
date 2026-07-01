# Export: ConfigWriter / ConfigReader — serialize a camera source + control
# values to the YAML schema in docs/design/config-schema.md.
add_library(xmcam_export
  config_writer.cpp
  config_reader.cpp)

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

add_library(xmcam::export ALIAS xmcam_export)
