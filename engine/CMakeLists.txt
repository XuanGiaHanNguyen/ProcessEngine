# Core engine: pure C++, no web or I/O code.
add_library(pf_engine
  src/version.cpp)
target_include_directories(pf_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pf_engine PUBLIC cxx_std_20)
pf_set_warnings(pf_engine)

if(PF_BUILD_TESTS)
  add_subdirectory(tests)
endif()
