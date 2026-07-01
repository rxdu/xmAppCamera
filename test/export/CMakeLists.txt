add_executable(test_export
  test_config_roundtrip.cpp)
target_link_libraries(test_export PRIVATE xmcam_export gtest gtest_main)
gtest_discover_tests(test_export)
