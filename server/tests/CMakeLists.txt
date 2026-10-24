add_executable(pf_server_tests server_test.cpp)
target_link_libraries(pf_server_tests PRIVATE pf_server_lib GTest::gtest_main)
target_compile_definitions(pf_server_tests PRIVATE
  PF_INSTANCES_DIR="${PROJECT_SOURCE_DIR}/instances")
gtest_discover_tests(pf_server_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 120)
