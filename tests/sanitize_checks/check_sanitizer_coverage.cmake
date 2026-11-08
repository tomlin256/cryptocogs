# check_sanitizer_coverage.cmake — test_sanitizer_coverage: every compile unit and every executable in a
# CRYPTOCOGS_SANITIZE tree carries the mode's options (plan sanitizer-option-and-presets).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DBUILD_DIR=<build dir> -DMODE=<asan|tsan> -P check_sanitizer_coverage.cmake
# and registered only where a mode is on and the generator writes link.txt (Makefiles).
#
# Why it exists. The canaries prove the sanitizer is live in the executables they are. They say nothing about the
# other forty, or about the hundred and twenty-odd translation units behind them, some fifty of which are in
# fetched dependencies. The root CMakeLists.txt sets the options as directory options before the first
# FetchContent_MakeAvailable, and a directory's options reach only the targets created after that line, so a
# subdirectory added above it would be quietly built without them. Its tests would pass, uninstrumented, in a tree
# named for the sanitizer. For ThreadSanitizer an uninstrumented static library linked into instrumented
# executables is the worst case: what it synchronises with is invisible, so a clean run proves nothing about it.
#
# Two halves, because each sees what the other cannot:
#
#   - Compile side: compile_commands.json, which the root CMakeLists.txt exports for every target, lists the
#     sources of static libraries and of fetched dependencies as well as those of executables. Every unit's command
#     must carry every compile option of the mode. A link line cannot tell: a static library has none, and the
#     executables that link it carry the link options whether or not it was compiled with the compile options.
#   - Link side: the link.txt that the Makefiles generator writes for each target. Every executable's link line
#     must carry every link option. A missing one would usually fail the link, since the runtime is a library the
#     linker must be told about, but this half does not depend on that.
#
# The expected options come from the same function the build used, so there is one place that says what a mode
# means, and test_sanitize_flags pins that. Both halves read CMake's own files, whose layout is not a contract, so
# each refuses to pass having scanned fewer than a floor (the tree has about 125 units and 40 executables, and an
# adapter-only tree far fewer): a changed layout fails loudly instead of passing on nothing.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR BUILD_DIR MODE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DBUILD_DIR=<build dir> -DMODE=<asan|tsan> "
            "-P check_sanitizer_coverage.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_sanitize.cmake")
cryptocogs_sanitizer_flags("${MODE}" want_compile want_link)
if("${want_compile}" STREQUAL "" OR "${want_link}" STREQUAL "")
    message(FATAL_ERROR "mode '${MODE}' has no options: nothing to check")
endif()

set(min_units 20)
set(min_executables 5)
set(shown_max 15)

set(missing "")

# ── compile side ────────────────────────────────────────────────────────────────────────────────────
set(db_file "${BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${db_file}")
    message(FATAL_ERROR "${db_file} does not exist: wrong build dir, or CMAKE_EXPORT_COMPILE_COMMANDS is off")
endif()
file(READ "${db_file}" db)
string(JSON units ERROR_VARIABLE error LENGTH "${db}")
if(error)
    message(FATAL_ERROR "${db_file} is not a JSON array: ${error}")
endif()
if(units LESS min_units)
    message(FATAL_ERROR
        "scanned only ${units} compile units in ${db_file}, want at least ${min_units}: wrong build dir, or "
        "CMake's compile_commands.json layout changed")
endif()
math(EXPR last "${units} - 1")
foreach(i RANGE ${last})
    string(JSON entry GET "${db}" ${i})
    string(JSON file ERROR_VARIABLE file_error GET "${entry}" file)
    string(JSON command ERROR_VARIABLE command_error GET "${entry}" command)
    if(command_error)
        list(APPEND missing "compile: ${file}: the entry has no \"command\" (CMake's layout changed?)")
        continue()
    endif()
    set(padded " ${command} ")      # whole options only: -fsanitize=address is not -fsanitize=address,undefined
    set(lacking "")
    foreach(option IN LISTS want_compile)
        string(FIND "${padded}" " ${option} " at)
        if(at EQUAL -1)
            list(APPEND lacking "${option}")
        endif()
    endforeach()
    if(lacking)
        list(JOIN lacking ", " lacking_text)
        list(APPEND missing "compile: ${file}: no ${lacking_text}")
    endif()
endforeach()

# ── link side ───────────────────────────────────────────────────────────────────────────────────────
# Current targets only: a build tree can also hold directories left behind by targets that no longer exist.
set(dirs_file "${BUILD_DIR}/CMakeFiles/TargetDirectories.txt")
if(NOT EXISTS "${dirs_file}")
    message(FATAL_ERROR "${dirs_file} does not exist: wrong build dir, or the generator does not write it")
endif()
file(STRINGS "${dirs_file}" target_dirs)
set(executables 0)
foreach(dir IN LISTS target_dirs)
    if(NOT EXISTS "${dir}/link.txt")
        continue()
    endif()
    file(READ "${dir}/link.txt" command)
    if(NOT command MATCHES " -o ")      # a static library is archived, not linked
        continue()
    endif()
    math(EXPR executables "${executables} + 1")
    string(REGEX REPLACE "[ \t\r\n]+" ";" args "${command}")
    foreach(option IN LISTS want_link)
        if(NOT option IN_LIST args)
            get_filename_component(target "${dir}" NAME_WE)
            list(APPEND missing "link: ${target}: no ${option}")
        endif()
    endforeach()
endforeach()
if(executables LESS min_executables)
    message(FATAL_ERROR
        "scanned only ${executables} executable link lines under ${BUILD_DIR}, want at least ${min_executables}: "
        "wrong build dir, or the generator's link.txt layout changed")
endif()

# ── verdict ─────────────────────────────────────────────────────────────────────────────────────────
if(missing)
    # An option that never reached the tree misses every unit at once; the first few name the cause, and the rest
    # is a hundred lines of the same thing.
    list(LENGTH missing count)
    if(count GREATER shown_max)
        list(SUBLIST missing 0 ${shown_max} shown)
        math(EXPR rest "${count} - ${shown_max}")
        list(APPEND shown "... and ${rest} more")
    else()
        set(shown "${missing}")
    endif()
    list(JOIN shown "\n  " report)
    message(FATAL_ERROR
        "uninstrumented, without the '${MODE}' mode's options (${count} findings in ${units} compile units and "
        "${executables} executable link lines):\n  ${report}")
endif()
message(STATUS
    "test_sanitizer_coverage: ${units} compile units and ${executables} executable link lines, all carry the "
    "'${MODE}' mode's options")
