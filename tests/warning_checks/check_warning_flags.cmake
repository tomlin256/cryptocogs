# check_warning_flags.cmake — test_warning_flags: the warning flags that cmake/cryptocogs_warnings.cmake gives the project's
# own targets (plan warnings-as-errors, #47).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -P check_warning_flags.cmake
# It needs no configure and no build tree, so it is registered in every tree and finishes in milliseconds.
#
# The function is pure, so every case is checked in process and compared as a string: the exact flags and their order, since
# -Werror comes last. What it holds:
#
#   - GNU, Clang and AppleClang at the top level get -Wall -Wextra -Wpedantic -Werror=deprecated-declarations, and -Werror after
#     them when the option is true. The option, and the top-level flag, are true and false by CMake's rules for if(): ON, TRUE,
#     1, YES, Y are true, and OFF, 0, FALSE, NO, N, NOTFOUND, a name ending in -NOTFOUND and the empty string are false;
#   - any other compiler gets nothing, with the option on as well, and the match is exact and case-sensitive: an id that merely
#     contains Clang gets nothing until it is added on purpose;
#   - below the top level (a project that fetches cryptocogs) every compiler gets nothing, so a consumer keeps its own flags;
#   - the module does not contain the literal that test_ci_cache_key reads to find a dependency.
#
# What it cannot hold is where the list is applied, which is the other half of the design: test_warning_option configures the
# real root and reads the compile commands.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_warning_flags.cmake")
endif()
set(module "${SOURCE_DIR}/cmake/cryptocogs_warnings.cmake")
include("${module}")

set(failures "")
set(cases 0)

set(four "-Wall;-Wextra;-Wpedantic;-Werror=deprecated-declarations")
set(four_and_werror "${four};-Werror")

# The flags for one set of arguments, compared as a string.
function(expect_flags label compiler top_level werror want)
    cryptocogs_warning_flags("${compiler}" "${top_level}" "${werror}" got)
    math(EXPR n "${cases} + 1")
    set(cases "${n}" PARENT_SCOPE)
    if(NOT "${got}" STREQUAL "${want}")
        string(APPEND failures
            "\n  ${label} (compiler '${compiler}', top level '${top_level}', option '${werror}'): '${got}', want '${want}'")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── the three compilers that get flags ──────────────────────────────────────────────────────────────
foreach(compiler IN ITEMS GNU Clang AppleClang)
    expect_flags("option off"           "${compiler}" ON  OFF "${four}")
    expect_flags("option on"            "${compiler}" ON  ON  "${four_and_werror}")

    # The option is true or false by the rules of if(), not by one spelling.
    foreach(value IN ITEMS ON TRUE 1 YES Y on true yes y 2)
        expect_flags("option true"      "${compiler}" ON  "${value}" "${four_and_werror}")
    endforeach()
    foreach(value IN ITEMS OFF FALSE 0 NO N off false no n NOTFOUND IGNORE "" "foo-NOTFOUND")
        expect_flags("option false"     "${compiler}" ON  "${value}" "${four}")
    endforeach()

    # The same for the top-level flag, which is what keeps a consumer's flags its own.
    foreach(value IN ITEMS ON TRUE 1 YES)
        expect_flags("top level true"   "${compiler}" "${value}" ON "${four_and_werror}")
    endforeach()
    foreach(value IN ITEMS OFF FALSE 0 NO NOTFOUND "")
        expect_flags("top level false"  "${compiler}" "${value}" ON "")
        expect_flags("top level false, option off" "${compiler}" "${value}" OFF "")
    endforeach()
endforeach()

# ── every other compiler, and near misses of the three ──────────────────────────────────────────────
# CMake's ids for compilers that no job builds with, an empty id, and spellings of the three that are not the three: the match
# is exact and case-sensitive.
foreach(compiler IN ITEMS
        MSVC Intel IntelLLVM NVHPC NVIDIA PGI XL Cray ARMClang FujitsuClang TIClang XLClang
        gnu GNUX clang CLANG appleclang "Apple Clang" "GNU Clang" "" "NOTFOUND")
    expect_flags("other compiler, option off" "${compiler}" ON OFF "")
    expect_flags("other compiler, option on"  "${compiler}" ON ON  "")
    expect_flags("other compiler, below the top level" "${compiler}" OFF ON "")
endforeach()

# ── the module's text ───────────────────────────────────────────────────────────────────────────────
# test_ci_cache_key reads the literal call to find the files that pin a dependency, so the module must not contain it, even in
# a comment.
file(READ "${module}" text)
set(literal "FetchContent_Declare")
string(FIND "${text}" "${literal}" at)
if(NOT at EQUAL -1)
    string(APPEND failures "\n  ${module} contains ${literal}, which test_ci_cache_key reads as a dependency")
endif()

if(failures)
    message(FATAL_ERROR "test_warning_flags failed:${failures}")
endif()
message(STATUS "test_warning_flags: ok (${cases} cases: GNU, Clang and AppleClang get the four flags and -Werror with the "
               "option, nothing else and nobody below the top level does)")
