include(FetchContent)

#-------------------------------------------------------------------------------
# backward-cpp (stack traces on crash for example programs)
#-------------------------------------------------------------------------------

FetchContent_Declare(
  backward
  GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
  GIT_TAG        v1.6
)
FetchContent_MakeAvailable(backward)

add_library(example_backward OBJECT examples/backward_init.cpp)
add_backward(example_backward)

#-------------------------------------------------------------------------------
# Find spdlog
#-------------------------------------------------------------------------------

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(spdlog)

#-------------------------------------------------------------------------------
# CLI11
#-------------------------------------------------------------------------------

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(CLI11)

#-------------------------------------------------------------------------------
# Kraken example apps (+ the legacy KAPI reference library they depend on)
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_KRAKEN)
    # Legacy KAPI library (lives in examples/, not installed/exported)
    add_library(kapi STATIC examples/kapi.cpp)
    target_include_directories(kapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_link_libraries(kapi CURL::libcurl OpenSSL::SSL)

    add_executable(private_rest examples/private_rest.cpp)
    target_link_libraries(private_rest kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable(get_balance examples/get_balance.cpp)
    target_link_libraries(get_balance kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (public_rest examples/public_rest.cpp)
    target_link_libraries(public_rest kraken spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (public_ws examples/public_ws.cpp)
    target_link_libraries(public_ws kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (private_ws examples/private_ws.cpp)
    target_link_libraries(private_ws kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (kraken_example examples/kraken_example.cpp)
    target_link_libraries(kraken_example kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (ws_client_example examples/ws_client_example.cpp)
    target_link_libraries(ws_client_example kraken ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward)

    add_executable (rest_client_example examples/rest_client_example.cpp)
    target_link_libraries(rest_client_example kraken spdlog::spdlog CLI11::CLI11 example_backward)
endif()

#-------------------------------------------------------------------------------
# Binance example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_BINANCE)
    add_executable(binance_rest_client_example examples/binance/binance_rest_client_example.cpp)
    target_link_libraries(binance_rest_client_example
        binance spdlog::spdlog CLI11::CLI11 example_backward
    )

    # The generic ExchangeWsClient impl comes from exchange_common, reached
    # transitively via binance's PUBLIC link — no kraken reference.
    add_executable(binance_ws_client_example examples/binance/binance_ws_client_example.cpp)
    target_link_libraries(binance_ws_client_example
        binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )

    add_executable(binance_ws_api_example examples/binance/binance_ws_api_example.cpp)
    target_link_libraries(binance_ws_api_example
        binance ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Coinbase example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_COINBASE)
    add_executable(coinbase_rest_client_example examples/coinbase/coinbase_rest_client_example.cpp)
    target_link_libraries(coinbase_rest_client_example
        coinbase spdlog::spdlog CLI11::CLI11 example_backward
    )

    # The bespoke CoinbaseStreamClient reaches the IWsConnection interface via
    # coinbase's PUBLIC link to exchange_common; the real transport
    # (IxWsConnection) needs ixwebsocket linked here.
    add_executable(coinbase_ws_client_example examples/coinbase/coinbase_ws_client_example.cpp)
    target_link_libraries(coinbase_ws_client_example
        coinbase ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Crypto.com example apps
#-------------------------------------------------------------------------------

if(CRYPTOCOGS_BUILD_CRYPTOCOM)
    add_executable(cryptocom_rest_client_example examples/cryptocom/cryptocom_rest_client_example.cpp)
    target_link_libraries(cryptocom_rest_client_example
        cryptocom spdlog::spdlog CLI11::CLI11 example_backward
    )

    # Reuses the generic ExchangeWsClient (via cryptocom's PUBLIC link to
    # exchange_common); the real transport (IxWsConnection) needs ixwebsocket here.
    add_executable(cryptocom_ws_client_example examples/cryptocom/cryptocom_ws_client_example.cpp)
    target_link_libraries(cryptocom_ws_client_example
        cryptocom ixwebsocket spdlog::spdlog CLI11::CLI11 example_backward
    )
endif()

#-------------------------------------------------------------------------------
# Unit tests
#-------------------------------------------------------------------------------

add_subdirectory(unit)

#-------------------------------------------------------------------------------
# Build-tree naming (plan 028): plain cmake -P scripts, so they run in every tree
#-------------------------------------------------------------------------------

# The platform tag and the name check in cmake/cryptocogs_platform.cmake, against a fixed table.
add_test(NAME test_platform_name
         COMMAND ${CMAKE_COMMAND}
                 -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tree_checks/check_platform.cmake)

# The guard in the root CMakeLists.txt: it configures the real project, with the tests off and the
# two dependencies it still needs taken from the sources this build fetched, so it needs no network
# and compiles nothing.
add_test(NAME test_tree_name
         COMMAND ${CMAKE_COMMAND}
                 -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                 -DSCRATCH_ROOT=${CMAKE_CURRENT_BINARY_DIR}/tree_checks
                 -DNLOHMANN_JSON_SRC=${nlohmann_json_SOURCE_DIR}
                 -DTOMLPLUSPLUS_SRC=${tomlplusplus_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tree_checks/check_tree_name.cmake)

# CMakePresets.json names one tree per kind and platform and says only what makes each name true. It
# reads the file with string(JSON) and asks CMake what it lists for this host. Top level only: a project
# that fetches this library does not run this repo's checks.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_test(NAME test_presets
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -DHOST_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tree_checks/check_presets.cmake)
endif()

#-------------------------------------------------------------------------------
# What the project asks of its builders and promises its consumers (plan 029): plain cmake -P scripts
#-------------------------------------------------------------------------------

# One CMake floor. The root CMakeLists.txt states it, and every other minimum in the checkout and the
# README say the same. Top level only, where the checkout it reads is this project's own.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_test(NAME test_cmake_floor
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_checks/check_cmake_floor.cmake)
endif()

# Linking any library built in src/ compiles the consumer as C++23. The export copies a target's
# INTERFACE_COMPILE_FEATURES to a find_package consumer, and nothing else in the suite notices a library that
# loses its target_compile_features: the top-level CMAKE_CXX_STANDARD still compiles it. The features are read
# by generator expression (transitively, so the script takes the highest), which needs no install, no network
# and no compile. The targets are the directory's own, so a library added to src/ later is held to it too.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    get_property(_cryptocogs_libraries DIRECTORY ${PROJECT_SOURCE_DIR}/src PROPERTY BUILDSYSTEM_TARGETS)
    set(_cryptocogs_target_features "")
    foreach(_library IN LISTS _cryptocogs_libraries)
        string(APPEND _cryptocogs_target_features
               "${_library}=$<JOIN:$<TARGET_PROPERTY:${_library},INTERFACE_COMPILE_FEATURES>,+>|")
    endforeach()
    add_test(NAME test_standard_requirement
             COMMAND ${CMAKE_COMMAND}
                     "-DTARGET_FEATURES=${_cryptocogs_target_features}"
                     -DWANT=cxx_std_23
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_checks/check_standard_requirement.cmake)
endif()

# Every public header refuses to compile below C++23, and says why. The suite builds as C++23, so nothing else
# would notice a guard that stopped working: this preprocesses each header at C++17 and at C++20 and expects
# the guard's message. It runs the compiler itself with -std= and -E, so it is registered for GCC and Clang,
# at the top level. The headers' own dependencies (nlohmann/json, curl, ixwebsocket) are on its include
# path, so that the guard is the only thing that can fail.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    string(REPLACE ";" "|" _cryptocogs_curl_include_dirs "${CURL_INCLUDE_DIRS}")
    add_test(NAME test_standard_guard
             COMMAND ${CMAKE_COMMAND}
                     -DCXX=${CMAKE_CXX_COMPILER}
                     -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
                     "-DEXTRA_INCLUDE_DIRS=${nlohmann_json_SOURCE_DIR}/include|${ixwebsocket_SOURCE_DIR}|${_cryptocogs_curl_include_dirs}"
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/standard_guard
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_checks/check_standard_guard.cmake)
endif()

# Which find_package(cryptocogs <version>) requests the package accepts: SameMajorVersion, so that from 1.0.0 a
# consumer that asks for 0.6 is refused rather than handed a library that needs C++23 and CMake 4.0. It reads
# the version file that CMakeLists.txt writes for the install, so nothing is built or installed, and it exists
# only when CRYPTOCOGS_INSTALL is on. The table is derived from the project's own version.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND CRYPTOCOGS_INSTALL)
    add_test(NAME test_package_version
             COMMAND ${CMAKE_COMMAND}
                     -DVERSION_FILE=${PROJECT_BINARY_DIR}/cryptocogsConfigVersion.cmake
                     -DPROJECT_VERSION=${PROJECT_VERSION}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_checks/check_package_version.cmake)
endif()

#-------------------------------------------------------------------------------
# What CI caches (plan 030): a plain cmake -P script
#-------------------------------------------------------------------------------

# The workflow restores the fetched dependencies under a key that hashes the files pinning them, and on a hit it
# trusts the restored sources completely, so a pin bumped in a file that the key does not hash builds the old version
# in a green run. This holds the key to the checkout: every CMakeLists.txt, *.cmake and *.cmake.in that declares a
# dependency is in it, and every file in it exists. Top level only, where the checkout it reads is this project's own.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_test(NAME test_ci_cache_key
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/ci_checks/check_cache_key.cmake)
endif()

#-------------------------------------------------------------------------------
# What starts CI (plan 031): a plain cmake -P script
#-------------------------------------------------------------------------------

# The workflow does not run for a push or a pull request that changes only the paths in its paths-ignore lists. Those
# must be documentation: an ignored file that a build or a check reads can break in a change that no run looked at.
# This holds the lists to the checkout. Top level only, where the checkout it reads is this project's own.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_test(NAME test_ci_paths
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/ci_checks/check_paths.cmake)
endif()

#-------------------------------------------------------------------------------
# Sanitizers (plan 032): plain cmake -P scripts
#-------------------------------------------------------------------------------

# The CRYPTOCOGS_SANITIZE mode -> options mapping in cmake/cryptocogs_sanitize.cmake. A plain script, so it runs in every
# tree: the option itself only changes the tree it is set in, and a regression in the mapping is invisible until someone
# asks for a sanitizer. Top level only, where the checkout it reads is this project's own.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_test(NAME test_sanitize_flags
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/sanitize_checks/check_sanitize_flags.cmake)
endif()
