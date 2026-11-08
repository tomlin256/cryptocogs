# check_clang_tidy_wiring.cmake — test_clang_tidy_wiring: cryptocogs_clang_tidy_add_tests() registers the clang-tidy tests, and
# the root CMakeLists.txt refuses a tool it cannot run (plan clang-tidy-opt-in-test-step, #34).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir> -DIXWEBSOCKET_SRC=<dir>
#         -DGOOGLETEST_SRC=<dir> -DBACKWARD_SRC=<dir> -DSPDLOG_SRC=<dir> -DCLI11_SRC=<dir> -P check_clang_tidy_wiring.cmake
# The *_SRC are the dependency sources the running build already fetched, so that no configure here needs the network. Two
# kinds of configure:
#
#   S  a stub project that includes the real module and calls cryptocogs_clang_tidy_add_tests() over a tree of targets: a
#      library (with a source generated into the build tree), three executables (two of them compile one source, and one is
#      built only when a stub option is on), a canary of the sanitizer trees, four dependencies (made the way FetchContent
#      makes one, under _deps in the build tree; under the build tree without _deps; under a _deps in the checkout; and
#      outside the checkout), and a source that no target compiles. The tool is empty and then `sh`, which is never run, and
#      `ctest --show-only=json-v1` says what was registered, so nothing here needs clang-tidy.
#   R  the real root CMakeLists.txt, in four configures: a tool no host has, a tool with the tests off, the root as a
#      subdirectory of another project, and the gate on over the real targets, where the registration is seen at the root
#      and clang_tidy_units compares it with the compile database that very configure wrote.
#
# The clang-tidy runs themselves, and so what each test's regular expression means, are the business of the gate when it is
# on: the canaries are the proof. What this holds is that the opt-in rule is a rule (empty registers nothing), that the units
# are the targets' sources and no others, and that the tests exist, are named from the paths, are handed the config file, and
# have the properties that make them mean it.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR SCRATCH_ROOT NLOHMANN_JSON_SRC TOMLPLUSPLUS_SRC IXWEBSOCKET_SRC GOOGLETEST_SRC
                         BACKWARD_SRC SPDLOG_SRC CLI11_SRC)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir> "
            "-DIXWEBSOCKET_SRC=<dir> -DGOOGLETEST_SRC=<dir> -DBACKWARD_SRC=<dir> -DSPDLOG_SRC=<dir> -DCLI11_SRC=<dir> "
            "-P check_clang_tidy_wiring.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_clang_tidy.cmake")

set(scratch "${SCRATCH_ROOT}/clang_tidy_wiring")
file(REMOVE_RECURSE "${scratch}")

# A failure is recorded in a global property, because the helpers below call this from inside one another and a variable
# set in a parent scope would reach only the helper's.
function(fail what)
    set_property(GLOBAL APPEND_STRING PROPERTY WIRING_FAILURES "\n  ${what}")
endfunction()

# ── S ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
# The tree the stub registers over. Its build trees are under it, as the real ones are under build/, so that a dependency's
# sources are under the checkout and under the build tree at once.
set(root "${scratch}/root")
set(elsewhere "${scratch}/elsewhere")
file(WRITE "${root}/src/CMakeLists.txt" "add_library(lib_a STATIC a.cpp \"\${CMAKE_BINARY_DIR}/gen_a.cpp\")\n")
file(WRITE "${root}/src/a.cpp" "int a;\n")
file(WRITE "${root}/tests/unit/CMakeLists.txt"
    "add_executable(unit_b b.cpp)\n"
    "add_executable(unit_b_again b.cpp)\n"
    "if(STUB_WITH_C)\n"
    "    add_executable(unit_c c.cpp)\n"
    "endif()\n")
file(WRITE "${root}/tests/unit/b.cpp" "int main() {}\n")
file(WRITE "${root}/tests/unit/c.cpp" "int main() {}\n")
file(WRITE "${root}/tests/unit/orphan.cpp" "int orphan;\n")
file(WRITE "${root}/tests/sanitize_checks/canaries/CMakeLists.txt" "add_executable(canary_d d.cpp)\n")
file(WRITE "${root}/tests/sanitize_checks/canaries/d.cpp" "int main() {}\n")
file(WRITE "${root}/third_party/_deps/y-src/CMakeLists.txt" "add_library(y_lib STATIC y.cpp)\n")
file(WRITE "${root}/third_party/_deps/y-src/y.cpp" "int y;\n")
file(WRITE "${elsewhere}/CMakeLists.txt" "add_library(e_lib STATIC e.cpp)\n")
file(WRITE "${elsewhere}/e.cpp" "int e;\n")
# The real fixtures and the real units script, which the module reads from <root>/tests/clang_tidy_checks.
file(COPY "${SOURCE_DIR}/tests/clang_tidy_checks" DESTINATION "${root}/tests")

