add_executable(test_core
  test_pixel_format.cpp
  test_source_caps.cpp
  test_rate_counter.cpp)
target_link_libraries(test_core PRIVATE xmcam_core gtest gtest_main)
gtest_discover_tests(test_core)
