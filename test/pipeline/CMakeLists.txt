set(XMCAM_PIPELINE_TEST_SRCS test_v4l2_enumerate.cpp)
if(XMCAM_WITH_GSTREAMER)
  list(APPEND XMCAM_PIPELINE_TEST_SRCS test_gst_source.cpp)
endif()

add_executable(test_pipeline ${XMCAM_PIPELINE_TEST_SRCS})
target_link_libraries(test_pipeline PRIVATE xmcam_pipeline gtest gtest_main)
gtest_discover_tests(test_pipeline)
