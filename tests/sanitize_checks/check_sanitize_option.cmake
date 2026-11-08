# check_sanitize_option.cmake — test_sanitize_option: the CRYPTOCOGS_SANITIZE option in the root CMakeLists.txt
# reaches the project's own targets (plan sanitizer-option-and-presets).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> -DTOMLPLUSPLUS_SRC=<dir>
#         -P check_sanitize_option.cmake
# It configures the real top-level project, as check_tree_name.cmake does, with the tests off and the two
# dependencies it still fetches taken from the sources the running build already has, so it needs no network and
# compiles nothing. Every tree is made in <scratch>/build/<name>, and the scratch directory is removed at the end.
# The names are not platform trees (no macos or linux in them), so the build-tree guard does not examine them.
#
# What it holds, from the compile_commands.json each tree writes:
#
#   - with no option, the status line is absent, the cache says OFF and no unit has a -fsanitize option;
#   - with asan or tsan, the status line names the mode and every translation unit carries every compile option of
#     that mode (the expected list is read from cmake/cryptocogs_sanitize.cmake, so test_sanitize_flags is the only
#     place that pins the text of the options);
#   - a mistyped mode stops the configure, naming the value and listing the modes.
#
# The tests are off here, so the units are the project's own libraries. That is what this proves: that the root file
# applies the options at all. The fetched dependencies, and every executable, are held by test_sanitizer_coverage in
# a sanitizer tree, where they exist.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR SCRATCH_ROOT NLOHMANN_JSON_SRC TOMLPLUSPLUS_SRC)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -DNLOHMANN_JSON_SRC=<dir> "
            "-DTOMLPLUSPLUS_SRC=<dir> -P check_sanitize_option.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_sanitize.cmake")

# Fewer units than this and the scan has read nothing worth the name: the project's libraries alone are some thirty.
set(min_units 5)

set(scratch "${SCRATCH_ROOT}/sanitize_option")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}/build")

set(failures "")

# Configure the real project into <scratch>/build/<name>. Sets <prefix>_rc, <prefix>_text (stdout and stderr) and
# <prefix>_db (the tree's compile_commands.json, or empty if it wrote none).
function(configure_tree prefix name)
    set(tree "${scratch}/build/${name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${tree}"
            -DCRYPTOCOGS_BUILD_TESTS=OFF
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=${NLOHMANN_JSON_SRC}"
            "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${TOMLPLUSPLUS_SRC}"
            ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(db "")
    if(EXISTS "${tree}/compile_commands.json")
        file(READ "${tree}/compile_commands.json" db)
    endif()
    set(${prefix}_rc "${rc}" PARENT_SCOPE)
    set(${prefix}_text "${out}\n${err}" PARENT_SCOPE)
    set(${prefix}_db "${db}" PARENT_SCOPE)
endfunction()

function(expect_configures label prefix)
    if(NOT ${prefix}_rc EQUAL 0)
        string(APPEND failures "\n  ${label}: the configure failed (${${prefix}_rc}): ${${prefix}_text}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The configure's output must (or must not) contain a text.
function(expect_output label prefix needle want_present)
    string(FIND "${${prefix}_text}" "${needle}" at)
    if(want_present AND at EQUAL -1)
        string(APPEND failures "\n  ${label}: the output does not contain '${needle}': ${${prefix}_text}")
    elseif(NOT want_present AND NOT at EQUAL -1)
        string(APPEND failures "\n  ${label}: the output contains '${needle}', want it absent")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# No translation unit in the tree's compile_commands.json has an option that starts with -fsanitize.
function(expect_no_sanitize_option label prefix)
    string(FIND "${${prefix}_db}" "-fsanitize" at)
    if(NOT at EQUAL -1)
        string(APPEND failures "\n  ${label}: compile_commands.json has a -fsanitize option, want none")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# Every translation unit in the tree's compile_commands.json carries every option in ARGN. The options are matched
# whole, between spaces, so that -fsanitize=address does not pass for -fsanitize=address,undefined.
function(expect_units_carry label prefix)
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
    set(lacking "")
    foreach(i RANGE ${last})
        string(JSON entry GET "${db}" ${i})
        string(JSON file ERROR_VARIABLE file_error GET "${entry}" file)
        string(JSON command ERROR_VARIABLE command_error GET "${entry}" command)
        if(command_error)
            list(APPEND lacking "${file}: the entry has no \"command\" (CMake's layout changed?)")
            continue()
        endif()
        set(padded " ${command} ")
        set(missing "")
        foreach(option IN LISTS ARGN)
            string(FIND "${padded}" " ${option} " at)
            if(at EQUAL -1)
                list(APPEND missing "${option}")
            endif()
        endforeach()
        if(missing)
            list(JOIN missing ", " missing_text)
            list(APPEND lacking "${file}: no ${missing_text}")
        endif()
    endforeach()
    if(lacking)
        list(LENGTH lacking n_lacking)
        list(SUBLIST lacking 0 8 shown)
        list(JOIN shown "\n    " report)
        string(APPEND failures
            "\n  ${label}: ${n_lacking} of ${count} units miss an option; the first:\n    ${report}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── no option: nothing changes ─────────────────────────────────────────────────────────────────────
configure_tree(a opt-default)
expect_configures("no option" a)
expect_output("no option" a "cryptocogs: sanitizer" FALSE)
expect_no_sanitize_option("no option" a)
if(EXISTS "${scratch}/build/opt-default/CMakeCache.txt")
    file(READ "${scratch}/build/opt-default/CMakeCache.txt" cache)
    if(NOT cache MATCHES "(^|\n)CRYPTOCOGS_SANITIZE:STRING=OFF\n")
        string(APPEND failures "\n  no option: the cache does not hold CRYPTOCOGS_SANITIZE:STRING=OFF")
    endif()
endif()

# ── each mode: the status line, and every unit carries the mode's options ───────────────────────────
foreach(mode IN ITEMS asan tsan)
    cryptocogs_sanitizer_flags("${mode}" want_compile want_link)
    configure_tree(m opt-${mode} -DCRYPTOCOGS_SANITIZE=${mode})
    expect_configures("${mode}" m)
    expect_output("${mode}" m "cryptocogs: sanitizer '${mode}'" TRUE)
    expect_units_carry("${mode}" m ${want_compile})
endforeach()

# ── a mistyped mode stops the configure, naming the value and the modes ────────────────────────────
configure_tree(b opt-bad -DCRYPTOCOGS_SANITIZE=adress)
if(b_rc EQUAL 0)
    string(APPEND failures "\n  adress: the configure succeeded, want it refused")
else()
    # CMake word-wraps a FATAL_ERROR, which can split the list of modes across two lines: compare the words.
    string(REGEX REPLACE "[ \t\r\n]+" " " words "${b_text}")
    foreach(needle IN ITEMS "CRYPTOCOGS_SANITIZE='adress'" "OFF, asan, tsan")
        string(FIND "${words}" "${needle}" at)
        if(at EQUAL -1)
            string(APPEND failures "\n  adress: the output does not contain '${needle}': ${b_text}")
        endif()
    endforeach()
endif()

file(REMOVE_RECURSE "${scratch}")

if(failures)
    message(FATAL_ERROR "test_sanitize_option failed:${failures}")
endif()
message(STATUS "test_sanitize_option: ok")
