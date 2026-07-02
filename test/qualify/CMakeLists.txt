add_executable(test_qualify
  test_qualify.cpp)
target_link_libraries(test_qualify PRIVATE xmcam_qualify gtest gtest_main)
gtest_discover_tests(test_qualify)
