# check_claude_md_budget_fixtures.cmake — test_claude_md_budget_fixtures: the budget check accepts what it should and
# refuses what it should (plan 044, cryptocogs#60).
#
# Run by ctest as `cmake -DSCRATCH_ROOT=<dir> -P check_claude_md_budget_fixtures.cmake`. It needs no configure and no build
# tree. It makes small checkouts in SCRATCH_ROOT (a CLAUDE.md of an exact size, a nested one, decoys in build trees) and runs
# check_claude_md_budget.cmake over each in a child run, because a refusal is a FATAL_ERROR and that ends a `cmake -P` run
# (check_clang_tidy_module.cmake does the same). A case that must be accepted has to exit 0. A case that must be refused has to
# exit non-zero with a message that names the file, its size and the limit, because that message is what the person who grew
# the file reads.
#
# The cases sit on the real limits: 21000 bytes for the root, 38000 characters for any CLAUDE.md. A limit moved, or a check off by
# one, fails here. The fixtures are made at run time and not kept in the tree: a file of 21,000 bytes would be one more thing to
# maintain and, named CLAUDE.md, one more file for Claude Code to load.
#
# The check script is the one beside this script, so a hand-made change to a scratch copy of the repo must fail on the case it
# breaks. The scratch directory is made fresh and removed at the end.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SCRATCH_ROOT)
    message(FATAL_ERROR "usage: cmake -DSCRATCH_ROOT=<dir> -P check_claude_md_budget_fixtures.cmake")
endif()

set(CHECKER "${CMAKE_CURRENT_LIST_DIR}/check_claude_md_budget.cmake")
if(NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "found no ${CHECKER}: this test runs the budget check, which is missing")
endif()
set(SCRATCH "${SCRATCH_ROOT}/claude_md_budget")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# One byte per character; three bytes per character (the em dash); and four characters of 1, 3, 3 and 2 bytes whose continuation
# bytes (0x80-0xBF) have every high nibble from 8 to b: x, the em dash, the ellipsis and o with a stroke.
set(ONE_BYTE "x")
set(THREE_BYTES "—")
set(MIXED "x—…ø")

# A file of COUNT characters made of UNIT, in a checkout of its own (directories are made).
function(write_file checkout relative unit count)
    set(path "${SCRATCH}/${checkout}/${relative}")
    get_filename_component(dir "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    string(REPEAT "${unit}" ${count} text)
    file(WRITE "${path}" "${text}")
endfunction()

set(failures "")

# Run the check over a checkout. ACCEPT must exit 0 and print every word of WORDS; REFUSE must exit non-zero and say every word
# of WORDS in its error. CMake word-wraps a FATAL_ERROR, so the words are compared and not where the lines break.
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
# Both limits are inclusive. Build trees, CLion's trees, .git, .cache and .claude hold other projects' files or copies of this
# one and are not looked at, whatever they hold.
write_file(at_both_limits CLAUDE.md "${ONE_BYTE}" 21000)
write_file(at_both_limits docs/CLAUDE.md "${ONE_BYTE}" 38000)
write_file(at_both_limits build/release-linux-x86_64/_deps/dep-src/CLAUDE.md "${ONE_BYTE}" 50000)
write_file(at_both_limits cmake-build-debug/CLAUDE.md "${ONE_BYTE}" 50000)
write_file(at_both_limits .git/CLAUDE.md "${ONE_BYTE}" 50000)
write_file(at_both_limits .cache/CLAUDE.md "${ONE_BYTE}" 50000)
write_file(at_both_limits .claude/worktrees/w/CLAUDE.md "${ONE_BYTE}" 50000)
expect_check(at_both_limits ACCEPT WORDS "CLAUDE.md: 21000 bytes, 21000 characters" "docs/CLAUDE.md: 38000 bytes, 38000 characters")

# A nested file is held in characters: 9500 of the 4-character unit are 38000 characters and 85500 bytes.
write_file(nested_in_characters CLAUDE.md "${ONE_BYTE}" 100)
write_file(nested_in_characters docs/CLAUDE.md "${MIXED}" 9500)
expect_check(nested_in_characters ACCEPT WORDS "docs/CLAUDE.md: 85500 bytes, 38000 characters")

# ── refused ──────────────────────────────────────────────────────────────────────────────────────────────────────────
write_file(root_one_byte_over CLAUDE.md "${ONE_BYTE}" 21001)
expect_check(root_one_byte_over REFUSE WORDS "CLAUDE.md" "21001 bytes" "21000")

# The root is held in bytes, the unit its cost is paid in: 7001 em dashes are 21003 bytes and 7001 characters.
write_file(root_in_bytes CLAUDE.md "${THREE_BYTES}" 7001)
expect_check(root_in_bytes REFUSE WORDS "CLAUDE.md" "21003 bytes" "21000")

write_file(nested_one_character_over CLAUDE.md "${ONE_BYTE}" 100)
write_file(nested_one_character_over a/b/CLAUDE.md "${ONE_BYTE}" 38001)
expect_check(nested_one_character_over REFUSE WORDS "a/b/CLAUDE.md" "38001 characters" "38000")

# A multi-byte character counts once, and the 38001st is over.
write_file(nested_one_character_over_multibyte CLAUDE.md "${ONE_BYTE}" 100)
write_file(nested_one_character_over_multibyte a/CLAUDE.md "${MIXED}" 9500)
file(APPEND "${SCRATCH}/nested_one_character_over_multibyte/a/CLAUDE.md" "x")
expect_check(nested_one_character_over_multibyte REFUSE WORDS "a/CLAUDE.md" "38001 characters" "38000")

# A check that finds nothing to hold is a failure, not a pass: a root CLAUDE.md that was moved must not go unnoticed.
write_file(no_root docs/CLAUDE.md "${ONE_BYTE}" 100)
expect_check(no_root REFUSE WORDS "no CLAUDE.md" "top of the checkout")

file(REMOVE_RECURSE "${SCRATCH}")
if(NOT failures STREQUAL "")
    message(FATAL_ERROR "the budget check does not behave as specified:${failures}")
endif()
message(STATUS "the budget check accepts the checkouts at its limits and refuses the ones over them")
