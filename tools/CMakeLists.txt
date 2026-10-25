# Command-line tools built on the engine. They do file I/O, so they live outside engine/.
add_executable(pf_compare compare/main.cpp)
target_link_libraries(pf_compare PRIVATE pf_json)
pf_set_warnings(pf_compare)

add_executable(pf_factory factory/main.cpp)
target_link_libraries(pf_factory PRIVATE pf_json)
pf_set_warnings(pf_factory)

if(PF_BUILD_TESTS)
  add_test(NAME pf_compare.smoke
    COMMAND pf_compare --count 5 --size small --time-limit 500
            --csv ${CMAKE_CURRENT_BINARY_DIR}/smoke.csv)
  add_test(NAME pf_compare.instances
    COMMAND pf_compare --solvers greedy,greedy_potential --instances ${PROJECT_SOURCE_DIR}/instances
            --csv ${CMAKE_CURRENT_BINARY_DIR}/instances.csv)
  add_test(NAME pf_compare.rejects_unknown_solver COMMAND pf_compare --solvers nope)
  set_tests_properties(pf_compare.rejects_unknown_solver PROPERTIES WILL_FAIL TRUE)
  add_test(NAME pf_factory.levels
    COMMAND pf_factory --time-limit 200 ${PROJECT_SOURCE_DIR}/instances/factory/1-first-stitch.json)
endif()
