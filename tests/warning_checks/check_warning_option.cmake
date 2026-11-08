# check_warning_option.cmake — test_warning_option: the warning flags reach the project's own targets, and nothing else (plan
# warnings-as-errors, #47).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DCXX=<compiler> -DCXX_ID=<its CMake id> -DGENERATOR=<generator>
#         -DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir> -DIXWEBSOCKET_SRC=<dir> -DGOOGLETEST_SRC=<dir>
#         -DBACKWARD_SRC=<dir> -DSPDLOG_SRC=<dir> -DCLI11_SRC=<dir> [-DGOOGLEBENCHMARK_SRC=<dir>]
#         -P check_warning_option.cmake
# It configures the real top-level project, as check_sanitize_option.cmake and check_clang_tidy_wiring.cmake do, with the
# dependency sources taken from the ones the running build already has, so it needs no network and compiles nothing, and
# reads the compile_commands.json each tree writes. It is given the running tree's compiler and generator, so that what it
# classifies is what that tree builds. Every tree is made in <scratch>/build/<name>, and the scratch directory is removed at the
# end. The names are not platform trees (no macos or linux in them), so the build-tree guard does not examine them.
#
# What it holds, case by case:
#
#   - top level, no option, and top level with the other value: every unit of this project has the four flags of
#     cmake/cryptocogs_warnings.cmake (the text of the flags is pinned by test_warning_flags alone, as test_sanitize_flags
#     does for the sanitizer) and has -Werror exactly when the option is true; at least one unit each under src/, tests/unit/
#     and tests/examples/, so that a wrong path or a new layout cannot pass by scanning nothing; and no unit of a dependency has
#     -Wpedantic or -Werror=deprecated-declarations, at least one such unit having been seen. Those two stand for the whole
#     list: it is applied by one call, and a dependency that sets -Werror for itself (google benchmark does) cannot be told
#     from a leak. The cache has an empty CMAKE_CXX_FLAGS, and the option holds the value the case set. Nothing is a unit of this
#     project that is under a _deps directory or under the scratch directory.
#   - a subproject: a parent project that adds the checkout, with the tests off and an executable of its own that links
#     cryptocogs::common. No unit of cryptocogs, and not the executable's, has any of the flags; the option is not in the
#     parent's cache; CMAKE_CXX_FLAGS is as the parent left it; no library of this project passes INTERFACE_COMPILE_OPTIONS.
#   - benchmarks, when the running tree has google benchmark (GOOGLEBENCHMARK_SRC): one more tree with them on, and the
#     benchmark units carry the flags like the rest.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR SCRATCH_ROOT CXX CXX_ID GENERATOR NLOHMANN_JSON_SRC TOMLPLUSPLUS_SRC IXWEBSOCKET_SRC
                          GOOGLETEST_SRC BACKWARD_SRC SPDLOG_SRC CLI11_SRC)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DCXX=<compiler> -DCXX_ID=<id> -DGENERATOR=<generator> "
            "-DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir> -DIXWEBSOCKET_SRC=<dir> -DGOOGLETEST_SRC=<dir> "
            "-DBACKWARD_SRC=<dir> -DSPDLOG_SRC=<dir> -DCLI11_SRC=<dir> [-DGOOGLEBENCHMARK_SRC=<dir>] "
            "-P check_warning_option.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_warnings.cmake")

# The flags this compiler gets. A compiler that gets none cannot be held to anything here.
cryptocogs_warning_flags("${CXX_ID}" ON OFF want_flags)
if(NOT want_flags)
    message(STATUS "test_warning_option: skipped, a ${CXX_ID} compiler gets no warning flags")
    return()
endif()

# The default of CRYPTOCOGS_WARNINGS_AS_ERRORS in the root, which this test holds: on (plan warnings-as-errors, D2), so that every
# build is strict, CI's and the sanitizer jobs' included, and no workflow has to ask for it. Changing the root's default without
# this line fails the test, and so does the other way round.
set(DEFAULT_WERROR ON)
if(DEFAULT_WERROR)
    set(OTHER_WERROR OFF)
else()
    set(OTHER_WERROR ON)
endif()

# Fewer units than this and the scan has read nothing worth the name: this project alone has some seventy.
set(min_units 20)

get_filename_component(source_real "${SOURCE_DIR}" REALPATH)
set(scratch "${SCRATCH_ROOT}/warning_option")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}/build")
get_filename_component(scratch_real "${scratch}" REALPATH)

set(failures "")
set(trees 0)

