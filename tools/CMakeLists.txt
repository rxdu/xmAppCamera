add_executable(xmcam_v4l2_smoke v4l2_capture_smoke.cpp)
target_link_libraries(xmcam_v4l2_smoke PRIVATE xmcam_pipeline)

if(XMCAM_WITH_GSTREAMER)
  add_executable(xmcam_gst_smoke gst_stream_smoke.cpp)
  target_link_libraries(xmcam_gst_smoke PRIVATE xmcam_pipeline)
endif()
