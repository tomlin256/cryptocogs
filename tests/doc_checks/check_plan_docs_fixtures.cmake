# check_plan_docs_fixtures.cmake — test_plan_docs_fixtures: the plan-docs check accepts what it should and refuses what it should
# (plan unnumbered-plan-docs, cryptocogs#65).
#
# Run by ctest as `cmake -DSCRATCH_ROOT=<dir> -P check_plan_docs_fixtures.cmake`. It needs no configure and no build tree. It makes
# small checkouts in SCRATCH_ROOT and runs check_plan_docs.cmake over each in a child run, because a refusal is a FATAL_ERROR and
# that ends a `cmake -P` run (check_claude_md_budget_fixtures.cmake does the same). A case that must be accepted has to exit 0. A
# case that must be refused has to exit non-zero with a message that names the file and the path or link at fault, because that
# message is what the person who numbered a plan, or renamed one and left a path behind, reads.
#
# The fixtures are made at run time and not kept in the tree: a numbered plan doc kept under tests/ would be the thing the check
# refuses, and a path to a plan that does not exist would be a dead path in the checkout. For the same reason the check skips
# tests/doc_checks/.
#
# The check script is the one beside this script, so a hand-made change to a scratch copy of the repo must fail on the case it
# breaks. The scratch directory is made fresh and removed at the end.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SCRATCH_ROOT)
    message(FATAL_ERROR "usage: cmake -DSCRATCH_ROOT=<dir> -P check_plan_docs_fixtures.cmake")
endif()

set(CHECKER "${CMAKE_CURRENT_LIST_DIR}/check_plan_docs.cmake")
if(NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "found no ${CHECKER}: this test runs the plan-docs check, which is missing")
endif()
set(SCRATCH "${SCRATCH_ROOT}/plan_docs")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# A file in a checkout of its own (directories are made). The content is one argument: a function ignores the arguments it has no
# name for, so a text in pieces is joined with string(CONCAT) first.
function(write_file checkout relative content)
    set(path "${SCRATCH}/${checkout}/${relative}")
    get_filename_component(dir "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${path}" "${content}")
endfunction()

set(failures "")

# Run the check over a checkout. ACCEPT must exit 0 and print every word of WORDS; REFUSE must exit non-zero and say every word of
# WORDS in its error. CMake word-wraps a FATAL_ERROR, so the words are compared and not where the lines break.
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

# ── accepted ─────────────────────────────────────────────────────────────────────────────────────────────────────────
# A digit inside a name; a link with a fragment, one to a sibling, one to a file outside docs/plans, one to a URL and one within
# the page; a hidden file; the directory, a pattern and a placeholder, which name no file; a URL that names a plan of another
# repository; a code comment that cites a plan that exists.
string(CONCAT step_2b
    "# Step 2b\n\nSee [the other plan](other-plan.md#risks), [the readme](../../README.md), [a page](https://example.com/page.md) "
    "and [the top](#top).\n")
write_file(accepted docs/plans/step-2b-compat-shim.md "${step_2b}")
write_file(accepted docs/plans/other-plan.md "# Other\n")
write_file(accepted docs/plans/.DS_Store "not a plan doc, and not looked at")
string(CONCAT readme
    "Plans are docs/plans/other-plan.md and docs/plans/step-2b-compat-shim.md. The directory is docs/plans/, a pattern is "
    "docs/plans/*.md and a placeholder is docs/plans/<name>.md. A design in another repository: "
    "[its plan](https://github.com/example/other/blob/main/docs/plans/design_plan.md).\n")