file(CONFIGURE OUTPUT "${root}/CMakeLists.txt" @ONLY CONTENT [=[
cmake_minimum_required(VERSION 4.0)
project(stub LANGUAGES CXX)
enable_testing()
include("@SOURCE_DIR@/cmake/cryptocogs_clang_tidy.cmake")
option(STUB_WITH_C "Build the optional unit" ON)
# A source generated into the build tree, which the project does not own.
file(WRITE "${CMAKE_BINARY_DIR}/gen_a.cpp" "int gen;\n")
add_subdirectory(src)
add_subdirectory(tests/unit)
add_subdirectory(tests/sanitize_checks/canaries)
# A dependency the way FetchContent makes one: its sources and its build directory are under _deps in the build tree.
file(WRITE "${CMAKE_BINARY_DIR}/_deps/x-src/CMakeLists.txt" "add_library(x_lib STATIC x.cpp)\n")
file(WRITE "${CMAKE_BINARY_DIR}/_deps/x-src/x.cpp" "int x;\n")
add_subdirectory("${CMAKE_BINARY_DIR}/_deps/x-src" "${CMAKE_BINARY_DIR}/_deps/x-build")
# One under the build tree and not under a _deps: only where it is tells it from the project's own.
file(WRITE "${CMAKE_BINARY_DIR}/gen/g-src/CMakeLists.txt" "add_library(g_lib STATIC g.cpp)\n")
file(WRITE "${CMAKE_BINARY_DIR}/gen/g-src/g.cpp" "int g;\n")
add_subdirectory("${CMAKE_BINARY_DIR}/gen/g-src" "${CMAKE_BINARY_DIR}/gen/g-build")
# One under a _deps in the checkout and outside the build tree (a source directory named into another tree's _deps).
add_subdirectory(third_party/_deps/y-src)
# One outside the checkout altogether (a source directory named for a dependency, or a local clone).
add_subdirectory("@elsewhere@" elsewhere-build)
cryptocogs_clang_tidy_add_tests("${TOOL}" "${CMAKE_CURRENT_SOURCE_DIR}" "--extra-one;--extra-two=2")
]=])

# Configure the stub with <tool> into its own tree and read what ctest lists. Sets <prefix>_json and <prefix>_build. Further
# arguments are passed to the configure.
function(register prefix tool)
    set(build "${root}/build/${prefix}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${root}" -B "${build}" "-DTOOL=${tool}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${status}" STREQUAL "0")
        fail("the stub project with tool '${tool}' did not configure:\n${out}\n${err}")
        return()
    endif()
    execute_process(
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${build}" --show-only=json-v1
        RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE err)
    if(NOT "${status}" STREQUAL "0")
        fail("ctest could not list the stub project's tests with tool '${tool}':\n${err}")
        return()
    endif()
    set(${prefix}_json "${json}" PARENT_SCOPE)
    set(${prefix}_build "${build}" PARENT_SCOPE)
endfunction()

# The names of the registered tests, sorted.
function(test_names json out)
    string(JSON count LENGTH "${json}" tests)
    set(names "")
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON name GET "${json}" tests ${i} name)
            list(APPEND names "${name}")
        endforeach()
    endif()
    list(SORT names)
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

# The index of the test called <name> in the JSON, or -1.
function(test_index json name out)
    string(JSON count LENGTH "${json}" tests)
    set(found -1)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON candidate GET "${json}" tests ${i} name)
            if(candidate STREQUAL name)
                set(found ${i})
            endif()
        endforeach()
    endif()
    set(${out} ${found} PARENT_SCOPE)
endfunction()

# The command of the test called <name>, as a list; and one of its properties, as a string (a regex property is a
# one-element array in the JSON), or "" if it has none. Both fail the case if there is no such test.
function(test_command json name out)
    test_index("${json}" "${name}" i)
    set(command "")
    if(i EQUAL -1)
        fail("no test is called ${name}")
    else()
        string(JSON count LENGTH "${json}" tests ${i} command)
        math(EXPR last "${count} - 1")
        foreach(j RANGE ${last})
            string(JSON word GET "${json}" tests ${i} command ${j})
            list(APPEND command "${word}")
        endforeach()
    endif()
    set(${out} "${command}" PARENT_SCOPE)
