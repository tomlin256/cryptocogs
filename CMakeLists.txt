# 4.0 is the floor: this project is CMake 4 (plan 029, cryptocogs#32). It is a plain minimum, not a
# <min>...<max> range: a range lets an older CMake run the project, and sets every policy up to <max> to NEW,
# which needs its own audit. The 3.15...4.2 range that stood here generated the same files as a plain 4.0
# (plan 029). test_cmake_floor holds every other minimum in the tree, and the README, to this one.
cmake_minimum_required(VERSION 4.0)

#-------------------------------------------------------------------------------
# One build tree per platform (plan 028)
#
# A CMake build tree belongs to one OS and one architecture, and one checkout is built on the Mac
# and in a Linux container that mounts it. Trees are named <kind>-<os>-<arch> and kept under build/
# (build/release-macos-arm64, build/asan-linux-arm64). The guard refuses a tree whose name says it
# is for another platform than this host, an asan-... or tsan-... tree that is not configured for
# that sanitizer (plan 032), and a tree that was first configured on another platform whatever it is
# called; each error names the tree to use. It sits before project(), because the
# host's architecture is not known after it (CMAKE_HOST_SYSTEM_PROCESSOR is empty until project()
# runs) and a refused configure should leave no compiler detection behind. Top-level only, so a
# consumer's tree is never examined.
#-------------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/cryptocogs_platform.cmake)
    cmake_host_system_information(RESULT _cryptocogs_machine QUERY OS_PLATFORM)
    cryptocogs_platform(_cryptocogs_host "${CMAKE_HOST_SYSTEM_NAME}" "${_cryptocogs_machine}")
    get_filename_component(_cryptocogs_tree "${CMAKE_BINARY_DIR}" NAME)
    cryptocogs_tree_problem(_cryptocogs_problem
        "${_cryptocogs_tree}" "${_cryptocogs_host}" "${CRYPTOCOGS_TREE_PLATFORM}" "${CRYPTOCOGS_SANITIZE}")
    if(_cryptocogs_problem)
        message(FATAL_ERROR "${_cryptocogs_problem}")
    endif()
    set(CRYPTOCOGS_TREE_PLATFORM "${_cryptocogs_host}" CACHE INTERNAL
        "The platform this build tree was first configured on (plan 028)")
endif()

project(CRYPTOCOGS VERSION 1.0.1 LANGUAGES CXX)

# Allow fetched dependencies that ask for less than CMake 3.5: CMake 4 removed compatibility below it and
# stops on them. backward-cpp v1.6 says 3.0 and ixwebsocket v12.1.0 says 3.4.1...3.17.2 (plan 029). This stays
# with the floor at 4.0: the floor is this project's, not theirs.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
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
# Sanitizers (opt-in; plan 032, #30)
#
#   -DCRYPTOCOGS_SANITIZE=asan    AddressSanitizer + UndefinedBehaviorSanitizer
#   -DCRYPTOCOGS_SANITIZE=tsan    ThreadSanitizer
#
# The default, OFF, changes nothing. Any other value is a configure error that lists the modes
# (cmake/cryptocogs_sanitize.cmake has the options and the reasons). Use a tree of its own, which the sanitizer
# presets make (build/asan-<os>-<arch>, build/tsan-<os>-<arch>): the options reach every target, so a tree is one
# mode or none.
#
# This sits BEFORE the first FetchContent_MakeAvailable on purpose. A directory's compile and link options are
# copied into each target as it is created, so they reach the fetched dependencies only if they are set first, and
# ThreadSanitizer needs the dependencies instrumented too (a hand-rolled handshake inside uninstrumented code reads
# as a race on our side). A subdirectory added above this block would be quietly uninstrumented:
# test_sanitizer_coverage (tests/sanitize_checks/) fails, in a sanitizer tree, if any compile unit or executable is.
#-------------------------------------------------------------------------------

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cryptocogs_sanitize.cmake)
set(CRYPTOCOGS_SANITIZE "OFF" CACHE STRING "Sanitizer: OFF, asan (address + undefined) or tsan (thread)")
set_property(CACHE CRYPTOCOGS_SANITIZE PROPERTY STRINGS OFF asan tsan)
cryptocogs_sanitizer_flags("${CRYPTOCOGS_SANITIZE}" _cryptocogs_sanitize_compile _cryptocogs_sanitize_link)
if(_cryptocogs_sanitize_compile)
    add_compile_options(${_cryptocogs_sanitize_compile})
    add_link_options(${_cryptocogs_sanitize_link})
    message(STATUS "cryptocogs: sanitizer '${CRYPTOCOGS_SANITIZE}' (${_cryptocogs_sanitize_link})")
endif()

#-------------------------------------------------------------------------------
# clang-tidy (opt-in; plan 035, #34)
#
#   -DCRYPTOCOGS_CLANG_TIDY=clang-tidy    the tool that holds the project's own sources to .clang-tidy: a command on PATH, or a path
#   -DCRYPTOCOGS_CLANG_TIDY_ARGS="..."    extra arguments for every run, a list
#
# The default, empty, leaves the clang_tidy_ tests out and changes nothing. With a tool named, ctest runs it over every unit of the
# project, one test each, and over the fixtures that prove the gate bites; the tests are registered at the end of this file, after
# the last target is made, because the units are read from the targets. A tool that was asked for and cannot be run is an error,
# never a gate that is silently absent, and so is a generator that writes no compile_commands.json, or the tests off. It is
# resolved HERE, before the first dependency is fetched, so that a refusal needs no network (test_clang_tidy_wiring holds that).
# Top level only: in a parent project's tree the variables are inert. cmake/cryptocogs_clang_tidy.cmake has the reasons.
#-------------------------------------------------------------------------------

set(CRYPTOCOGS_CLANG_TIDY "" CACHE STRING
    "clang-tidy command that holds the project's sources to .clang-tidy; empty leaves the clang_tidy_ tests out")
set(CRYPTOCOGS_CLANG_TIDY_ARGS "" CACHE STRING "Extra arguments for every clang-tidy run, a list")
set(_cryptocogs_clang_tidy "")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cryptocogs_clang_tidy.cmake)
    cryptocogs_clang_tidy_tool("${CRYPTOCOGS_CLANG_TIDY}" "${CMAKE_GENERATOR}" _cryptocogs_clang_tidy)
    if(_cryptocogs_clang_tidy AND NOT CRYPTOCOGS_BUILD_TESTS)
        message(FATAL_ERROR
            "CRYPTOCOGS_CLANG_TIDY is set, and the clang_tidy_ tests are registered with the tests: turn "
            "CRYPTOCOGS_BUILD_TESTS on")
    endif()
endif()

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
  #
  # Pinned at v12.1.0: v12.0.0's IXSocketOpenSSL.cpp warns about two unused
  # parameters (-Wunused-parameter) on every build (#36).
  #-----------------------------------------------------------------------------
  FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v12.1.0
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
    GIT_TAG        v1.18.0
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
# clang-tidy tests (opt-in; plan 035, #34) — registered HERE, after the last target of the project is made: the units are
# the sources of the targets (D9), and clang_tidy_units fails if a target is made after this call. Nothing is registered unless
# CRYPTOCOGS_CLANG_TIDY named a tool (above).
#-------------------------------------------------------------------------------

if(_cryptocogs_clang_tidy)
    cryptocogs_clang_tidy_add_tests("${_cryptocogs_clang_tidy}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CRYPTOCOGS_CLANG_TIDY_ARGS}")
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
