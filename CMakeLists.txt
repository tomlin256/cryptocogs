cmake_minimum_required(VERSION 3.15...4.2)
project(CRYPTOCOGS VERSION 0.5.5 LANGUAGES CXX)

# Allow fetched dependencies that declare cmake_minimum_required < 3.5
# (CMake 4.x removed compatibility below 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#-------------------------------------------------------------------------------
# Library options
#-------------------------------------------------------------------------------

option(CRYPTOCOGS_BUILD_KRAKEN    "Build the Kraken exchange adapter"    ON)
option(CRYPTOCOGS_BUILD_BINANCE   "Build the Binance exchange adapter"   ON)
option(CRYPTOCOGS_BUILD_COINBASE  "Build the Coinbase exchange adapter"  ON)
option(CRYPTOCOGS_BUILD_CRYPTOCOM "Build the Crypto.com exchange adapter" ON)
option(CRYPTOCOGS_BUILD_TESTS "Build tests" ON)
option(CRYPTOCOGS_BUILD_BENCHMARKS "Build Kraken performance benchmarks (plan 025)" OFF)

# Dependency rules between the exchange/feature flags, checked at configure time.
if(NOT CRYPTOCOGS_BUILD_KRAKEN AND NOT CRYPTOCOGS_BUILD_BINANCE AND
   NOT CRYPTOCOGS_BUILD_COINBASE AND NOT CRYPTOCOGS_BUILD_CRYPTOCOM)
    message(WARNING
        "All of CRYPTOCOGS_BUILD_KRAKEN, CRYPTOCOGS_BUILD_BINANCE, "
        "CRYPTOCOGS_BUILD_COINBASE and CRYPTOCOGS_BUILD_CRYPTOCOM are OFF — "
        "nothing will be built.")
endif()

# Generate install + package-config rules by default for a top-level build;
# off when cryptocogs is pulled in as a subproject (e.g. via FetchContent), so it
# never pollutes a parent project's install.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_kapi_install_default ON)
else()
    set(_kapi_install_default OFF)
endif()
option(CRYPTOCOGS_INSTALL "Generate install rules and a CMake package config" ${_kapi_install_default})

#-------------------------------------------------------------------------------
# Find OpenSSL
#-------------------------------------------------------------------------------

find_package(OpenSSL REQUIRED)

#-------------------------------------------------------------------------------
# Find CURL
#-------------------------------------------------------------------------------

find_package(CURL REQUIRED)

#-------------------------------------------------------------------------------
# A sensible json library
#
# JSON_Install OFF: nlohmann ships its own install rules; we don't want them
# polluting a cryptocogs install — its headers are vendored explicitly from the
# install block below.
#-------------------------------------------------------------------------------

set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

#-------------------------------------------------------------------------------
# toml++ — header-only TOML parser (plan 021)
#
# Used ONLY inside src/exchange/common/credentials_file.cpp (the shared
# Credentials::from_file loader). It never appears in a public header, so it is a
# pure build-time implementation detail: exchange_common links it
# PRIVATE $<BUILD_INTERFACE:...>, keeping it out of the cryptocogsTargets export
# and needing no vendoring. Install rules are suppressed — same treatment as
# JSON_Install / IXWEBSOCKET_INSTALL / INSTALL_GTEST.
#-------------------------------------------------------------------------------

set(TOMLPLUSPLUS_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

#-------------------------------------------------------------------------------
# Subdirectories
#-------------------------------------------------------------------------------

add_subdirectory(src)

if(CRYPTOCOGS_BUILD_TESTS)
  #-----------------------------------------------------------------------------
  # ixwebsocket — used only by examples + tests (no core library links it), so
  # it is fetched here and kept out of the cryptocogs install via
  # IXWEBSOCKET_INSTALL OFF below (plan 017).
  #-----------------------------------------------------------------------------
  FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v12.0.0
  )
  set(USE_TLS ON CACHE BOOL "" FORCE)
  set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
  # ixwebsocket defaults IXWEBSOCKET_INSTALL ON, which would drop its lib,
  # headers, cmake config and pkgconfig into our prefix. Suppress it — same
  # treatment as INSTALL_GTEST / JSON_Install (plan 017).
  set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ixwebsocket)

  #-----------------------------------------------------------------------------
  # Google Test (INSTALL_GTEST OFF — never install gtest/gmock from here)
  #-----------------------------------------------------------------------------
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_subdirectory(tests)
endif()

