# check_curl_floor_fixtures.cmake — test_curl_floor_fixtures: the libcurl floor check accepts what it should and
# refuses what it should.
#
# Run by ctest as `cmake -DSCRATCH_ROOT=<dir> -P check_curl_floor_fixtures.cmake`. It needs no configure and no
# build tree. It makes small checkouts in SCRATCH_ROOT, each the three files that state the floor (CMakeLists.txt,
# cmake/cryptocogsConfig.cmake.in, README.md) and nothing else, and runs check_curl_floor.cmake over each in a
# child run, because a refusal is a FATAL_ERROR and that ends a `cmake -P` run. A case that must be accepted has
# to exit 0 and name the floor. A case that must be refused has to exit non-zero with a message that names the
# file that disagrees and what it should say, because that message is what the person who changed one of the
# three reads.
#
# The check script is the one beside this script, so a hand-made change to a scratch copy of the repo must fail
# on the case it breaks. The scratch directory is made fresh and removed at the end.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SCRATCH_ROOT)
    message(FATAL_ERROR "usage: cmake -DSCRATCH_ROOT=<dir> -P check_curl_floor_fixtures.cmake")
endif()

set(CHECKER "${CMAKE_CURRENT_LIST_DIR}/check_curl_floor.cmake")
if(NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "found no ${CHECKER}: this test runs the libcurl floor check, which is missing")
endif()
set(SCRATCH "${SCRATCH_ROOT}/curl_floor")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# A checkout of the three files. ROOT is the text of the root CMakeLists.txt, CONFIG of the package config and
# README of README.md; each is written as given.
function(write_checkout checkout)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ROOT;CONFIG;README" "")
    file(MAKE_DIRECTORY "${SCRATCH}/${checkout}/cmake")
    file(WRITE "${SCRATCH}/${checkout}/CMakeLists.txt" "${arg_ROOT}")
    file(WRITE "${SCRATCH}/${checkout}/cmake/cryptocogsConfig.cmake.in" "${arg_CONFIG}")
    file(WRITE "${SCRATCH}/${checkout}/README.md" "${arg_README}")
endfunction()

set(failures "")

# Run the check over a checkout. ACCEPT must exit 0 and print every word of WORDS; REFUSE must exit non-zero and
# say every word of WORDS in its error. CMake word-wraps a FATAL_ERROR, so the words are compared and not where
# the lines break.
function(expect_check checkout verdict)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "WORDS")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SCRATCH}/${checkout}" -P "${CHECKER}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE printed
        ERROR_VARIABLE message)
    string(REGEX REPLACE "[ \t\r\n]+" " " words "${printed} ${message}")
    if(verdict STREQUAL "ACCEPT")
        if(NOT "${status}" STREQUAL "0")
            string(APPEND failures "\n  ${checkout}: was refused (${status}), want accepted:\n${message}")
        endif()
    elseif("${status}" STREQUAL "0")
        string(APPEND failures "\n  ${checkout}: was accepted, want refused")
    endif()
    foreach(word IN LISTS arg_WORDS)
        string(FIND "${words}" "${word}" at)
        if(at EQUAL -1)
            string(APPEND failures "\n  ${checkout}: the output does not say '${word}':\n${printed}${message}")
        endif()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The three files as the project writes them, at one floor.
set(ROOT_AT_7_80 "find_package(OpenSSL REQUIRED)\nfind_package(CURL 7.80.0 REQUIRED)\n")
set(CONFIG_AT_7_80 "include(CMakeFindDependencyMacro)\nfind_dependency(OpenSSL)\nfind_dependency(CURL 7.80.0)\n")
set(README_AT_7_80 "Building needs a C++23 compiler, CMake 4.0 or later and libcurl 7.80.0 or later.\n")

# ── accepted ─────────────────────────────────────────────────────────────────────────────────────────────────
write_checkout(three_agree ROOT "${ROOT_AT_7_80}" CONFIG "${CONFIG_AT_7_80}" README "${README_AT_7_80}")
expect_check(three_agree ACCEPT WORDS "the libcurl floor is 7.80.0")

