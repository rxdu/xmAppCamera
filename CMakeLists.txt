cmake_minimum_required(VERSION 3.16)
project(xmAppCamera VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(XMCAM_BUILD_GUI "Build the GUI (needs quickviz/GLFW/GLM/Cairo)" ON)
option(XMCAM_WITH_GSTREAMER "Enable GStreamer sources + MJPEG decode" ON)
option(XMCAM_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# --- xmSigma (logging) : submodule with find_package fallback ------------------
if(NOT TARGET xmotion::xmSigma)
  find_package(xmSigma QUIET)
  if(NOT xmSigma_FOUND)
    add_subdirectory(third_party/xmSigma EXCLUDE_FROM_ALL)
  endif()
endif()

# --- googletest ----------------------------------------------------------------
if(XMCAM_BUILD_TESTS)
  enable_testing()
  if(NOT TARGET gtest)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
  endif()
endif()

# --- GStreamer (optional; degrades gracefully) ---------------------------------
if(XMCAM_WITH_GSTREAMER)
  pkg_check_modules(GST IMPORTED_TARGET
                    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
  if(NOT GST_FOUND)
    message(WARNING "GStreamer dev not found — network sources + MJPEG decode "
                    "disabled. Install libgstreamer1.0-dev "
                    "libgstreamer-plugins-base1.0-dev.")
    set(XMCAM_WITH_GSTREAMER OFF)
  endif()
endif()

# --- yaml-cpp ------------------------------------------------------------------
find_package(yaml-cpp REQUIRED)

# --- GUI deps: auto-disable if GLFW absent so the backend still builds ----------
if(XMCAM_BUILD_GUI)
  pkg_check_modules(GLFW glfw3)
  if(NOT GLFW_FOUND)
    message(WARNING "GLFW not found — GUI disabled. Install: "
                    "libglfw3-dev libglm-dev libcairo2-dev libglu1-mesa-dev")
    set(XMCAM_BUILD_GUI OFF)
  endif()
endif()
if(XMCAM_BUILD_GUI AND NOT TARGET quickviz)
  # Keep quickviz on C++17 (its Yoga auto-layout would force C++20).
  set(ENABLE_AUTO_LAYOUT OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/quickviz EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
add_subdirectory(tools)
if(XMCAM_BUILD_TESTS)
  add_subdirectory(test)
endif()

message(STATUS "== xmAppCamera: GUI=${XMCAM_BUILD_GUI} "
               "GStreamer=${XMCAM_WITH_GSTREAMER} Tests=${XMCAM_BUILD_TESTS} ==")
