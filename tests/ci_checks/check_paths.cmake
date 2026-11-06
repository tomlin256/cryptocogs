# check_paths.cmake — test_ci_paths: the paths that CI ignores are documentation, and nothing that a build or a check reads
# (plan 031; every workflow, plan 040).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_paths.cmake`. A workflow does not run for a push or a pull request
# whose changed paths all match its paths-ignore list. That is right for documentation, which builds nothing and tests
# nothing, and silently wrong for anything else: an ignored file that a build or a check reads can break in a change that no
# run looked at. So this holds the lists to the checkout. It reads every .github/workflows/*.yml (and *.yaml) that has a push
# or a pull_request trigger, and fails unless:
#   - each of them has a paths-ignore list under both triggers, and every list is non-empty and equal to every other one, in
#     that workflow and in the rest (a list changed under one trigger, or in one workflow, and not in the others is a hole:
#     one change would start a run of one workflow and not of another);
#   - each entry is a file name or <dir>/** and nothing else: the check does not guess what another pattern matches;
#   - each entry matches at least one file in the checkout (a misspelt entry ignores nothing);
#   - no entry covers a file that a check reads, found as ${SOURCE_DIR}/ and a name in tests/**/*.cmake (today README.md,
#     which test_cmake_floor reads, among others), nor a CMakeLists.txt, *.cmake, *.cmake.in, *.cpp, *.hpp or *.inl file,
#     which the build reads.
# A workflow with neither trigger (sanitizers.yml has a schedule and a dispatch) starts for nothing that a push or a pull
# request changes, so it has nothing to ignore: it is skipped, and the status line says which. The check fails if it finds no
# workflow, none with either trigger, or no file that a check reads, so that a moved key cannot pass by reading nothing.
#
# It reads each workflow line by line and expects its layout: the triggers under a bare `on:`, two-space indentation, and a
# block list under the paths-ignore of each trigger. A workflow in another layout (`on: [push, pull_request]`, four-space
# indentation, a quoted "on" key) makes it fail and say so, and is never skipped: a workflow it cannot read is not one that
# needs no list.
#
# A build tree holds other projects' files, so build*/ and cmake-build-*/ (CLion's, both ignored by git) are skipped, and so
# is .git/.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_paths.cmake")
endif()

# Does a rule ("dir|<dir>" or "file|<name>") cover a path (relative to the checkout)?
function(rule_covers rule path out)
    string(FIND "${rule}" "|" bar)
    string(SUBSTRING "${rule}" 0 ${bar} kind)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${rule}" ${start} -1 base)
    set(${out} FALSE PARENT_SCOPE)
    if(kind STREQUAL "dir")
        string(LENGTH "${base}/" length)
        string(SUBSTRING "${path}" 0 ${length} head)
        if(head STREQUAL "${base}/")
            set(${out} TRUE PARENT_SCOPE)
        endif()
    elseif(path STREQUAL base)
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

# How a rule reads in a message: docs/** or CLAUDE.md.
function(rule_text rule out)
    string(FIND "${rule}" "|" bar)
    string(SUBSTRING "${rule}" 0 ${bar} kind)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${rule}" ${start} -1 base)
    if(kind STREQUAL "dir")
        set(${out} "${base}/**" PARENT_SCOPE)
    else()
        set(${out} "${base}" PARENT_SCOPE)
    endif()
endfunction()

# ── the lists in the workflows ───────────────────────────────────────────────
file(GLOB workflow_files "${SOURCE_DIR}/.github/workflows/*.yml" "${SOURCE_DIR}/.github/workflows/*.yaml")
list(SORT workflow_files)
if("${workflow_files}" STREQUAL "")
    message(FATAL_ERROR "found no workflow (*.yml or *.yaml) under ${SOURCE_DIR}/.github/workflows")
endif()