#-------------------------------------------------------------------------------
# Performance benchmarks (plan 025 Kraken; plan 027 Binance/Coinbase/Crypto.com)
# — off by default, independent of CRYPTOCOGS_BUILD_TESTS: each adapter's
# benchmark binary needs only that adapter's lib and some shared test fixture
# headers (included directly, not linked), not gtest/ixwebsocket. Per-adapter
# gating happens inside tests/benchmarks/CMakeLists.txt, the same convention
# tests/CMakeLists.txt already uses for unit tests.
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_BENCHMARKS)
  if(NOT CRYPTOCOGS_BUILD_KRAKEN AND NOT CRYPTOCOGS_BUILD_BINANCE AND
     NOT CRYPTOCOGS_BUILD_COINBASE AND NOT CRYPTOCOGS_BUILD_CRYPTOCOM)
    message(WARNING
        "CRYPTOCOGS_BUILD_BENCHMARKS is ON but all of CRYPTOCOGS_BUILD_KRAKEN, "
        "CRYPTOCOGS_BUILD_BINANCE, CRYPTOCOGS_BUILD_COINBASE and "
        "CRYPTOCOGS_BUILD_CRYPTOCOM are OFF — nothing to benchmark; skipping.")
  else()
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googlebenchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG        v1.9.5
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_subdirectory(tests/benchmarks)
  endif()
endif()

#-------------------------------------------------------------------------------
# Install + CMake package config (plan 012)
#
# Lets a downstream project `find_package(cryptocogs)` and link
# `cryptocogs::kraken` / `cryptocogs::binance` against an installed prefix.
# The library targets are added to the cryptocogsTargets export in src/.
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_INSTALL)
  include(CMakePackageConfigHelpers)

  install(EXPORT cryptocogsTargets
          NAMESPACE   cryptocogs::
          FILE        cryptocogsTargets.cmake
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cryptocogs)

  # Public headers — component-gated to match the build flags.
  install(DIRECTORY include/exchange/common
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exchange)
  if(CRYPTOCOGS_BUILD_KRAKEN)
    install(DIRECTORY include/exchange/kraken
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exchange)
  endif()
  if(CRYPTOCOGS_BUILD_BINANCE)
    install(DIRECTORY include/exchange/binance
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exchange)
  endif()
  if(CRYPTOCOGS_BUILD_COINBASE)
    install(DIRECTORY include/exchange/coinbase
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exchange)
  endif()
  if(CRYPTOCOGS_BUILD_CRYPTOCOM)
    install(DIRECTORY include/exchange/cryptocom
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/exchange)
  endif()
  # Vendor the header-only nlohmann_json into our prefix. It is FetchContent'd
  # (a non-installable local target), so it cannot be an export dependency;
  # shipping its headers here makes the installed package self-contained — the
  # public headers' #include <nlohmann/json.hpp> resolves with no separate
  # nlohmann_json install on the consumer side.
  install(DIRECTORY ${nlohmann_json_SOURCE_DIR}/include/nlohmann
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  configure_package_config_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cryptocogsConfig.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/cryptocogsConfig.cmake
      INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cryptocogs)
  write_basic_package_version_file(
      ${CMAKE_CURRENT_BINARY_DIR}/cryptocogsConfigVersion.cmake
      VERSION       ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion)
  install(FILES
          ${CMAKE_CURRENT_BINARY_DIR}/cryptocogsConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/cryptocogsConfigVersion.cmake
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cryptocogs)
endif()
