# check_clang_tidy_workflow.cmake — test_ci_clang_tidy: the clang-tidy workflow runs the whole gate, at the LLVM major that the
# project holds it to (plan clang-tidy-ci-job, #49).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_clang_tidy_workflow.cmake`. It reads .github/workflows/clang-tidy.yml,
# cmake/cryptocogs_clang_tidy.cmake and CMakePresets.json. It needs no clang-tidy and no build, and runs in every tree.
#
# A workflow cannot be run here, so this holds what a run would find out late, or not at all. It holds:
#
#   - LLVM_MAJOR is set once, in the workflow's env, and is CRYPTOCOGS_CLANG_TIDY_MAJOR. The apt source, the package and the
#     tool are named by $LLVM_MAJOR, and no line has a literal major in their place. A major moved in one place only is
#     otherwise a red clang_tidy_version after the push.
#   - The configure turns the gate on (-DCRYPTOCOGS_CLANG_TIDY=clang-tidy-$LLVM_MAJOR) and the benchmarks on
#     (-DCRYPTOCOGS_BUILD_BENCHMARKS=ON). Without the second the gate is 88 tests and not 95, the seven benchmark files are
#     unheld, and everything stays green.
#   - Every ctest command selects the gate's tests, -R '^clang_tidy_'. Without it the job would run the whole suite, which
#     nothing has built.
#   - Nothing lets the job pass by failing: no continue-on-error, no `|| true`. A lint step that cannot fail gates nothing.
#   - PRESET is set once, names a configure preset and a test preset of CMakePresets.json, and every --preset argument is that
#     variable, so the tree that is configured is the tree that is tested.
#
# It reads the workflow's code and not its comments (whole-line ones are dropped first), so a rule is not met by a comment,
# and a comment that mentions a rule does not break it. What it does not hold: the triggers and their paths-ignore lists
# (test_ci_paths), and the key of a cache (there is none: plan clang-tidy-ci-job, D1).
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_clang_tidy_workflow.cmake")
endif()

set(workflow "${SOURCE_DIR}/.github/workflows/clang-tidy.yml")
set(module   "${SOURCE_DIR}/cmake/cryptocogs_clang_tidy.cmake")
set(presets  "${SOURCE_DIR}/CMakePresets.json")

set(failures "")
foreach(file IN ITEMS "${workflow}" "${module}" "${presets}")
    if(NOT EXISTS "${file}")
        string(APPEND failures "\n  ${file} does not exist")
    endif()
endforeach()
if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy workflow is wrong:${failures}")
endif()

# ── the major that the project holds the gate to ─────────────────────────────
include("${module}")
if(NOT DEFINED CRYPTOCOGS_CLANG_TIDY_MAJOR OR NOT "${CRYPTOCOGS_CLANG_TIDY_MAJOR}" MATCHES "^[0-9]+$")
    message(FATAL_ERROR "${module} does not set CRYPTOCOGS_CLANG_TIDY_MAJOR to a number")
endif()

# ── the workflow's code ──────────────────────────────────────────────────────
file(READ "${workflow}" yaml)
# Whole-line comments go. Then the characters that a CMake list gives meaning to, so that a line can be an item of one.
string(REGEX REPLACE "\n[ \t]*#[^\n]*" "\n" code "\n${yaml}")
string(REPLACE ";" "<semicolon>" code "${code}")
string(REPLACE "[" "<lbracket>" code "${code}")
string(REPLACE "]" "<rbracket>" code "${code}")

# A reference to a variable of the workflow's env, in the three forms a workflow can write one: $NAME, ${NAME} and
# ${{ env.NAME }}.
function(env_reference name out)
    set(${out} "(\\$${name}|\\$\\{${name}\\}|\\$\\{\\{ *env\\.${name} *\\}\\})" PARENT_SCOPE)
endfunction()
env_reference(LLVM_MAJOR major_ref)
env_reference(PRESET preset_ref)

# One character of the release's codename as the source line writes it: the letters of 'resolute', or $VERSION_CODENAME, or
# ${VERSION_CODENAME}. The class ends in $ because CMake reads a $ followed by a name and { as a reference of a kind it lacks.
set(codename "[A-Za-z_{}$]")

# The value of a variable that the env sets once: the one line, less its key, a trailing comment and quotes. <out> is empty and
# <out>_problem says why when the workflow sets the variable on no line or on several.
function(env_value name out)
    set(${out} "" PARENT_SCOPE)
    string(REGEX MATCHALL "\n[ \t]*${name}:[^\n]*" lines "${code}")
    list(LENGTH lines count)
    if(NOT count EQUAL 1)
        set(${out}_problem "${name} is set on ${count} lines of the workflow, and must be set once, in its env" PARENT_SCOPE)
        return()
    endif()
    string(REGEX REPLACE "^[\n \t]*${name}:[ \t]*" "" value "${lines}")
    string(REGEX REPLACE "[ \t]+#.*$" "" value "${value}")
    string(STRIP "${value}" value)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" value "${value}")
    string(REGEX REPLACE "^'(.*)'$" "\\1" value "${value}")
    set(${out} "${value}" PARENT_SCOPE)
    set(${out}_problem "" PARENT_SCOPE)
endfunction()

# ── the major ────────────────────────────────────────────────────────────────
env_value(LLVM_MAJOR llvm_major)
if(NOT "${llvm_major_problem}" STREQUAL "")
    string(APPEND failures "\n  ${llvm_major_problem}")
elseif(NOT "${llvm_major}" MATCHES "^[0-9]+$")
    string(APPEND failures "\n  LLVM_MAJOR is '${llvm_major}', not a number")