set(problems "")
set(filtered "")                            # the workflows with a push or a pull_request trigger, relative to the checkout
set(skipped "")                             # the others
set(lists "")                               # "<n>|<workflow>|<trigger>"; the entries of that list are in ignore_<n>_<trigger>
set(n 0)
foreach(workflow IN LISTS workflow_files)
    file(RELATIVE_PATH name "${SOURCE_DIR}" "${workflow}")
    file(READ "${workflow}" yaml)
    # The lines go into a CMake list, so keep the characters that a list gives meaning to out of them.
    string(REPLACE "\r" "" yaml "${yaml}")
    string(REPLACE ";" "<semicolon>" yaml "${yaml}")
    string(REPLACE "[" "<lbracket>" yaml "${yaml}")
    string(REPLACE "]" "<rbracket>" yaml "${yaml}")
    string(REPLACE "\n" ";" lines "${yaml}")

    set(on_layout "")                       # "block": `on:` alone on its line; "inline": `on:` with its value after it
    set(in_on FALSE)
    set(trigger "")
    set(triggers "")
    set(collecting FALSE)
    set(ignore_push "")
    set(ignore_pull_request "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*(#.*)?$")
            continue()                      # blank, or a comment
        endif()
        if(NOT line MATCHES "^[ \t]")       # a top-level key ends the one before it
            set(in_on FALSE)
            set(trigger "")
            set(collecting FALSE)
            if(line MATCHES "^on:[ \t]*(#.*)?$")
                set(in_on TRUE)
                set(on_layout "block")
            elseif(line MATCHES "^on:")
                set(on_layout "inline")
            endif()
            continue()
        endif()
        if(NOT in_on)
            continue()
        endif()
        if(line MATCHES "^  ([A-Za-z_]+):")  # a trigger: push, pull_request, ...
            set(trigger "${CMAKE_MATCH_1}")
            list(APPEND triggers "${trigger}")
            set(collecting FALSE)
            continue()
        endif()
        if(line MATCHES "^    paths-ignore:[ \t]*$")
            set(collecting TRUE)
            continue()
        endif()
        if(collecting AND line MATCHES "^      -[ \t]+(.*)$")
            set(entry "${CMAKE_MATCH_1}")
            string(REGEX REPLACE "[ \t]+#.*$" "" entry "${entry}")      # a comment after the entry
            string(STRIP "${entry}" entry)
            string(REGEX REPLACE "^'(.*)'$" "\\1" entry "${entry}")
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" entry "${entry}")
            if(trigger STREQUAL "push")
                list(APPEND ignore_push "${entry}")
            elseif(trigger STREQUAL "pull_request")
                list(APPEND ignore_pull_request "${entry}")
            endif()
            continue()
        endif()
        set(collecting FALSE)               # any other line in a trigger ends its list
    endforeach()

    # A workflow that cannot be read in this layout is reported, and not skipped.
    if("${on_layout}" STREQUAL "")
        string(APPEND problems "  ${name} has no top-level 'on:' key, which is the layout this check reads\n")
        continue()
    elseif("${on_layout}" STREQUAL "inline")
        string(APPEND problems "  ${name} has its triggers on the line of 'on:'; this check needs the block layout, with 'on:' "
                               "alone and each trigger (push:, pull_request:) on a line of its own under it\n")
        continue()
    elseif("${triggers}" STREQUAL "")
        string(APPEND problems "  ${name} has no trigger under 'on:' that this check can read (two-space indentation, "
                               "'  push:')\n")
        continue()
    endif()

    if("push" IN_LIST triggers OR "pull_request" IN_LIST triggers)
        list(APPEND filtered "${name}")
        foreach(kind IN ITEMS push pull_request)
            if(NOT "${kind}" IN_LIST triggers)
                string(APPEND problems "  ${name} has no ${kind} trigger: a workflow that a push or a pull request starts has "
                                       "both, each with the same paths-ignore list\n")
            elseif("${ignore_${kind}}" STREQUAL "")
                string(APPEND problems "  found no paths-ignore list under the ${kind} trigger of ${name}\n")
            else()
                list(APPEND lists "${n}|${name}|${kind}")
                set(ignore_${n}_${kind} ${ignore_${kind}})
            endif()
        endforeach()
        math(EXPR n "${n} + 1")
    else()
        list(APPEND skipped "${name}")
    endif()
endforeach()

if("${filtered}" STREQUAL "")
    string(APPEND problems "  found no workflow with a push or a pull_request trigger, so there is no list to hold\n")
endif()

# Every list is the same, in a workflow and across them.
set(entries "")
set(first_list "")
set(have_first FALSE)
set(differ FALSE)
set(shown "")
foreach(item IN LISTS lists)
    if(NOT item MATCHES "^([0-9]+)\\|(.*)\\|(push|pull_request)$")
        message(FATAL_ERROR "internal error: '${item}' is not a list this script made")
    endif()
    set(item_n "${CMAKE_MATCH_1}")
    set(item_name "${CMAKE_MATCH_2}")
    set(item_kind "${CMAKE_MATCH_3}")
    set(sorted ${ignore_${item_n}_${item_kind}})
    list(SORT sorted)
    list(APPEND entries ${sorted})
    string(REPLACE ";" ", " sorted_text "${sorted}")
    string(APPEND shown "      ${item_name}, ${item_kind}: ${sorted_text}\n")
    if(NOT have_first)
        set(first_list "${sorted}")
        set(have_first TRUE)
    elseif(NOT "${sorted}" STREQUAL "${first_list}")
        set(differ TRUE)
    endif()
endforeach()
if(differ)
    string(APPEND problems "  the lists differ, and every one must be the same:\n${shown}")
endif()
list(REMOVE_DUPLICATES entries)

# Each entry is a file name or <dir>/**: a rule "dir|<dir>" or "file|<name>".
set(rules "")
foreach(entry IN LISTS entries)
    if(entry MATCHES "^([A-Za-z0-9_./-]+)/\\*\\*$")
        list(APPEND rules "dir|${CMAKE_MATCH_1}")
    elseif(entry MATCHES "^[A-Za-z0-9_./-]+$")
        list(APPEND rules "file|${entry}")
    else()
        string(APPEND problems "  '${entry}' is neither a file name nor <dir>/**, and this check does not guess what "
                               "another pattern matches\n")
    endif()
