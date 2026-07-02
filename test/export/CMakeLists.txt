add_executable(test_export
  test_config_roundtrip.cpp)
target_link_libraries(test_export PRIVATE xmcam_export gtest gtest_main)
gtest_discover_tests(test_export)
if(XMCAM_WITH_GSTREAMER)
  add_executable(test_file_sink test_file_sink.cpp)
  target_link_libraries(test_file_sink PRIVATE xmcam_export gtest gtest_main)
  gtest_discover_tests(test_file_sink)
endif()