write_file(accepted README.md "${readme}")
write_file(accepted src/a.cpp "// plan other-plan: docs/plans/other-plan.md.\n")
# Other projects' files and copies of this one are not looked at, whatever they say, and neither are the check's own fixtures.
write_file(accepted build/release-linux-x86_64/_deps/dep-src/docs/plans/001-numbered.md "See docs/plans/missing.md.\n")
write_file(accepted cmake-build-debug/notes.md "See docs/plans/missing.md.\n")
write_file(accepted .git/notes.md "See docs/plans/missing.md.\n")
write_file(accepted .cache/notes.md "See docs/plans/missing.md.\n")
write_file(accepted .claude/worktrees/w/notes.md "See docs/plans/missing.md.\n")
write_file(accepted tests/doc_checks/own_fixture.cmake "# See docs/plans/missing.md.\n")
expect_check(accepted ACCEPT WORDS "2 plan docs")

# ── refused: a name ──────────────────────────────────────────────────────────────────────────────────────────────────
# The same file as under build/ above, in the place that counts. The message names it and says what a name is.
write_file(numbered_name docs/plans/001-numbered.md "# Numbered\n")
write_file(numbered_name docs/plans/second.md "# Second\n")
expect_check(numbered_name REFUSE WORDS "docs/plans/001-numbered.md" "[a-z][a-z0-9-]*.md")

write_file(capital_letter docs/plans/Capital.md "# Capital\n")
expect_check(capital_letter REFUSE WORDS "docs/plans/Capital.md")

write_file(not_markdown docs/plans/first.md "# First\n")
write_file(not_markdown docs/plans/notes.txt "notes\n")
expect_check(not_markdown REFUSE WORDS "docs/plans/notes.txt")

write_file(subdirectory docs/plans/first.md "# First\n")
write_file(subdirectory docs/plans/older/second.md "# Second\n")
expect_check(subdirectory REFUSE WORDS "docs/plans/older")

# ── refused: a path ──────────────────────────────────────────────────────────────────────────────────────────────────
write_file(missing_plan docs/plans/first.md "# First\n")
write_file(missing_plan src/a.cpp "// plan gone: see docs/plans/gone.md.\n")
expect_check(missing_plan REFUSE WORDS "src/a.cpp" "docs/plans/gone.md")

# A partial path: the number alone (docs/plans/027 Design decisions) and a name without its .md.
write_file(partial_path docs/plans/first.md "# First\n")
write_file(partial_path src/b.cpp "// docs/plans/027 Design decisions, and docs/plans/first Design decisions.\n")
expect_check(partial_path REFUSE WORDS "src/b.cpp" "docs/plans/027" "docs/plans/first")

# ── refused: a link ──────────────────────────────────────────────────────────────────────────────────────────────────
write_file(missing_sibling docs/plans/first.md "# First\n\nSee [the second](second.md).\n")
expect_check(missing_sibling REFUSE WORDS "docs/plans/first.md" "second.md")

write_file(missing_with_fragment docs/plans/first.md "# First\n\nSee [the second](second.md#risks).\n")
expect_check(missing_with_fragment REFUSE WORDS "docs/plans/first.md" "second.md")

write_file(missing_parent docs/plans/first.md "# First\n\nSee [the readme](../../README.md).\n")
expect_check(missing_parent REFUSE WORDS "docs/plans/first.md" "../../README.md")

# ── refused: nothing to hold ─────────────────────────────────────────────────────────────────────────────────────────
# A check that finds no plans is a failure, not a pass: a directory that moved or emptied must not go unnoticed.
file(MAKE_DIRECTORY "${SCRATCH}/empty_directory/docs/plans")
write_file(empty_directory README.md "No plans.\n")
expect_check(empty_directory REFUSE WORDS "no plan doc")

write_file(only_hidden docs/plans/.DS_Store "not a plan doc")
expect_check(only_hidden REFUSE WORDS "no plan doc")

write_file(no_directory README.md "No docs.\n")
expect_check(no_directory REFUSE WORDS "no docs/plans directory")

file(REMOVE_RECURSE "${SCRATCH}")
if(NOT failures STREQUAL "")
    message(FATAL_ERROR "the plan-docs check does not behave as specified:${failures}")
endif()
message(STATUS "the plan-docs check accepts the checkouts that follow the rule and refuses the ones that break it")
