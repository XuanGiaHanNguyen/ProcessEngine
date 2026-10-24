add_executable(pf_engine_tests
  baseline_solvers_test.cpp
  exact_solvers_test.cpp
  game_session_test.cpp
  heuristics_test.cpp
  generator_test.cpp
  json_io_test.cpp
  model_test.cpp
  property_test.cpp
  registry_test.cpp
  rules_test.cpp
  instances_test.cpp
  validator_test.cpp
  version_test.cpp)
target_link_libraries(pf_engine_tests PRIVATE pf_json GTest::gtest_main)
target_compile_definitions(pf_engine_tests PRIVATE
  PF_INSTANCES_DIR="${PROJECT_SOURCE_DIR}/instances")
pf_set_warnings(pf_engine_tests)
# Every test, including the 1000-instance property test, must finish within 2 minutes.
gtest_discover_tests(pf_engine_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 120)
