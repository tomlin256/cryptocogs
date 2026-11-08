# check_clang_tidy_units.cmake — clang_tidy_units: the units the gate registered are the units the tree compiles (plan
# clang-tidy-opt-in-test-step, D9).
#
# Run by ctest, in a tree configured with CRYPTOCOGS_CLANG_TIDY, as
#   cmake -DSOURCE_DIR=<repo> -DBUILD_DIR=<tree> "-DREGISTERED=<unit>|<unit>|..." -P check_clang_tidy_units.cmake
# REGISTERED is the list of units that cryptocogs_clang_tidy_add_tests turned into tests, bar-separated.
#
# The gate takes its units from the targets of the project at configure time, because a unit that is not in the tree's
# compile database would be analysed with flags that clang-tidy guesses. The database is the oracle clang-tidy itself reads,
# so this compares the two: every .cpp, .cc or .cxx in the database that is under the checkout and not under the build
# tree, less the fixtures (the same filter the registration used), against the registered list. A difference both ways is
# a failure:
#
#   - in the database and not registered: a target created after the registration call in the root CMakeLists.txt, or
#     sources the walk could not read (a generator expression), so a unit that is built is not checked;
#   - registered and not in the database: a unit the tree does not build, which clang-tidy would guess flags for.
#
# A gate that silently checks fewer files than the tree builds passes, which is why this is a test.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR BUILD_DIR REGISTERED)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DBUILD_DIR=<tree> \"-DREGISTERED=<unit>|<unit>|...\" "
            "-P check_clang_tidy_units.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_clang_tidy.cmake")

set(database "${BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${database}")
    message(FATAL_ERROR "${database} does not exist: the tree was configured without a compile database, and the gate "
                        "reads one")
endif()
file(READ "${database}" json)
string(JSON count LENGTH "${json}")

# The files the tree compiles, less anything under the build tree (the dependencies' and any generated source).
set(build "${BUILD_DIR}")
cmake_path(NORMAL_PATH build)
set(candidates "")
if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON file GET "${json}" ${i} file)
        cmake_path(IS_PREFIX build "${file}" NORMALIZE in_build)
        if(NOT in_build)
            list(APPEND candidates "${file}")
        endif()
    endforeach()
endif()
cryptocogs_clang_tidy_select("${SOURCE_DIR}" "${candidates}" compiled)

string(REPLACE "|" ";" registered "${REGISTERED}")
list(SORT registered)

# The items of <a> that are not in <b>.
function(difference a b out)
    set(result "")
    foreach(item IN LISTS a)
        if(NOT item IN_LIST b)
            list(APPEND result "${item}")
        endif()
    endforeach()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

difference("${compiled}" "${registered}" unchecked)
difference("${registered}" "${compiled}" unbuilt)
set(failures "")
if(NOT "${unchecked}" STREQUAL "")
    string(REPLACE ";" "\n    " listed "${unchecked}")
    string(APPEND failures
        "\n  the compile database lists units that were not registered, so they are built and not checked (a target "
        "created after the registration call in the root CMakeLists.txt?):\n    ${listed}")
endif()
if(NOT "${unbuilt}" STREQUAL "")
    string(REPLACE ";" "\n    " listed "${unbuilt}")
    string(APPEND failures
        "\n  units were registered that the compile database does not list, so clang-tidy would guess their flags:"
        "\n    ${listed}")
endif()
if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy units are not the tree's units:${failures}")
endif()
list(LENGTH compiled units)
message(STATUS "the ${units} units the gate checks are exactly the ${units} that the tree compiles")
