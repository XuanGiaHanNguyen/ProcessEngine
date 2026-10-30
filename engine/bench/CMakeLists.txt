add_executable(pf_bench solver_bench.cpp)
target_link_libraries(pf_bench PRIVATE pf_json benchmark::benchmark)
target_compile_definitions(pf_bench PRIVATE PF_INSTANCES_DIR="${PROJECT_SOURCE_DIR}/instances")