# The floor is whatever the root says, not a number the check knows. A comment that shows another lookup is not
# one: the lines after # here would be a second call at the root and another version in the package config.
write_checkout(three_agree_on_another
    ROOT "# find_package(CURL REQUIRED)\nfind_package(CURL 8.5 REQUIRED)  # not find_package(CURL 7.80.0)\n"
    CONFIG "find_dependency(CURL 8.5)  # not find_dependency(CURL 7.80.0)\n"
    README "| libcurl 8.5 or later | system |\n")
expect_check(three_agree_on_another ACCEPT WORDS "the libcurl floor is 8.5")

# ── refused ──────────────────────────────────────────────────────────────────────────────────────────────────
# The root looks libcurl up and names no version: the project then has no floor, whatever the other two say.
write_checkout(root_names_no_version
    ROOT "find_package(CURL REQUIRED)\n" CONFIG "${CONFIG_AT_7_80}" README "${README_AT_7_80}")
expect_check(root_names_no_version REFUSE WORDS "CMakeLists.txt" "without a minimum version" "no libcurl floor")

# A range is not a floor that the other two can repeat.
write_checkout(root_names_a_range
    ROOT "find_package(CURL 7.80.0...8.99 REQUIRED)\n" CONFIG "${CONFIG_AT_7_80}" README "${README_AT_7_80}")
expect_check(root_names_a_range REFUSE WORDS "CMakeLists.txt" "'7.80.0...8.99'" "not a plain version")

# Two lookups at the root: which one is the floor?
write_checkout(root_looks_up_twice
    ROOT "find_package(CURL 7.80.0 REQUIRED)\nfind_package(CURL REQUIRED)\n" CONFIG "${CONFIG_AT_7_80}" README "${README_AT_7_80}")
expect_check(root_looks_up_twice REFUSE WORDS "found 2 calls of find_package for CURL" "CMakeLists.txt")

# The floor was raised at the root and the installed package still asks for the old one, or for none.
write_checkout(config_asks_for_another
    ROOT "${ROOT_AT_7_80}" CONFIG "find_dependency(CURL 7.79.1)\n" README "${README_AT_7_80}")
expect_check(config_asks_for_another REFUSE
             WORDS "the libcurl floor is 7.80.0" "cmake/cryptocogsConfig.cmake.in asks for CURL 7.79.1, not 7.80.0")

write_checkout(config_names_no_version
    ROOT "${ROOT_AT_7_80}" CONFIG "find_dependency(OpenSSL)\nfind_dependency(CURL)\n" README "${README_AT_7_80}")
expect_check(config_names_no_version REFUSE
             WORDS "the libcurl floor is 7.80.0" "cmake/cryptocogsConfig.cmake.in looks CURL up without a minimum version, not 7.80.0")

write_checkout(config_does_not_look_it_up
    ROOT "${ROOT_AT_7_80}" CONFIG "find_dependency(OpenSSL)\n" README "${README_AT_7_80}")
expect_check(config_does_not_look_it_up REFUSE
             WORDS "cmake/cryptocogsConfig.cmake.in has 0 calls of find_dependency for CURL, not one")

# The README names another version, or none.
write_checkout(readme_names_another
    ROOT "${ROOT_AT_7_80}" CONFIG "${CONFIG_AT_7_80}" README "Building needs libcurl 7.79.1 or later.\n")
expect_check(readme_names_another REFUSE WORDS "the libcurl floor is 7.80.0" "README.md does not say \"libcurl 7.80.0 or later\"")

write_checkout(readme_names_none
    ROOT "${ROOT_AT_7_80}" CONFIG "${CONFIG_AT_7_80}" README "| libcurl | system |\n")
expect_check(readme_names_none REFUSE WORDS "README.md does not say \"libcurl 7.80.0 or later\"")

# Both of the other two disagree: the error names both, so one run is enough to put them right.
write_checkout(config_and_readme_disagree
    ROOT "${ROOT_AT_7_80}" CONFIG "find_dependency(CURL)\n" README "| libcurl | system |\n")
expect_check(config_and_readme_disagree REFUSE
             WORDS "cmake/cryptocogsConfig.cmake.in looks CURL up without a minimum version" "README.md does not say")

file(REMOVE_RECURSE "${SCRATCH}")
if(NOT failures STREQUAL "")
    message(FATAL_ERROR "the libcurl floor check does not behave as specified:${failures}")
endif()
message(STATUS "the libcurl floor check accepts the checkouts whose three files agree and refuses the ones where one disagrees")