# Configure a project into <scratch>/build/<name>. Sets <prefix>_rc, <prefix>_text (stdout and stderr), <prefix>_db (the tree's
# compile_commands.json, or empty if it wrote none) and <prefix>_cache (its CMakeCache.txt).
function(configure_tree prefix name project_dir)
    set(tree "${scratch}/build/${name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${project_dir}" -B "${tree}" -G "${GENERATOR}"
            "-DCMAKE_CXX_COMPILER=${CXX}"
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=${NLOHMANN_JSON_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${TOMLPLUSPLUS_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_IXWEBSOCKET=${IXWEBSOCKET_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=${GOOGLETEST_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_BACKWARD=${BACKWARD_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_SPDLOG=${SPDLOG_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_CLI11=${CLI11_SRC}"
            ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(db "")
    if(EXISTS "${tree}/compile_commands.json")
        file(READ "${tree}/compile_commands.json" db)
    endif()
    set(cache "")
    if(EXISTS "${tree}/CMakeCache.txt")
        file(READ "${tree}/CMakeCache.txt" cache)
    endif()
    set(${prefix}_rc "${rc}" PARENT_SCOPE)
    set(${prefix}_text "${out}\n${err}" PARENT_SCOPE)
    set(${prefix}_db "${db}" PARENT_SCOPE)
    set(${prefix}_cache "${cache}" PARENT_SCOPE)
    math(EXPR n "${trees} + 1")
    set(trees "${n}" PARENT_SCOPE)
endfunction()

function(expect_configures label prefix)
    if(NOT ${prefix}_rc EQUAL 0)
        string(APPEND failures "\n  ${label}: the configure failed (${${prefix}_rc}): ${${prefix}_text}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The tree's cache holds a whole line, such as CRYPTOCOGS_WARNINGS_AS_ERRORS:BOOL=OFF.
function(expect_cache label prefix line)
    string(FIND "\n${${prefix}_cache}\n" "\n${line}\n" at)
    if(at EQUAL -1)
        string(APPEND failures "\n  ${label}: the cache does not hold '${line}'")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The tree's cache has no entry of that name, whatever its type.
function(expect_no_cache_entry label prefix name)
    string(FIND "\n${${prefix}_cache}" "\n${name}:" at)
    if(NOT at EQUAL -1)
        string(APPEND failures "\n  ${label}: the cache holds ${name}, want it absent")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# Up to eight lines of a list, for a failure message.
function(first_lines list_var out_var)
    list(LENGTH ${list_var} n)
    set(shown "${${list_var}}")
    if(n GREATER 8)
        list(SUBLIST shown 0 8 shown)
    endif()
    list(JOIN shown "\n    " text)
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Read the tree's units. The compile commands are matched whole, between spaces, so that -Werror does not pass for
# -Werror=deprecated-declarations. A unit of this project is under the checkout and neither under a _deps directory nor under the
# scratch directory; anything else is a dependency's. expect_werror: ON/OFF, whether -Werror is wanted on this project's units.
# want_deps: ON if at least one dependency unit must have been seen. ARGN: directories (relative to the checkout) that must each
# hold at least one unit of this project.
function(scan_tree label prefix expect_werror want_deps)
    set(db "${${prefix}_db}")
    if("${db}" STREQUAL "")
        string(APPEND failures
            "\n  ${label}: the tree wrote no compile_commands.json (this test needs a Makefiles or Ninja generator)")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    string(JSON count ERROR_VARIABLE error LENGTH "${db}")
    if(error OR count LESS min_units)
        string(APPEND failures
            "\n  ${label}: compile_commands.json has '${count}' entries (${error}), want at least ${min_units}")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    math(EXPR last "${count} - 1")
    set(own 0)
    set(deps 0)
    set(own_dirs "")
    set(lacking "")
    set(unwanted "")
    set(leaked "")
    foreach(i RANGE ${last})
        string(JSON entry GET "${db}" ${i})
        string(JSON file GET "${entry}" file)
        string(JSON command ERROR_VARIABLE command_error GET "${entry}" command)
        if(command_error)
            list(APPEND lacking "${file}: the entry has no \"command\" (CMake's layout changed?)")
            continue()
        endif()
        get_filename_component(real "${file}" REALPATH)
        string(FIND "${real}" "${source_real}/" in_source)
        string(FIND "${real}" "/_deps/" in_deps)
        string(FIND "${real}" "${scratch_real}/" in_scratch)
        set(padded " ${command} ")
        if(in_source EQUAL 0 AND in_deps EQUAL -1 AND NOT in_scratch EQUAL 0)
            math(EXPR own "${own} + 1")
            file(RELATIVE_PATH rel "${source_real}" "${real}")
            list(APPEND own_dirs "${rel}")
            set(missing "")
            foreach(flag IN LISTS want_flags)
                string(FIND "${padded}" " ${flag} " at)
                if(at EQUAL -1)
                    list(APPEND missing "${flag}")
                endif()
            endforeach()
            string(FIND "${padded}" " -Werror " at)
            if(expect_werror AND at EQUAL -1)
                list(APPEND missing "-Werror")
            elseif(NOT expect_werror AND NOT at EQUAL -1)
                list(APPEND unwanted "${rel}: has -Werror, want none")
            endif()
            if(missing)
                list(JOIN missing ", " missing_text)
                list(APPEND lacking "${rel}: no ${missing_text}")
            endif()
        else()
            math(EXPR deps "${deps} + 1")
            set(has "")
            foreach(flag IN ITEMS -Wpedantic -Werror=deprecated-declarations)
                string(FIND "${padded}" " ${flag} " at)
                if(NOT at EQUAL -1)
                    list(APPEND has "${flag}")
                endif()
            endforeach()
            if(has)
                list(JOIN has ", " has_text)
                list(APPEND leaked "${real}: ${has_text}")
            endif()
        endif()
    endforeach()
    if(own LESS 1)
        string(APPEND failures "\n  ${label}: no unit of this project among the ${count} entries")
    endif()
    foreach(dir IN LISTS ARGN)
        set(in_dir "${own_dirs}")
        list(FILTER in_dir INCLUDE REGEX "^${dir}")
        if(NOT in_dir)
            string(APPEND failures "\n  ${label}: no unit of this project under ${dir} among the ${own} that were found")
        endif()
    endforeach()
    if(want_deps AND deps LESS 1)
        string(APPEND failures "\n  ${label}: no dependency unit among the ${count} entries, so a leak could not be seen")
    endif()
    if(lacking)
        list(LENGTH lacking n)
        first_lines(lacking report)
        string(APPEND failures "\n  ${label}: ${n} of ${own} units of this project miss a flag; the first:\n    ${report}")
    endif()
    if(unwanted)
        list(LENGTH unwanted n)
        first_lines(unwanted report)
        string(APPEND failures "\n  ${label}: ${n} of ${own} units of this project have -Werror; the first:\n    ${report}")
    endif()
    if(leaked)
        list(LENGTH leaked n)
        first_lines(leaked report)
        string(APPEND failures
            "\n  ${label}: ${n} of ${deps} dependency units have this project's flags; the first:\n    ${report}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The parent project's tree: not one unit has any of the flags, and the parent's own executable is among the units.
function(scan_subproject label prefix)
    set(db "${${prefix}_db}")
    if("${db}" STREQUAL "")
        string(APPEND failures
            "\n  ${label}: the tree wrote no compile_commands.json (this test needs a Makefiles or Ninja generator)")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    string(JSON count ERROR_VARIABLE error LENGTH "${db}")
    if(error OR count LESS 5)
        string(APPEND failures "\n  ${label}: compile_commands.json has '${count}' entries (${error}), want at least 5")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    math(EXPR last "${count} - 1")
    set(found_consumer FALSE)
    set(flagged "")
    foreach(i RANGE ${last})
        string(JSON entry GET "${db}" ${i})
        string(JSON file GET "${entry}" file)
        string(JSON command ERROR_VARIABLE command_error GET "${entry}" command)
        if(command_error)
            list(APPEND flagged "${file}: the entry has no \"command\" (CMake's layout changed?)")
            continue()
        endif()
        if("${file}" MATCHES "/consumer\\.cpp$")
            set(found_consumer TRUE)
        endif()
        set(padded " ${command} ")
        set(has "")
        foreach(flag IN LISTS want_flags ITEMS -Werror)
            string(FIND "${padded}" " ${flag} " at)
            if(NOT at EQUAL -1)
                list(APPEND has "${flag}")
            endif()
        endforeach()
        if(has)
            list(JOIN has ", " has_text)
            list(APPEND flagged "${file}: ${has_text}")
        endif()
    endforeach()
    if(NOT found_consumer)
        string(APPEND failures "\n  ${label}: the parent's own executable is not among the ${count} units")
    endif()
    if(flagged)
        list(LENGTH flagged n)
        first_lines(flagged report)
        string(APPEND failures "\n  ${label}: ${n} of ${count} units have flags of this project; the first:\n    ${report}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── top level, no option ───────────────────────────────────────────────────────────────────────────
configure_tree(a no-option "${SOURCE_DIR}")
expect_configures("no option" a)
scan_tree("no option" a ${DEFAULT_WERROR} ON src/ tests/unit/ tests/examples/)
expect_cache("no option" a "CRYPTOCOGS_WARNINGS_AS_ERRORS:BOOL=${DEFAULT_WERROR}")
expect_cache("no option" a "CMAKE_CXX_FLAGS:STRING=")

# ── top level, the other value ─────────────────────────────────────────────────────────────────────
configure_tree(b other-value "${SOURCE_DIR}" -DCRYPTOCOGS_WARNINGS_AS_ERRORS=${OTHER_WERROR})
expect_configures("=${OTHER_WERROR}" b)
scan_tree("=${OTHER_WERROR}" b ${OTHER_WERROR} ON src/ tests/unit/ tests/examples/)
expect_cache("=${OTHER_WERROR}" b "CRYPTOCOGS_WARNINGS_AS_ERRORS:BOOL=${OTHER_WERROR}")
expect_cache("=${OTHER_WERROR}" b "CMAKE_CXX_FLAGS:STRING=")

# ── a subproject ───────────────────────────────────────────────────────────────────────────────────
file(MAKE_DIRECTORY "${scratch}/parent")
file(WRITE "${scratch}/parent/consumer.cpp" "int main() { return 0; }\n")
file(WRITE "${scratch}/parent/CMakeLists.txt.in" [=[
cmake_minimum_required(VERSION 4.0)
project(parent LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(flags_before "${CMAKE_CXX_FLAGS}")

add_subdirectory("@SOURCE_DIR@" cryptocogs)

# A consumer's compile flags are its own: the project's warning flags must reach it neither through the cache nor through the
# libraries it links.
if(NOT "${CMAKE_CXX_FLAGS}" STREQUAL "${flags_before}")
    message(FATAL_ERROR "cryptocogs changed its parent's CMAKE_CXX_FLAGS from '${flags_before}' to '${CMAKE_CXX_FLAGS}'")
endif()
if(DEFINED CACHE{CRYPTOCOGS_WARNINGS_AS_ERRORS})
    message(FATAL_ERROR "cryptocogs put CRYPTOCOGS_WARNINGS_AS_ERRORS in its parent's cache")
endif()
foreach(library IN ITEMS exchange_common exchange_http kraken binance coinbase cryptocom)
    if(TARGET ${library})
        get_target_property(options ${library} INTERFACE_COMPILE_OPTIONS)
        if(options)
            message(FATAL_ERROR "${library} passes compile options to its consumers: ${options}")
        endif()
    endif()
endforeach()

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE cryptocogs::common)
]=])
file(READ "${scratch}/parent/CMakeLists.txt.in" parent_text)
string(REPLACE "@SOURCE_DIR@" "${SOURCE_DIR}" parent_text "${parent_text}")
file(WRITE "${scratch}/parent/CMakeLists.txt" "${parent_text}")
configure_tree(c parent "${scratch}/parent" -DCRYPTOCOGS_BUILD_TESTS=OFF)
expect_configures("subproject" c)
scan_subproject("subproject" c)
expect_no_cache_entry("subproject" c CRYPTOCOGS_WARNINGS_AS_ERRORS)

# ── benchmarks, where the running tree has google benchmark ────────────────────────────────────────
if(DEFINED GOOGLEBENCHMARK_SRC AND NOT "${GOOGLEBENCHMARK_SRC}" STREQUAL "")
    if(NOT EXISTS "${GOOGLEBENCHMARK_SRC}/CMakeLists.txt")
        string(APPEND failures "\n  benchmarks: GOOGLEBENCHMARK_SRC '${GOOGLEBENCHMARK_SRC}' is not google benchmark's sources")
    else()
        configure_tree(d benchmarks "${SOURCE_DIR}" -DCRYPTOCOGS_BUILD_BENCHMARKS=ON
            "-DFETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK=${GOOGLEBENCHMARK_SRC}")
        expect_configures("benchmarks" d)
        scan_tree("benchmarks" d ${DEFAULT_WERROR} ON src/ tests/unit/ tests/examples/ tests/benchmarks/)
    endif()
endif()

file(REMOVE_RECURSE "${scratch}")

if(failures)
    message(FATAL_ERROR "test_warning_option failed:${failures}")
endif()
message(STATUS "test_warning_option: ok (${trees} trees: the flags are on every unit of this project and on no dependency's, "
               "-Werror follows the option, and a subproject keeps its own flags)")