endfunction()

function(test_property json name property out)
    test_index("${json}" "${name}" i)
    set(value "")
    if(NOT i EQUAL -1)
        string(JSON count ERROR_VARIABLE ignored LENGTH "${json}" tests ${i} properties)
        if(count GREATER 0)
            math(EXPR last "${count} - 1")
            foreach(j RANGE ${last})
                string(JSON key GET "${json}" tests ${i} properties ${j} name)
                if(key STREQUAL property)
                    string(JSON kind TYPE "${json}" tests ${i} properties ${j} value)
                    if(kind STREQUAL "ARRAY")
                        string(JSON value GET "${json}" tests ${i} properties ${j} value 0)
                    else()
                        string(JSON value GET "${json}" tests ${i} properties ${j} value)
                    endif()
                endif()
            endforeach()
        endif()
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

# Opt in: an empty tool registers nothing, and copies no fixture.
register(off "")
if(DEFINED off_json)
    test_names("${off_json}" names)
    if(NOT "${names}" STREQUAL "")
        fail("an empty tool registered tests: ${names}")
    endif()
    if(EXISTS "${off_build}/clang_tidy_canary")
        fail("an empty tool still copied the canary fixtures into ${off_build}")
    endif()
endif()

# With a tool: the units of the targets by their paths, and the six proofs, and nothing else. unit_b and unit_b_again
# compile one source, which is one unit; orphan.cpp is compiled by no target, gen_a.cpp is generated into the build tree,
# canary_d is a fixture, and x_lib, g_lib, y_lib and e_lib belong to dependencies (under _deps in the build tree, under the
# build tree, under a _deps in the checkout, and outside the checkout: each is told from the project's own by one rule, and the
# list has none of them).
register(on "sh" -DSTUB_WITH_C=ON)
set(proofs
    clang_tidy_version clang_tidy_checks clang_tidy_units
    clang_tidy_canary_source clang_tidy_canary_own_header clang_tidy_canary_dependency_header)
