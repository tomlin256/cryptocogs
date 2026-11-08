# check_plan_docs.cmake — test_plan_docs: a plan doc carries no sequence number, and everything that names one names a file that
# exists (plan unnumbered-plan-docs, cryptocogs#65).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_plan_docs.cmake`. It fails when
#   - there is no docs/plans directory, or it holds no plan doc (a directory that moved must not pass by checking nothing), or
#   - a file directly in docs/plans (a hidden one such as .DS_Store is skipped) is not named [a-z][a-z0-9-]*.md, or
#   - a file of the checkout says docs/plans/<x> where <x> is not a .md file in docs/plans: a plan that is gone, a misspelt name, a
#     name without its .md, or a partial path such as docs/plans/027, or
#   - a .md file in docs/plans links, relatively, to a .md file that does not exist (a #fragment is ignored, and a link to a URL is
#     not read).
#
# Why. A plan is named for what it does and not numbered: a number reads as an issue number and is not one, and a name with a
# number in front of it sorts by the order of the plans and says nothing about what is in one. So a plan doc is
# docs/plans/<name>.md, a plan is cited as `plan <name>` and a commit subject says `(plan <name>, step N)`. This holds the names
# and every path that points at one, so that a numbered plan cannot come back and a rename cannot leave a dead path behind.
#
# What counts as a mention. The text docs/plans/ followed by the characters of a file name (letters, digits, '_', '.' and '-'),
# less a full stop at its end, which is the end of a sentence. The directory alone (docs/plans/), a pattern (docs/plans/*.md) and a
# placeholder (docs/plans/<name>.md) name no file and pass. Only text files are read (the extensions in TEXT_FILES).
#
# Where it looks. Every file of the checkout, found by glob and named nowhere, so test_ci_paths (which refuses an ignored path
# that a check reads by name) has no objection. A push that changes only docs/** starts no CI run and skips this test; the ctest of
# each checkpoint runs it. build*/ and cmake-build-*/ hold other projects' files, and .git/, .cache/ and .claude/ hold copies (a
# worktree), so they are skipped. tests/doc_checks/ is skipped too: its fixtures hold paths that must not exist.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_plan_docs.cmake")
endif()

set(PLANS_RELATIVE "docs/plans")
set(PLANS "${SOURCE_DIR}/${PLANS_RELATIVE}")
set(PLAN_DOC_NAME "^[a-z][a-z0-9-]*\\.md$")
set(TEXT_FILES "\\.(md|hpp|cpp|inl|cmake|txt|yml|yaml|json|toml|py|sh)$|(^|/)\\.(clang-tidy|clangd)$")

if(NOT IS_DIRECTORY "${PLANS}")
    message(FATAL_ERROR "found no ${PLANS_RELATIVE} directory in ${SOURCE_DIR}: a plan doc is ${PLANS_RELATIVE}/<name>.md, and a "
                        "check that finds no plans to hold would pass")
endif()

set(problems "")

# ── the names ────────────────────────────────────────────────────────────────────────────────────────────────────────
file(GLOB entries LIST_DIRECTORIES true RELATIVE "${PLANS}" "${PLANS}/*")
list(SORT entries)
set(plan_docs "")                       # named as a plan doc is
set(md_files "")                        # every .md file there is, whatever its name: what a path or a link can point at
foreach(entry IN LISTS entries)
    if(entry MATCHES "^\\.")
        continue()
    endif()
    if(entry MATCHES "\\.md$" AND NOT IS_DIRECTORY "${PLANS}/${entry}")
        list(APPEND md_files "${entry}")
    endif()
    if(entry MATCHES "${PLAN_DOC_NAME}" AND NOT IS_DIRECTORY "${PLANS}/${entry}")
        list(APPEND plan_docs "${entry}")
    else()
        string(APPEND problems "  ${PLANS_RELATIVE}/${entry} is not named [a-z][a-z0-9-]*.md: a plan doc has a lower-case name "
                               "that starts with a letter, and no number in front\n")
    endif()
endforeach()
list(LENGTH plan_docs plan_count)
if(plan_count EQUAL 0)
    string(APPEND problems "  ${PLANS_RELATIVE} holds no plan doc: a directory that moved or emptied must not pass by checking "
                           "nothing\n")
endif()

# ── every file of the checkout ───────────────────────────────────────────────────────────────────────────────────────
file(GLOB top RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*")
set(files "")
foreach(entry IN LISTS top)
    if(entry MATCHES "^(build.*|cmake-build-.*|\\.git|\\.cache|\\.claude)$")
        continue()
    endif()
    if(IS_DIRECTORY "${SOURCE_DIR}/${entry}")
        file(GLOB_RECURSE found RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${entry}/*")
        list(APPEND files ${found})
    else()
        list(APPEND files "${entry}")
    endif()
endforeach()
list(SORT files)

# ── what they say about a plan doc ───────────────────────────────────────────────────────────────────────────────────
set(read_count 0)
foreach(file IN LISTS files)
    if(NOT file MATCHES "${TEXT_FILES}" OR file MATCHES "^tests/doc_checks/")
        continue()
    endif()
    math(EXPR read_count "${read_count} + 1")
    file(READ "${SOURCE_DIR}/${file}" text)
    string(REGEX REPLACE "https?://[^ \t\n)>]*" "" text "${text}")
    string(REGEX MATCHALL "${PLANS_RELATIVE}/[A-Za-z0-9_.-]*" mentions "${text}")
    list(REMOVE_DUPLICATES mentions)
    foreach(mention IN LISTS mentions)
        string(REGEX REPLACE "^${PLANS_RELATIVE}/" "" named "${mention}")
        string(REGEX REPLACE "\\.+$" "" named "${named}")
        if(named STREQUAL "" OR named IN_LIST md_files)
            continue()
        endif()
        string(APPEND problems "  ${file} says ${PLANS_RELATIVE}/${named}, which is not a .md file in ${PLANS_RELATIVE}: a path is "
                               "${PLANS_RELATIVE}/<name>.md of a file that exists\n")
    endforeach()
endforeach()

# ── what a plan doc links to ─────────────────────────────────────────────────────────────────────────────────────────
foreach(doc IN LISTS md_files)
    file(READ "${PLANS}/${doc}" rest)
    # The text is taken link by link and never made a list: a match that starts with ']' does not survive being a list element.
    while("${rest}" MATCHES "\\]\\(([^) \t\n;]*)\\)")
        set(target "${CMAKE_MATCH_1}")
        string(FIND "${rest}" "${CMAKE_MATCH_0}" at)
        string(LENGTH "${CMAKE_MATCH_0}" matched_length)
        math(EXPR after "${at} + ${matched_length}")
        string(SUBSTRING "${rest}" ${after} -1 rest)
        string(REGEX REPLACE "#.*$" "" target "${target}")
        if(target STREQUAL "" OR target MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:" OR NOT target MATCHES "\\.md$")
            continue()
        endif()
        get_filename_component(resolved "${PLANS}/${target}" ABSOLUTE)
        if(NOT EXISTS "${resolved}")
            set(report "  ${PLANS_RELATIVE}/${doc} links to ${target}, which does not exist\n")
            string(FIND "${problems}" "${report}" reported)
            if(reported EQUAL -1)
                string(APPEND problems "${report}")
            endif()
        endif()
    endwhile()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "the plan docs break the rule that a plan has a name and no number (${PLANS_RELATIVE}/<name>.md, cited as "
                        "'plan <name>'):\n${problems}")
endif()
message(STATUS "${plan_count} plan docs, each named for what it does; every mention of one and every link between them resolves "
               "(${read_count} text files read)")
