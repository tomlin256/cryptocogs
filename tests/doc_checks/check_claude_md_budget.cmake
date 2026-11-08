# check_claude_md_budget.cmake — test_claude_md_budget: CLAUDE.md stays small (plan claude-md-trim, cryptocogs#60).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_claude_md_budget.cmake`. It fails when
#   - the root CLAUDE.md is over 21000 bytes, or
#   - any CLAUDE.md in the checkout is over 38000 characters, or
#   - there is no CLAUDE.md at the top of the checkout (a file that moved must not pass by checking nothing).
#
# Why these numbers. Claude Code loads the root CLAUDE.md whole at the start of every session, so its size is paid every time:
# 21000 bytes is the trimmed file plus room for a new rule. A CLAUDE.md in a subdirectory loads when Claude first reads a file
# there, and past 40000 characters only the start of it is kept: 38000 leaves a margin. The root is counted in bytes (what it
# costs) and every file in characters (what the cut-off counts); an em dash is three bytes and one character. A file that outgrows
# this holds a runbook (move it to docs/) or a copy of what a header, a CMake comment or a check script already says (point at it).
#
# What it does not see. CLAUDE.md is in the paths-ignore lists of the workflows, so a push that changes only CLAUDE.md starts no run
# and skips this test; the next run that starts finds a file over its limit, and the ctest of each plan's checkpoint finds it
# sooner. The script finds the files by their name wherever they are, as check_cmake_floor.cmake finds CMakeLists.txt, and names
# none of them, so test_ci_paths (which refuses an ignored path that a check reads by name) does not object. To hold CLAUDE.md
# on a push that changes only it, take 'CLAUDE.md' out of the four lists and nothing else.
#
# build*/ and cmake-build-*/ hold other projects' files and are skipped, and so are .git/, .cache/ and .claude/ (worktrees there
# hold copies). The limits are constants here and not parameters: check_claude_md_budget_fixtures.cmake holds them.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_claude_md_budget.cmake")
endif()

set(ROOT_BYTES 21000)
set(FILE_CHARS 38000)

# The size of a file in bytes and in UTF-8 characters. string(LENGTH) counts bytes, so the text is read as hex and the characters
# are the bytes that are not continuation bytes: in UTF-8 every byte of 0x80-0xBF continues a character that an earlier byte began.
function(size_of file bytes_out chars_out)
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR bytes "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\1;" byte_list "${hex}")
    list(FILTER byte_list INCLUDE REGEX "^[89ab]")
    list(LENGTH byte_list continuation_bytes)
    math(EXPR chars "${bytes} - ${continuation_bytes}")
    set(${bytes_out} "${bytes}" PARENT_SCOPE)
    set(${chars_out} "${chars}" PARENT_SCOPE)
endfunction()

# ── every CLAUDE.md in the checkout ──────────────────────────────────────────────────────────────────────────────────
file(GLOB top RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*")
set(files "")
foreach(entry IN LISTS top)
    if(entry MATCHES "^(build.*|cmake-build-.*|\\.git|\\.cache|\\.claude)$")
        continue()
    endif()
    if(IS_DIRECTORY "${SOURCE_DIR}/${entry}")
        file(GLOB_RECURSE found RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${entry}/CLAUDE.md")
        list(APPEND files ${found})
    elseif(entry STREQUAL "CLAUDE.md")
        list(APPEND files "${entry}")
    endif()
endforeach()
list(SORT files)

if(NOT "CLAUDE.md" IN_LIST files)
    message(FATAL_ERROR "found no CLAUDE.md at the top of the checkout ${SOURCE_DIR}: the root file is what every session loads, "
                        "and a check that finds nothing to hold would pass")
endif()

# ── the limits ───────────────────────────────────────────────────────────────────────────────────────────────────────
set(problems "")
set(report "")
foreach(file IN LISTS files)
    size_of("${SOURCE_DIR}/${file}" bytes chars)
    string(APPEND report "  ${file}: ${bytes} bytes, ${chars} characters\n")
    if(chars GREATER FILE_CHARS)
        string(APPEND problems "  ${file} is ${chars} characters; a CLAUDE.md is held to ${FILE_CHARS}\n")
    endif()
    if(file STREQUAL "CLAUDE.md" AND bytes GREATER ROOT_BYTES)
        string(APPEND problems "  ${file} is ${bytes} bytes; the root CLAUDE.md is held to ${ROOT_BYTES}\n")
    endif()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "a CLAUDE.md is over its budget (it is loaded into Claude's context, the root by every session):\n"
                        "${problems}Keep rules, commands and traps; move a runbook to docs/ and point at what a header, a CMake "
                        "comment or a check script already says.")
endif()
message(STATUS "every CLAUDE.md is within its budget (root ${ROOT_BYTES} bytes, any file ${FILE_CHARS} characters):\n${report}")