elseif(NOT "${llvm_major}" STREQUAL "${CRYPTOCOGS_CLANG_TIDY_MAJOR}")
    string(APPEND failures
        "\n  LLVM_MAJOR is ${llvm_major} and CRYPTOCOGS_CLANG_TIDY_MAJOR (cmake/cryptocogs_clang_tidy.cmake) is "
        "${CRYPTOCOGS_CLANG_TIDY_MAJOR}: clang_tidy_version would fail the run, so move both in one commit")
endif()

# No line names the tool or the repository with a literal major: they are $LLVM_MAJOR, so that there is one place to move.
string(REGEX MATCH "[^\n]*clang-tidy-[0-9][^\n]*" literal "${code}")
if(NOT "${literal}" STREQUAL "")
    string(STRIP "${literal}" literal)
    string(APPEND failures "\n  a line names the tool with a literal major, where it must say clang-tidy-$LLVM_MAJOR:\n      ${literal}")
endif()
string(REGEX MATCH "[^\n]*llvm-toolchain-${codename}*-[0-9][^\n]*" literal "${code}")
if(NOT "${literal}" STREQUAL "")
    string(STRIP "${literal}" literal)
    string(APPEND failures
        "\n  a line names apt.llvm.org's repository with a literal major, where it must say "
        "llvm-toolchain-<codename>-$LLVM_MAJOR:\n      ${literal}")
endif()

# And the three places that must name it do.
if(NOT code MATCHES "apt-get install[^\n]*clang-tidy-${major_ref}")
    string(APPEND failures "\n  no 'apt-get install' line installs clang-tidy-$LLVM_MAJOR")
endif()
if(NOT code MATCHES "llvm-toolchain-${codename}*-${major_ref}")
    string(APPEND failures "\n  the apt source is not llvm-toolchain-<codename>-$LLVM_MAJOR")
endif()
if(NOT code MATCHES "-DCRYPTOCOGS_CLANG_TIDY=clang-tidy-${major_ref}")
    string(APPEND failures "\n  the configure does not turn the gate on with -DCRYPTOCOGS_CLANG_TIDY=clang-tidy-$LLVM_MAJOR")
endif()

# ── what the job holds ───────────────────────────────────────────────────────
if(NOT code MATCHES "-DCRYPTOCOGS_BUILD_BENCHMARKS=ON")
    string(APPEND failures
        "\n  the configure does not turn the benchmarks on with -DCRYPTOCOGS_BUILD_BENCHMARKS=ON: the gate would be 88 "
        "tests and not 95, and the seven benchmark files would go unheld")
endif()

string(REGEX MATCHALL "[^\n]*ctest[^\n]*" ctest_lines "${code}")
list(LENGTH ctest_lines ctest_count)
if(ctest_count EQUAL 0)
    string(APPEND failures "\n  the workflow never runs ctest")
endif()
foreach(line IN LISTS ctest_lines)
    if(NOT line MATCHES "-R '\\^clang_tidy_'")
        string(STRIP "${line}" shown)
        string(APPEND failures "\n  a ctest command does not select the gate's tests with -R '^clang_tidy_':\n      ${shown}")
    endif()
endforeach()

# ── nothing lets the job pass by failing ─────────────────────────────────────
if(code MATCHES "continue-on-error")
    string(APPEND failures "\n  the workflow says continue-on-error: a step that cannot fail gates nothing")
endif()
if(code MATCHES "\\|\\|[ \t]*(true|:)")
    string(APPEND failures "\n  a line ends in '|| true' (or '|| :'): a command that cannot fail gates nothing")
endif()

# ── the preset ───────────────────────────────────────────────────────────────
env_value(PRESET preset)
if(NOT "${preset_problem}" STREQUAL "")
    string(APPEND failures "\n  ${preset_problem}")
else()
    file(READ "${presets}" presets_json)
    foreach(section IN ITEMS configurePresets testPresets)
        string(JSON entries ERROR_VARIABLE problem LENGTH "${presets_json}" ${section})
        if(problem)
            string(APPEND failures "\n  CMakePresets.json has no ${section} (${problem})")
            continue()
        endif()
        set(found FALSE)
        if(entries GREATER 0)
            math(EXPR last "${entries} - 1")
            foreach(i RANGE ${last})
                string(JSON entry_name GET "${presets_json}" ${section} ${i} name)
                if("${entry_name}" STREQUAL "${preset}")
                    set(found TRUE)
                endif()
            endforeach()
        endif()
        if(NOT found)
            string(APPEND failures "\n  PRESET is '${preset}', which is not the name of any of CMakePresets.json's ${section}")
        endif()
    endforeach()
endif()
# Every --preset is the variable, so that what is configured is what is tested.
string(REGEX MATCHALL "[^\n]*--preset[^\n]*" preset_uses "${code}")
list(LENGTH preset_uses preset_use_count)
if(preset_use_count EQUAL 0)
    string(APPEND failures "\n  the workflow never says --preset")
endif()
foreach(line IN LISTS preset_uses)
    if(NOT line MATCHES "--preset[ \t]+${preset_ref}")
        string(STRIP "${line}" shown)
        string(APPEND failures "\n  a --preset argument is not the PRESET variable of the env:\n      ${shown}")
    endif()
endforeach()

if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "the clang-tidy workflow (${workflow}) is wrong:${failures}")
endif()
message(STATUS "the clang-tidy workflow runs LLVM ${CRYPTOCOGS_CLANG_TIDY_MAJOR} (the project's), on the preset ${preset}, "
               "with the gate and the benchmarks on; its ${ctest_count} ctest command(s) select only clang_tidy_ tests, "
               "and nothing lets it pass by failing")
