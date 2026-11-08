# check_clang_tidy_config.cmake — test_clang_tidy_config: .clang-tidy and .clangd (plan clang-tidy-opt-in-test-step, #34).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_clang_tidy_config.cmake`. It reads two files and lists the headers the
# checkout owns, needs no clang-tidy, and runs in every tree, because the editor reads .clang-tidy whether or not the gate
# is on.
#
# What it holds:
#
#   - HeaderFilterRegex admits every header the checkout owns (under include/, src/ and tests/). The tests report a finding
#     in a header only when its path matches, so a header under a directory the expression lacks has its includers checked
#     and itself silently not. A header added under a new directory fails here, and the person who added it decides.
#   - ExcludeHeaderFilterRegex is /_deps/, which keeps every dependency out even where it has a directory that the inclusion
#     matches (googletest, spdlog and nlohmann/json have a tests/ or a test/ of their own).
#   - .clang-tidy has Checks, and no WarningsAsErrors: clangd would show every hint in the editor as an error, so the tests
#     pass --warnings-as-errors=* themselves.
#   - .clangd keeps its compilation database and carries no ClangTidy list. .clang-tidy is the only one, so the editor and
#     the gate cannot disagree about what is checked.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_clang_tidy_config.cmake")
endif()

set(failures "")
foreach(name IN ITEMS .clang-tidy .clangd)
    if(NOT EXISTS "${SOURCE_DIR}/${name}")
        string(APPEND failures "\n  ${SOURCE_DIR}/${name} does not exist")
    endif()
endforeach()
if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy configuration is wrong:${failures}")
endif()
file(READ "${SOURCE_DIR}/.clang-tidy" tidy)
file(READ "${SOURCE_DIR}/.clangd" clangd)

# HeaderFilterRegex: '<regex>', and every header the checkout owns against it. The path matched is the one under the
# checkout, with a leading slash, so the result does not depend on where the checkout lives.
string(REGEX MATCH "(^|\n)HeaderFilterRegex: '([^'\n]*)'" matched "${tidy}")
if("${matched}" STREQUAL "")
    string(APPEND failures "\n  .clang-tidy has no HeaderFilterRegex of the form '<regex>'")
else()
    set(admits "${CMAKE_MATCH_2}")
    set(headers "")
    foreach(directory IN ITEMS include src tests)
        foreach(pattern IN ITEMS hpp h hxx inl)
            file(GLOB_RECURSE found LIST_DIRECTORIES false RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${directory}/*.${pattern}")
            list(APPEND headers ${found})
        endforeach()
    endforeach()
    list(LENGTH headers count)
    if(count EQUAL 0)
        string(APPEND failures "\n  found no header under include/, src/ or tests/ in ${SOURCE_DIR}: is SOURCE_DIR the checkout?")
    endif()
    set(unadmitted "")
    foreach(header IN LISTS headers)
        string(REGEX MATCH "${admits}" hit "/${header}")
        if("${hit}" STREQUAL "")
            list(APPEND unadmitted "${header}")
        endif()
    endforeach()
    if(NOT "${unadmitted}" STREQUAL "")
        string(REPLACE ";" "\n    " listed "${unadmitted}")
        string(APPEND failures
            "\n  .clang-tidy's HeaderFilterRegex '${admits}' does not admit these headers, whose findings would be"
            " silently left out:\n    ${listed}")
    endif()
endif()

string(REGEX MATCH "(^|\n)ExcludeHeaderFilterRegex: '/_deps/'[ \t]*(\n|$)" matched "${tidy}")
if("${matched}" STREQUAL "")
    string(APPEND failures "\n  .clang-tidy's ExcludeHeaderFilterRegex is not '/_deps/'")
endif()

string(REGEX MATCH "(^|\n)Checks:" matched "${tidy}")
if("${matched}" STREQUAL "")
    string(APPEND failures "\n  .clang-tidy has no Checks")
endif()
string(REGEX MATCH "(^|\n)[ \t]*WarningsAsErrors[ \t]*:" matched "${tidy}")
if(NOT "${matched}" STREQUAL "")
    string(APPEND failures
        "\n  .clang-tidy has WarningsAsErrors, which clangd would show as an error on every hint: "
        "the tests pass --warnings-as-errors=* themselves")
endif()

# The root registers the tests after the last of its add_subdirectory calls, because the units are read from the targets. A
# call placed above one of them would leave that directory's units out; clang_tidy_units notices a unit missed in a tree that
# has the gate on, and this is the text of the rule for the trees that have not.
file(READ "${SOURCE_DIR}/CMakeLists.txt" root)
string(FIND "${root}" "cryptocogs_clang_tidy_add_tests(" at_call REVERSE)
string(FIND "${root}" "add_subdirectory(" at_last_directory REVERSE)
if(at_call EQUAL -1)
    string(APPEND failures "\n  the root CMakeLists.txt never calls cryptocogs_clang_tidy_add_tests(): the gate registers nothing")
elseif(at_call LESS at_last_directory)
    string(APPEND failures
        "\n  the root CMakeLists.txt calls cryptocogs_clang_tidy_add_tests() above its last add_subdirectory(): the units of "
        "the directories added after it are not registered")
endif()

string(REGEX MATCH "(^|\n)[ \t]*CompilationDatabase:" matched "${clangd}")
if("${matched}" STREQUAL "")
    string(APPEND failures "\n  .clangd has lost its CompilationDatabase")
endif()
string(REGEX MATCH "(^|\n)[ \t]*ClangTidy[ \t]*:" matched "${clangd}")
if(NOT "${matched}" STREQUAL "")
    string(APPEND failures "\n  .clangd has a ClangTidy list: .clang-tidy is the only one")
endif()

if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy configuration is wrong:${failures}")
endif()
message(STATUS ".clang-tidy admits all ${count} headers of the checkout and excludes _deps, and .clangd carries no second list")
