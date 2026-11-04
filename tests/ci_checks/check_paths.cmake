# check_paths.cmake — test_ci_paths: the paths that CI ignores are documentation, and nothing that a build or a check reads
# (plan 031).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_paths.cmake`. The workflow does not run for a push or a pull request
# whose changed paths all match its paths-ignore list. That is right for documentation, which builds nothing and tests
# nothing, and silently wrong for anything else: an ignored file that a build or a check reads can break in a change that no
# run looked at. So this holds the list to the checkout. It reads the lists under the push and pull_request triggers, and
# fails unless:
#   - both are present, non-empty and equal (a list changed under one trigger and not the other is a hole);
#   - each entry is a file name or <dir>/** and nothing else: the check does not guess what another pattern matches;
#   - each entry matches at least one file in the checkout (a misspelt entry ignores nothing);
#   - no entry covers a file that a check reads, found as ${SOURCE_DIR}/ and a name in tests/**/*.cmake (today README.md,
#     which test_cmake_floor reads, among others), nor a CMakeLists.txt, *.cmake, *.cmake.in, *.cpp, *.hpp or *.inl file,
#     which the build reads.
# It fails if it finds no list, or no file that a check reads, so that a moved key cannot pass by reading nothing.
#
# It reads the workflow line by line and expects its layout: two-space indentation, and a block list under the paths-ignore
# of each trigger. A different layout makes it fail and say so, which is the point.
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

# ── the lists in the workflow ────────────────────────────────────────────────
set(workflow "${SOURCE_DIR}/.github/workflows/cmake-single-platform.yml")
if(NOT EXISTS "${workflow}")
    message(FATAL_ERROR "${workflow} does not exist")
endif()
file(READ "${workflow}" yaml)
# The lines go into a CMake list, so keep the characters that a list gives meaning to out of them.
string(REPLACE ";" "<semicolon>" yaml "${yaml}")
string(REPLACE "[" "<lbracket>" yaml "${yaml}")
string(REPLACE "]" "<rbracket>" yaml "${yaml}")
string(REPLACE "\n" ";" lines "${yaml}")

set(in_on FALSE)
set(trigger "")
set(collecting FALSE)
set(ignore_push "")
set(ignore_pull_request "")
foreach(line IN LISTS lines)
    if(line MATCHES "^[ \t]*(#.*)?$")
        continue()                          # blank, or a comment
    endif()
    if(NOT line MATCHES "^[ \t]")           # a top-level key ends the one before it
        set(in_on FALSE)
        set(trigger "")
        set(collecting FALSE)
        if(line MATCHES "^on:")
            set(in_on TRUE)
        endif()
        continue()
    endif()
    if(NOT in_on)
        continue()
    endif()
    if(line MATCHES "^  ([A-Za-z_]+):")      # a trigger: push, pull_request, ...
        set(trigger "${CMAKE_MATCH_1}")
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
    set(collecting FALSE)                   # any other line in a trigger ends its list
endforeach()

foreach(name IN ITEMS push pull_request)
    if(ignore_${name} STREQUAL "")
        message(FATAL_ERROR "found no paths-ignore list under the ${name} trigger of ${workflow}")
    endif()
endforeach()

set(problems "")

set(sorted_push ${ignore_push})
set(sorted_pull_request ${ignore_pull_request})
list(SORT sorted_push)
list(SORT sorted_pull_request)
if(NOT sorted_push STREQUAL sorted_pull_request)
    string(REPLACE ";" ", " push_text "${ignore_push}")
    string(REPLACE ";" ", " pull_request_text "${ignore_pull_request}")
    string(APPEND problems "  the lists differ: push ignores ${push_text}, pull_request ignores ${pull_request_text}\n")
endif()

# Each entry is a file name or <dir>/**: a rule "dir|<dir>" or "file|<name>".
set(entries ${ignore_push})
list(REMOVE_DUPLICATES entries)
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

if(NOT problems STREQUAL "")
    string(REPLACE ";" ", " entries_text "${entries}")
    message(FATAL_ERROR "the paths-ignore lists in ${workflow} (${entries_text}), but:\n${problems}")
endif()

list(REMOVE_DUPLICATES covered_files)
list(LENGTH covered_files covered_count)
list(LENGTH entries entries_count)
string(REPLACE ";" ", " entries_text "${entries}")
message(STATUS "CI ignores ${entries_count} entries (${entries_text}) under push and pull_request: ${covered_count} files, "
               "none of which a check reads or the build compiles; ${reads_count} files are read by the checks")
