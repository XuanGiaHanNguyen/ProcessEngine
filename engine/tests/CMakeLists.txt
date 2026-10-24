add_executable(pf_engine_tests
  version_test.cpp)
target_link_libraries(pf_engine_tests PRIVATE pf_engine GTest::gtest_main)
pf_set_warnings(pf_engine_tests)
gtest_discover_tests(pf_engine_tests DISCOVERY_TIMEOUT 30)
