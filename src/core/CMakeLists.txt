add_library(xmcam_core
  pixel_format.cpp
  source_caps.cpp
  result.cpp)

target_include_directories(xmcam_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)

# core uses XLOG (scope_timer.hpp) -> PUBLIC link so consumers get the headers.
target_link_libraries(xmcam_core PUBLIC xmotion::xmSigma Threads::Threads)

add_library(xmcam::core ALIAS xmcam_core)
