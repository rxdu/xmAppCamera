cmake_minimum_required(VERSION 3.16)
project(xmAppCamera VERSION 0.1.0 LANGUAGES C CXX)

# C++17: xmSigma's spdlog logging wrapper is incompatible with C++20's
# compile-time fmt format checking. We only use quickviz's `viewer` module
# (not `scene`), so we don't need Yoga/C++20.
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
option(XMCAM_WERROR "Treat warnings as errors in our code (CI gate)" OFF)

# Warning flags for OUR code only (src/, tools/, test/); third_party is exempt.
set(XMCAM_WARNING_FLAGS -Wall -Wextra)
if(XMCAM_WERROR)
  list(APPEND XMCAM_WARNING_FLAGS -Werror)
endif()

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

# Optional: RTSP re-export (Phase 5) needs gst-rtsp-server.
if(XMCAM_WITH_GSTREAMER)
  pkg_check_modules(GST_RTSP IMPORTED_TARGET gstreamer-rtsp-server-1.0)
  if(GST_RTSP_FOUND)
    set(XMCAM_WITH_RTSP_SERVER ON)
    # Global (not per-target): AppController's layout depends on it and the UI
    # includes that header, so every TU must agree.
    add_compile_definitions(XMCAM_WITH_RTSP_SERVER=1)
  else()
    message(STATUS "gstreamer-rtsp-server not found — RTSP re-export disabled "
                   "(install libgstrtspserver-1.0-dev).")
    set(XMCAM_WITH_RTSP_SERVER OFF)
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
  # We link only the `viewer` module (Panel/Viewer/ImGui/glad), which builds on
  # C++17 without Yoga. Auto-layout OFF keeps the `scene` module (C++20-only)
  # out of our dependency graph.
  set(ENABLE_AUTO_LAYOUT OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/quickviz EXCLUDE_FROM_ALL)

  # Treat quickviz headers as system includes: their warnings (unused params
  # in inline handlers etc.) must not trip our -Wall/-Werror when compiled
  # inside our translation units.
  foreach(qv_target viewer core imcore stb glad)
    if(TARGET ${qv_target})
      set_target_properties(${qv_target} PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        $<TARGET_PROPERTY:${qv_target},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
  endforeach()
endif()

add_subdirectory(src)
add_subdirectory(tools)
if(XMCAM_BUILD_TESTS)
  add_subdirectory(test)
endif()

message(STATUS "== xmAppCamera: GUI=${XMCAM_BUILD_GUI} "
               "GStreamer=${XMCAM_WITH_GSTREAMER} Tests=${XMCAM_BUILD_TESTS} ==")
