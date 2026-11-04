# check_tree_name.cmake — test_tree_name: the build-tree guard in the root CMakeLists.txt (plans 028 and 032).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir> -P
# It configures the real top-level project, with the tests, benchmarks and install rules off and the
# two dependencies it still fetches taken from the sources the running build already has, so it needs
# no network and compiles nothing. Every tree is made in <scratch>/build/<name> (or, for the case that
# configures a different source, <scratch>/parent_build/<name>), and the scratch directory is removed at
# the end.
#
# The host's own platform is the oracle and the other architecture is the lie, so the test is the same
# on every host. SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of
# the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR SCRATCH_ROOT NLOHMANN_JSON_SRC TOMLPLUSPLUS_SRC)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> "
            "-DTOMLPLUSPLUS_SRC=<dir> -P check_tree_name.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_platform.cmake")

# The host's platform, and the same OS on the other architecture.
cmake_host_system_information(RESULT machine QUERY OS_PLATFORM)
cryptocogs_platform(host "${CMAKE_HOST_SYSTEM_NAME}" "${machine}")
string(REGEX MATCH "^([a-z0-9]+)-(.+)$" ignored "${host}")
set(host_os "${CMAKE_MATCH_1}")
set(host_arch "${CMAKE_MATCH_2}")
if(host_arch STREQUAL "arm64")
    set(other_arch "x86_64")
else()
    set(other_arch "arm64")
endif()
set(other "${host_os}-${other_arch}")

set(scratch "${SCRATCH_ROOT}/tree_name")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}/build")
# The directory the trees are made in; a case that configures a different source sets its own, since a
# tree is for one source directory.
set(base "${scratch}/build")

set(failures "")

# Configure <source> into <base>/<name>. Sets <prefix>_rc, <prefix>_text (stdout and stderr) and
# <prefix>_cache (the tree's CMakeCache.txt, or empty if it has none).
function(configure_tree prefix source name)
    set(tree "${base}/${name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${tree}"
            -DCRYPTOCOGS_BUILD_TESTS=OFF
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=${NLOHMANN_JSON_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${TOMLPLUSPLUS_SRC}"
            ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(cache "")
    if(EXISTS "${tree}/CMakeCache.txt")
        file(READ "${tree}/CMakeCache.txt" cache)
    endif()
    set(${prefix}_rc "${rc}" PARENT_SCOPE)
    set(${prefix}_text "${out}\n${err}" PARENT_SCOPE)
    set(${prefix}_cache "${cache}" PARENT_SCOPE)
endfunction()

function(expect_configures label prefix)
    if(NOT ${prefix}_rc EQUAL 0)
        string(APPEND failures "\n  ${label}: the configure failed (${${prefix}_rc}): ${${prefix}_text}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

function(expect_cache_platform label prefix want)
    string(REGEX MATCH "CRYPTOCOGS_TREE_PLATFORM:INTERNAL=([^\n]*)" ignored "${${prefix}_cache}")
    if(NOT CMAKE_MATCH_1 STREQUAL want)
        string(APPEND failures
            "\n  ${label}: the cache holds platform '${CMAKE_MATCH_1}', want '${want}'")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The configure must fail, and its output must contain every needle.
function(expect_refused label prefix)
    if(${prefix}_rc EQUAL 0)
        string(APPEND failures "\n  ${label}: the configure succeeded, want it refused")
    endif()
    foreach(needle IN LISTS ARGN)
        string(FIND "${${prefix}_text}" "${needle}" at)
        if(at EQUAL -1)
            string(APPEND failures "\n  ${label}: the output does not name '${needle}': ${${prefix}_text}")
        endif()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

function(expect_no_compiler label prefix)
    if("${${prefix}_cache}" MATCHES "(^|\n)CMAKE_CXX_COMPILER:")
        string(APPEND failures
            "\n  ${label}: the cache holds a CMAKE_CXX_COMPILER entry, so the guard ran after project()")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── the truthful tree: configures, records the platform, and configures again ──
configure_tree(a "${SOURCE_DIR}" "release-${host}")
expect_configures("release-${host}" a)
expect_cache_platform("release-${host}" a "${host}")
configure_tree(a2 "${SOURCE_DIR}" "release-${host}")
expect_configures("release-${host}, again" a2)

# ── names that are not platform trees, and a platform tree with no kind ────────
configure_tree(b "${SOURCE_DIR}" "build")
expect_configures("build" b)
expect_cache_platform("build" b "${host}")

configure_tree(c "${SOURCE_DIR}" "${host}")
expect_configures("${host} (no kind)" c)

# ── a sanitizer tree configured for the mode in its name (plan 032) ────────────
# (Each case has a tree of its own: a tree keeps the -D it was configured with.)
configure_tree(s1 "${SOURCE_DIR}" "asan-${host}-gcc13" -DCRYPTOCOGS_SANITIZE=asan)
expect_configures("asan-${host}-gcc13 with asan" s1)

# A kind that only starts like a sanitizer's is not one, and nothing else is required of a sanitizer tree: a
# debug tree may be instrumented.
configure_tree(s2 "${SOURCE_DIR}" "asanx-${host}")
expect_configures("asanx-${host} with nothing" s2)
configure_tree(s3 "${SOURCE_DIR}" "debug-${host}" -DCRYPTOCOGS_SANITIZE=asan)
expect_configures("debug-${host} with asan" s3)

# ── a sanitizer tree that is not configured for its mode ───────────────────────
configure_tree(s4 "${SOURCE_DIR}" "asan-${host}")
expect_refused("asan-${host} with nothing" s4 "asan-${host}" "asan" "CRYPTOCOGS_SANITIZE" "OFF")
expect_no_compiler("asan-${host} with nothing" s4)
configure_tree(s5 "${SOURCE_DIR}" "tsan-${host}" -DCRYPTOCOGS_SANITIZE=asan)
expect_refused("tsan-${host} with asan" s5 "tsan-${host}" "tsan" "asan")
expect_no_compiler("tsan-${host} with asan" s5)

# ── a free-form tree that remembers another platform ───────────────────────────
configure_tree(d "${SOURCE_DIR}" "scratch" "-DCRYPTOCOGS_TREE_PLATFORM=${other}")
expect_refused("a free-form tree seeded with ${other}" d "${other}" "${host}")

# ── the lie: a tree named for the other architecture ───────────────────────────
configure_tree(e "${SOURCE_DIR}" "release-${other}")
expect_refused("release-${other}" e "release-${other}" "${other}" "${host}")
expect_no_compiler("release-${other}" e)

configure_tree(f "${SOURCE_DIR}" "${other}")
expect_refused("${other} (no kind)" f "${other}" "${host}")

# ── an OS and no architecture ──────────────────────────────────────────────────
configure_tree(g "${SOURCE_DIR}" "release-${host_os}")
expect_refused("release-${host_os}" g "release-${host_os}")

# ── a subproject is never examined ─────────────────────────────────────────────
file(MAKE_DIRECTORY "${scratch}/parent")
file(WRITE "${scratch}/parent/CMakeLists.txt"
    "cmake_minimum_required(VERSION 4.0)\n"
    "project(parent LANGUAGES NONE)\n"
    "add_subdirectory(\"${SOURCE_DIR}\" cryptocogs)\n")
set(base "${scratch}/parent_build")
configure_tree(h "${scratch}/parent" "release-${other}")
expect_configures("this repo as a subdirectory, in release-${other}" h)

file(REMOVE_RECURSE "${scratch}")

if(failures)
    message(FATAL_ERROR "test_tree_name failed:${failures}")
endif()
message(STATUS "test_tree_name: ok")
