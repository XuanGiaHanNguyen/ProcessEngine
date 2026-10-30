# Core engine: pure C++, no web or I/O code, no third-party dependencies.
add_library(pf_engine
  src/bounds.cpp
  src/factory/level.cpp
  src/factory/sim.cpp
  src/game_session.cpp
  src/generator.cpp
  src/model.cpp
  src/registry.cpp
  src/rules.cpp
  src/solver.cpp
  src/solvers/beam_search.cpp
  src/solvers/branch_and_bound.cpp
  src/solvers/brute_force.cpp
  src/solvers/dp.cpp
  src/solvers/greedy.cpp
  src/solvers/lp_relaxation.cpp
  src/solvers/simulated_annealing.cpp
  src/timeline.cpp
  src/validator.cpp
  src/version.cpp)
target_include_directories(pf_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pf_engine PUBLIC cxx_std_20)
pf_set_warnings(pf_engine)

# JSON (de)serialization of engine types. Kept separate so pf_engine stays dependency-free.
add_library(pf_json
  src/factory/json.cpp
  src/json_io.cpp)
target_link_libraries(pf_json PUBLIC pf_engine nlohmann_json::nlohmann_json)
pf_set_warnings(pf_json)

if(PF_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(PF_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
