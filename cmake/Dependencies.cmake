# Third-party dependencies, fetched at configure time and pinned to exact versions.
include(FetchContent)

FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SYSTEM)
FetchContent_MakeAvailable(nlohmann_json)

if(PF_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if(PF_BUILD_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW ON
    SYSTEM)
  FetchContent_MakeAvailable(benchmark)
endif()

if(PF_BUILD_SERVER)
  # Crow (HTTP + WebSocket, header-only) on standalone Asio.
  # SOURCE_SUBDIR points at a directory without a CMakeLists.txt, so MakeAvailable only
  # downloads the sources; we wire up the include paths ourselves.
  FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-configure)
  FetchContent_Declare(crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.3.2
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-configure)
  FetchContent_MakeAvailable(asio crow)
  find_package(Threads REQUIRED)
  add_library(pf_crow INTERFACE)
  target_include_directories(pf_crow SYSTEM INTERFACE
    ${crow_SOURCE_DIR}/include ${asio_SOURCE_DIR}/asio/include)
  target_compile_definitions(pf_crow INTERFACE ASIO_STANDALONE)
  target_link_libraries(pf_crow INTERFACE Threads::Threads)
endif()
