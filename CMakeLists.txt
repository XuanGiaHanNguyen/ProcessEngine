cmake_minimum_required(VERSION 3.25)
project(ProcessForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PF_BUILD_TESTS "Build ProcessForge tests" ON)
option(PF_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(PF_BUILD_BENCHMARKS "Build Google Benchmark suites" OFF)
option(PF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

if(PF_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(PF_BUILD_TESTS)
  enable_testing()
  include(GoogleTest)
endif()

add_subdirectory(engine)
add_subdirectory(tools)