if(DEFINED on_json)
    set(want_names clang_tidy_src_a clang_tidy_tests_unit_b clang_tidy_tests_unit_c ${proofs})
    list(SORT want_names)
    test_names("${on_json}" names)
    if(NOT "${names}" STREQUAL "${want_names}")
        fail("the registered tests are:\n    ${names}\n  want:\n    ${want_names}")
    endif()

    set(build "${on_build}")
    set(config "--config-file=${root}/.clang-tidy")

    # A per-file test: the tool, the compile database of this tree, the config file, the gate's arguments (the SDK too,
    # where the target is Apple), the caller's extras, and the file last.
    set(want_args --warnings-as-errors=* --extra-arg=-Wno-error=deprecated-declarations)
    if(CMAKE_HOST_APPLE)
        execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
        list(APPEND want_args --extra-arg=-isysroot "--extra-arg=${sdk}")
    endif()
    list(APPEND want_args --extra-one --extra-two=2)
    test_command("${on_json}" clang_tidy_src_a command)
    list(POP_FRONT command tool_path)
    cmake_path(GET tool_path FILENAME tool_leaf)
    if(NOT tool_leaf STREQUAL "sh")
        fail("a per-file test runs '${tool_path}', want the tool")
    endif()
    set(want_command -p "${build}" --quiet "${config}" ${want_args} "${root}/src/a.cpp")
    if(NOT "${command}" STREQUAL "${want_command}")
        fail("a per-file test's arguments are:\n    ${command}\n  want:\n    ${want_command}")
    endif()

    # The other proofs name the config file as well, so that the file the checks test lists, the file the canaries read and
    # the file the units read are one file.
    test_command("${on_json}" clang_tidy_checks command)
    list(POP_FRONT command ignored)
    if(NOT "${command}" STREQUAL "${config};--list-checks")
        fail("clang_tidy_checks runs with:\n    ${command}\n  want:\n    ${config};--list-checks")
    endif()
    test_command("${on_json}" clang_tidy_version command)
    list(POP_FRONT command ignored)
    if(NOT "${command}" STREQUAL "--version")
        fail("clang_tidy_version runs with '${command}', want --version")
    endif()
    set(canary "${build}/clang_tidy_canary")
    set(want_canary ${config} ${want_args} "${canary}/canary.cpp" --
        -std=c++23 "-I${canary}/include" "-I${canary}/_deps/canary-src/tests")
    foreach(name IN ITEMS clang_tidy_canary_source clang_tidy_canary_own_header clang_tidy_canary_dependency_header)
        test_command("${on_json}" ${name} command)
        list(POP_FRONT command ignored)
        if(NOT "${command}" STREQUAL "${want_canary}")
            fail("${name} runs with:\n    ${command}\n  want:\n    ${want_canary}")
        endif()
    endforeach()

    # The units proof compares the registered units with the compile database of this tree, at test time.
    test_command("${on_json}" clang_tidy_units command)
    list(POP_FRONT command units_cmake)
    set(want_command
        "-DSOURCE_DIR=${root}" "-DBUILD_DIR=${build}"
        "-DREGISTERED=${root}/src/a.cpp|${root}/tests/unit/b.cpp|${root}/tests/unit/c.cpp"
        -P "${root}/tests/clang_tidy_checks/check_clang_tidy_units.cmake")
    if(NOT "${units_cmake}" STREQUAL "${CMAKE_COMMAND}")
        fail("clang_tidy_units runs '${units_cmake}', want ${CMAKE_COMMAND}")
    endif()
    if(NOT "${command}" STREQUAL "${want_command}")
        fail("clang_tidy_units runs with:\n    ${command}\n  want:\n    ${want_command}")
    endif()

    # The properties that make the tests mean it. The expressions are the gate's to prove, with the tool; here it is that
    # each is there and names what it is about.
    test_property("${on_json}" clang_tidy_version PASS_REGULAR_EXPRESSION version_pass)
    if(NOT "${version_pass}" STREQUAL "LLVM version ${CRYPTOCOGS_CLANG_TIDY_MAJOR}\\.")
        fail("clang_tidy_version passes on '${version_pass}', want the major ${CRYPTOCOGS_CLANG_TIDY_MAJOR}")
    endif()
    test_property("${on_json}" clang_tidy_checks PASS_REGULAR_EXPRESSION checks_pass)
    test_property("${on_json}" clang_tidy_checks FAIL_REGULAR_EXPRESSION checks_fail)
    foreach(word IN ITEMS bugprone-use-after-move cppcoreguidelines-pro-type-member-init
                          performance-unnecessary-value-param)
        string(FIND "${checks_pass}" "${word}" at)
        if(at EQUAL -1)
            fail("clang_tidy_checks does not ask for ${word}: '${checks_pass}'")
        endif()
    endforeach()
    foreach(word IN ITEMS bugprone-easily-swappable-parameters bugprone-narrowing-conversions readability- modernize-)
        string(FIND "${checks_fail}" "${word}" at)
        if(at EQUAL -1)
            fail("clang_tidy_checks does not refuse ${word}: '${checks_fail}'")
        endif()
    endforeach()
    test_property("${on_json}" clang_tidy_canary_source PASS_REGULAR_EXPRESSION source_pass)
    test_property("${on_json}" clang_tidy_canary_own_header PASS_REGULAR_EXPRESSION own_pass)
    test_property("${on_json}" clang_tidy_canary_dependency_header PASS_REGULAR_EXPRESSION dep_pass)
    test_property("${on_json}" clang_tidy_canary_dependency_header FAIL_REGULAR_EXPRESSION dep_fail)
    string(FIND "${source_pass}" "canary\\.cpp" at_unit)
    string(FIND "${source_pass}" "bugprone-use-after-move" at_check)
    if(at_unit EQUAL -1 OR at_check EQUAL -1)
        fail("clang_tidy_canary_source does not look for bugprone-use-after-move in canary.cpp: '${source_pass}'")
    endif()
    string(FIND "${own_pass}" "include/exchange/canary_own\\.hpp" at_header)
    string(FIND "${own_pass}" "performance-unnecessary-value-param" at_check)
    if(at_header EQUAL -1 OR at_check EQUAL -1)
        fail("clang_tidy_canary_own_header does not look for performance-unnecessary-value-param in "
             "include/exchange/canary_own.hpp: '${own_pass}'")
    endif()
    if(NOT "${dep_pass}" STREQUAL "${source_pass}")
        fail("clang_tidy_canary_dependency_header must see the unit's error too, or it passes on a run that checked "
             "nothing: passes on '${dep_pass}'")
    endif()
    if(NOT "${dep_fail}" STREQUAL "canary_dep\\.hpp")
        fail("clang_tidy_canary_dependency_header fails on '${dep_fail}', want a mention of canary_dep.hpp")
    endif()

    # The fixtures are copied under the build tree, to the two kinds of path the header filter must tell apart.
    foreach(pair IN ITEMS
            "canary.cpp|canary.cpp"
            "canary_own.hpp|include/exchange/canary_own.hpp"
            "canary_dep.hpp|_deps/canary-src/tests/canary_dep.hpp")
        string(REPLACE "|" ";" pair "${pair}")
        list(GET pair 0 from)
        list(GET pair 1 to)
        if(NOT EXISTS "${canary}/${to}")
            fail("the fixture ${from} was not copied to ${canary}/${to}")
        else()
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E compare_files "${SOURCE_DIR}/tests/clang_tidy_checks/canaries/${from}"
                        "${canary}/${to}"
                RESULT_VARIABLE differs)
            if(NOT "${differs}" STREQUAL "0")
                fail("${canary}/${to} is not a copy of tests/clang_tidy_checks/canaries/${from}")
            endif()
        endif()
    endforeach()
