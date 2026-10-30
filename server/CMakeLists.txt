# HTTP/WebSocket wrapper around the engine (Crow). All rules and solving live in engine/.
add_library(pf_server_lib
  src/job_manager.cpp
  src/logging.cpp
  src/server.cpp
  src/session_store.cpp)
target_include_directories(pf_server_lib PUBLIC src)
target_link_libraries(pf_server_lib PUBLIC pf_json pf_crow)
pf_set_warnings(pf_server_lib)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC reports a false positive inside Crow's bundled TinySHA1 once it is inlined here.
  target_compile_options(pf_server_lib PRIVATE -Wno-stringop-overflow)
endif()

add_executable(pf_server src/main.cpp)
target_link_libraries(pf_server PRIVATE pf_server_lib)
pf_set_warnings(pf_server)

if(PF_BUILD_TESTS)
  add_subdirectory(tests)
endif()