endforeach()

# ── every file in the checkout ───────────────────────────────────────────────
file(GLOB top RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*")
set(all_files "")
foreach(top_entry IN LISTS top)
    if(top_entry MATCHES "^(build.*|cmake-build-.*|\\.git)$")
        continue()
    endif()
    if(IS_DIRECTORY "${SOURCE_DIR}/${top_entry}")
        file(GLOB_RECURSE found RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/${top_entry}/*")
        list(APPEND all_files ${found})
    else()
        list(APPEND all_files "${top_entry}")
    endif()
endforeach()
list(SORT all_files)

# ── the files that the checks read, by name ──────────────────────────────────
file(GLOB_RECURSE check_scripts RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/tests/*.cmake")
set(reads "")                               # "<name>|<script that reads it>"
foreach(script IN LISTS check_scripts)
    file(READ "${SOURCE_DIR}/${script}" text)
    string(REGEX MATCHALL "\\$\\{SOURCE_DIR\\}/[A-Za-z0-9_./-]+" references "${text}")
    foreach(reference IN LISTS references)
        string(REGEX REPLACE "^\\$\\{SOURCE_DIR\\}/" "" name "${reference}")
        if(NOT name MATCHES "/$")           # a directory is read by the build-input rule below
            list(APPEND reads "${name}|${script}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES reads)
list(LENGTH reads reads_count)
if(reads_count EQUAL 0)
    message(FATAL_ERROR "found no file under ${SOURCE_DIR}/tests that a check script reads as \${SOURCE_DIR}/<name>")
endif()

# ── the rules against the checkout ───────────────────────────────────────────
set(covered_files "")
foreach(rule IN LISTS rules)
    rule_text("${rule}" text)

    set(matched 0)
    set(source_count 0)
    set(source_example "")
    foreach(path IN LISTS all_files)
        rule_covers("${rule}" "${path}" covers)
        if(NOT covers)
            continue()
        endif()
        math(EXPR matched "${matched} + 1")
        list(APPEND covered_files "${path}")
        if(path MATCHES "(^|/)CMakeLists\\.txt$" OR path MATCHES "\\.(cmake|cpp|hpp|inl)$" OR path MATCHES "\\.cmake\\.in$")
            math(EXPR source_count "${source_count} + 1")
            if(source_example STREQUAL "")
                set(source_example "${path}")
            endif()
        endif()
    endforeach()
    if(matched EQUAL 0)
        string(APPEND problems "  ${text} matches no file in the checkout\n")
    endif()
    if(source_count GREATER 0)
        string(APPEND problems "  ${text} covers ${source_count} CMake or C++ source files that the build reads, such as "
                               "${source_example}\n")
    endif()

    foreach(read IN LISTS reads)
        string(FIND "${read}" "|" bar)
        string(SUBSTRING "${read}" 0 ${bar} name)
        math(EXPR start "${bar} + 1")
        string(SUBSTRING "${read}" ${start} -1 script)
        rule_covers("${rule}" "${name}" covers)
        if(covers)
            string(APPEND problems "  ${text} covers ${name}, which ${script} reads\n")
        endif()
    endforeach()
endforeach()

string(REPLACE ";" ", " entries_text "${entries}")
list(LENGTH filtered filtered_count)
set(filtered_names "")
foreach(name IN LISTS filtered)
    get_filename_component(base "${name}" NAME)
    list(APPEND filtered_names "${base}")
endforeach()
string(REPLACE ";" ", " filtered_text "${filtered_names}")
if("${filtered_text}" STREQUAL "")
    set(filtered_text "none")
endif()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "the paths-ignore lists of the workflows that a push or a pull request starts (${filtered_text}), "
                        "which ignore ${entries_text}, but:\n${problems}")
endif()

list(REMOVE_DUPLICATES covered_files)
list(LENGTH covered_files covered_count)
list(LENGTH entries entries_count)
set(skipped_names "")
foreach(name IN LISTS skipped)
    get_filename_component(base "${name}" NAME)
    list(APPEND skipped_names "${base}")
endforeach()
string(REPLACE ";" ", " skipped_text "${skipped_names}")
if("${skipped_text}" STREQUAL "")
    set(skipped_text "none")
endif()
if(filtered_count EQUAL 1)
    set(workflows_word "workflow")
else()
    set(workflows_word "workflows")
endif()
message(STATUS "CI ignores ${entries_count} entries (${entries_text}) under push and pull_request in ${filtered_count} "
               "${workflows_word} (${filtered_text}): ${covered_count} files, none of which a check reads or the build "
               "compiles; ${reads_count} files are read by the checks; skipped, with neither trigger: ${skipped_text}")