endif()

# The list follows the build: with the optional unit off it is not a unit, and the proof is handed the shorter list.
register(without_c "sh" -DSTUB_WITH_C=OFF)
if(DEFINED without_c_json)
    set(want_names clang_tidy_src_a clang_tidy_tests_unit_b ${proofs})
    list(SORT want_names)
    test_names("${without_c_json}" names)
    if(NOT "${names}" STREQUAL "${want_names}")
        fail("with the optional unit off, the registered tests are:\n    ${names}\n  want:\n    ${want_names}")
    endif()
    test_command("${without_c_json}" clang_tidy_units command)
    list(FIND command "-DREGISTERED=${root}/src/a.cpp|${root}/tests/unit/b.cpp" at)
    if(at EQUAL -1)
        fail("with the optional unit off, clang_tidy_units is handed:\n    ${command}")
    endif()
endif()

# ── R ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
# The real root. The trees are named 'scratch' and the like, which the build-tree guard does not examine.
file(MAKE_DIRECTORY "${scratch}/real")

# Configure the real root into <name> and check the outcome. EXPECT is 'refused' or 'configures'; WORDS must all be in a
# refusal's message, which is compared word by word because CMake wraps its errors.
function(configure_root name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT" "WORDS;ARGS")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${scratch}/real/${name}" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
                ${arg_ARGS}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(arg_EXPECT STREQUAL "configures")
        if(NOT "${status}" STREQUAL "0")
            fail("${name}: the root did not configure, want it to:\n${out}\n${err}")
        endif()
    else()
        string(REGEX REPLACE "[ \t\r\n]+" " " words "${err}")
        if("${status}" STREQUAL "0")
            fail("${name}: the root configured, want a configure error")
        endif()
        foreach(word IN LISTS arg_WORDS)
            string(FIND "${words}" "${word}" at)
            if(at EQUAL -1)
                fail("${name}: the root's error does not say '${word}':\n${err}")
            endif()
        endforeach()
    endif()
endfunction()

# A tool no host has: the root resolves the tool before its first dependency. FETCHCONTENT_FULLY_DISCONNECTED is on and no
# source directory is given, so a root that went on to fetch nlohmann_json would fail there with another message, and the
# words below would be missing.
configure_root(missing_tool EXPECT refused
    ARGS -DCRYPTOCOGS_CLANG_TIDY=no-such-clang-tidy-for-cryptocogs
    WORDS "CRYPTOCOGS_CLANG_TIDY=no-such-clang-tidy-for-cryptocogs" "not a command or a path")

# A tool with the tests off: the clang_tidy_ tests are registered with the tests, so this is a gate that would be silently
# absent.
configure_root(tests_off EXPECT refused
    ARGS -DCRYPTOCOGS_CLANG_TIDY=sh -DCRYPTOCOGS_BUILD_TESTS=OFF
    WORDS "CRYPTOCOGS_CLANG_TIDY" "CRYPTOCOGS_BUILD_TESTS")

# The root as a subdirectory of another project never looks at the variable: a tool no host has, and the tests off, and it
# configures. The two header-only dependencies it fetches are the sources this build already has, so there is no network.
file(WRITE "${scratch}/parent/CMakeLists.txt"
    "cmake_minimum_required(VERSION 4.0)\n"
    "project(parent LANGUAGES NONE)\n"
    "add_subdirectory(\"${SOURCE_DIR}\" cryptocogs)\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${scratch}/parent" -B "${scratch}/parent/build/scratch"
            -DCRYPTOCOGS_BUILD_TESTS=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=${NLOHMANN_JSON_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${TOMLPLUSPLUS_SRC}"
            -DCRYPTOCOGS_CLANG_TIDY=no-such-clang-tidy-for-cryptocogs
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${status}" STREQUAL "0")
    fail("as a subdirectory with CRYPTOCOGS_CLANG_TIDY set to a tool no host has, the root did not configure, want it to "
         "ignore the variable:\n${out}\n${err}")
endif()

# The gate on over the real targets: the root with its tests on, over the dependency sources this build already has, and `sh`
# as the tool, which is never run. The dependencies are named into this build's _deps, which is in the checkout: only the
# _deps rule keeps their sources out, so this is where a regression in it would show. What is checked is what the root
# registers at its end: the six proofs, a unit for every source under src/ (every adapter is on by default, so every one is
# built) and none for a dependency's or the build tree's, and then clang_tidy_units, a cmake -P script, over the compile
# database that this very configure wrote.
set(tree "${scratch}/real/gate_on")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${tree}" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=${NLOHMANN_JSON_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${TOMLPLUSPLUS_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_IXWEBSOCKET=${IXWEBSOCKET_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=${GOOGLETEST_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_BACKWARD=${BACKWARD_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_SPDLOG=${SPDLOG_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_CLI11=${CLI11_SRC}"
            -DCRYPTOCOGS_CLANG_TIDY=sh
            "-DCRYPTOCOGS_CLANG_TIDY_ARGS=--extra-one;--extra-two=2"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${status}" STREQUAL "0")
    fail("the real root with the gate on did not configure:\n${out}\n${err}")
else()
    execute_process(
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${tree}" -R "^clang_tidy_" --show-only=json-v1
        RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE err)
    if(NOT "${status}" STREQUAL "0")
        fail("ctest could not list the real root's clang_tidy_ tests:\n${err}")
    else()
        test_names("${json}" names)
        foreach(proof IN LISTS proofs)
            if(NOT proof IN_LIST names)
                fail("the real root did not register ${proof}")
            endif()
        endforeach()
        file(GLOB_RECURSE library_sources "${SOURCE_DIR}/src/*.cpp")
        foreach(source IN LISTS library_sources)
            cryptocogs_clang_tidy_test_name("${SOURCE_DIR}" "${source}" unit)
            if(NOT unit IN_LIST names)
                fail("the real root did not register ${unit} for ${source}")
            endif()
        endforeach()
        foreach(name IN LISTS names)
            if(name MATCHES "_deps|^clang_tidy_build_")
                fail("the real root registered ${name}, a dependency's or the build tree's source")
            endif()
        endforeach()
        # What a unit is handed: the tool as a path, the compile database of this tree, the config file of the checkout, the
        # caller's extras from CRYPTOCOGS_CLANG_TIDY_ARGS in order, and the file last.
        test_command("${json}" clang_tidy_src_kraken_auth command)
        list(POP_FRONT command tool_path)
        # ctest finds a bare command name on the PATH it runs with, so the listing cannot tell a resolved tool from one
        # that was not; the file the configure wrote can. The registration must hold the path find_program gave.
        file(READ "${tree}/CTestTestfile.cmake" ctest_file)
        string(REGEX MATCH "add_test\\([^ \n]*clang_tidy_src_kraken_auth[^ \n]* +(\"|\\[=\\[)/" resolved "${ctest_file}")
        if("${resolved}" STREQUAL "")
            fail("the real root registered a unit with a command that is not the absolute path of the tool: see "
                 "${tree}/CTestTestfile.cmake")
        endif()
        foreach(want IN ITEMS "-p;${tree}" "--config-file=${SOURCE_DIR}/.clang-tidy" "--extra-one;--extra-two=2"
                              "--extra-two=2;${SOURCE_DIR}/src/kraken/auth.cpp")
            string(FIND "${command}" "${want}" at)
            if(at EQUAL -1)
                fail("the real root hands a unit '${command}', want '${want}' in it")
            endif()
        endforeach()
        execute_process(
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${tree}" -R "^clang_tidy_units$" --output-on-failure
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT "${status}" STREQUAL "0")
            fail("clang_tidy_units failed in the real root's tree:\n${out}\n${err}")
        endif()
    endif()
endif()

file(REMOVE_RECURSE "${scratch}")
get_property(failures GLOBAL PROPERTY WIRING_FAILURES)
if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy tests are wrongly wired:${failures}")
endif()
message(STATUS "the clang-tidy tests register only when asked, for the targets' units and no others, by path, with the config "
               "file and their properties, and the root refuses a tool it cannot run before it fetches anything")
