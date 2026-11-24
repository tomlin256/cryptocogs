# check_readme_dependencies.cmake — test_readme_dependencies: the README's Dependencies table says what the build fetches
# (plan docs-and-headers-accurate).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_readme_dependencies.cmake`. CMake fetches every dependency that is not the
# system's with a FetchContent_Declare call (GIT_REPOSITORY and GIT_TAG are the pin), and the README's "Dependencies" table is where
# a reader finds which libraries those are and at which version. A pin that moves in one place and not in the other leaves a reader
# building against a library that the README does not describe. It fails when
#   - the README has no "## Dependencies" section, or
#   - a declaration has no row: a row's first cell links the repository that the declaration names (a trailing ".git" is not part
#     of the address), or
#   - a row's Version cell is not the declaration's GIT_TAG, or
#   - a row that is not a system library (its Version cell does not begin "system") has no link, or no version, or links a
#     repository that nothing fetches, or
#   - a declaration names no GIT_REPOSITORY or no GIT_TAG, or a GIT_TAG that is not a literal (the table cannot be held to it),
#     or names a repository that another declaration names, or
#   - the checkout declares nothing (a SOURCE_DIR that moved must not pass by checking nothing).
# Every problem is listed, so that one run puts the table right.
#
# System libraries (OpenSSL, libcurl, zlib) are rows whose Version cell begins "system". They are not fetched and are not held to
# a declaration; the libcurl floor in that row is held by test_curl_floor.
#
# Where it looks. Every CMakeLists.txt, *.cmake and *.cmake.in of the checkout, which are the files that test_ci_cache_key holds
# the CI cache key to, so a library fetched from a file that the key does not hash is refused there. build*/ and cmake-build-*/
# hold other projects' files, and .git/, .cache/ and .claude/ hold copies, so they are skipped. A line comment is not a call:
# what follows a # is not read. A pin made another way (ExternalProject_Add) is invisible to it, as it is to the cache key, and a
# call whose arguments hold a ")" ends at it.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here;
# check_readme_dependencies_fixtures.cmake runs this script over small checkouts that agree and that disagree.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_readme_dependencies.cmake")
endif()

# A regex for a CMake command name that does not tell FetchContent_Declare from fetchcontent_declare, which CMake does not either.
function(command_regex name out)
    string(LENGTH "${name}" length)
    math(EXPR last "${length} - 1")
    set(regex "")
    foreach(i RANGE ${last})
        string(SUBSTRING "${name}" ${i} 1 character)
        string(TOUPPER "${character}" upper)
        string(TOLOWER "${character}" lower)
        if(upper STREQUAL lower)
            string(APPEND regex "${character}")
        else()
            string(APPEND regex "[${upper}${lower}]")
        endif()
    endforeach()
    set(${out} "${regex}" PARENT_SCOPE)
endfunction()
command_regex("FetchContent_Declare" declare_name)
set(declare_regex "${declare_name}[ \t\r\n]*\\(([^)]*)\\)")

# ── the declarations ─────────────────────────────────────────────────────────
file(GLOB top RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*")
set(files "")
foreach(entry IN LISTS top)
    if(entry MATCHES "^(build.*|cmake-build-.*|\\.git|\\.cache|\\.claude)$")
        continue()
    endif()
    if(IS_DIRECTORY "${SOURCE_DIR}/${entry}")
        file(GLOB_RECURSE found RELATIVE "${SOURCE_DIR}"
             "${SOURCE_DIR}/${entry}/CMakeLists.txt"
             "${SOURCE_DIR}/${entry}/*.cmake"
             "${SOURCE_DIR}/${entry}/*.cmake.in")
        list(APPEND files ${found})
    elseif(entry STREQUAL "CMakeLists.txt" OR entry MATCHES "\\.cmake(\\.in)?$")
        list(APPEND files "${entry}")
    endif()
endforeach()
list(SORT files)

set(problems "")
set(fetched_repositories "")
set(fetched_tags "")
set(fetched_names "")
set(fetched_files "")
foreach(file IN LISTS files)
    file(READ "${SOURCE_DIR}/${file}" text)
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    string(REGEX MATCHALL "${declare_regex}" calls "${text}")
    foreach(call IN LISTS calls)
        string(REGEX REPLACE "${declare_regex}" "\\1" arguments "${call}")
        string(STRIP "${arguments}" arguments)
        string(REGEX REPLACE "[ \t\r\n]+" ";" arguments "${arguments}")
        list(GET arguments 0 name)
        set(where "the declaration of ${name} in ${file}")
        list(FIND arguments GIT_REPOSITORY at_repository)
        list(FIND arguments GIT_TAG at_tag)
        list(LENGTH arguments length)
        math(EXPR after_repository "${at_repository} + 1")
        math(EXPR after_tag "${at_tag} + 1")
        if(at_repository EQUAL -1 OR after_repository EQUAL length)
            string(APPEND problems "  ${where} names no GIT_REPOSITORY, so the table cannot be held to it\n")
            continue()
        endif()
        if(at_tag EQUAL -1 OR after_tag EQUAL length)
            string(APPEND problems "  ${where} names no GIT_TAG, so the table cannot be held to it\n")
            continue()
        endif()
        list(GET arguments ${after_repository} repository)
        list(GET arguments ${after_tag} tag)
        if(NOT tag MATCHES "^[A-Za-z0-9._/+-]+$")
            string(APPEND problems "  ${where} has GIT_TAG '${tag}', which is not a literal that the table can be held to\n")
            continue()
        endif()
        string(REGEX REPLACE "/*(\\.git)?/*$" "" repository "${repository}")
        list(FIND fetched_repositories "${repository}" at_twice)
        if(NOT at_twice EQUAL -1)
            list(GET fetched_files ${at_twice} first_file)
            list(GET fetched_names ${at_twice} first_name)
            string(APPEND problems "  ${where} names ${repository}, which the declaration of ${first_name} in ${first_file} "
                                   "names too\n")
            continue()
        endif()
        list(APPEND fetched_repositories "${repository}")
        list(APPEND fetched_tags "${tag}")
        list(APPEND fetched_names "${name}")
        list(APPEND fetched_files "${file}")
    endforeach()
endforeach()
list(LENGTH fetched_repositories fetched_count)
if(fetched_count EQUAL 0 AND problems STREQUAL "")
    message(FATAL_ERROR "found no FetchContent_Declare call under ${SOURCE_DIR}: is SOURCE_DIR the repo?")
endif()

# ── the table ────────────────────────────────────────────────────────────────
set(readme_file "${SOURCE_DIR}/README.md")
if(NOT EXISTS "${readme_file}")
    message(FATAL_ERROR "found no README.md in ${SOURCE_DIR}: it holds the Dependencies table")
endif()
file(READ "${readme_file}" readme)
string(REPLACE ";" "<semicolon>" readme "${readme}")
set(heading "\n## Dependencies\n")
string(FIND "\n${readme}" "${heading}" at_heading)
if(at_heading EQUAL -1)
    message(FATAL_ERROR "README.md has no \"## Dependencies\" section, so nothing holds the libraries that CMake fetches to it")
endif()
string(LENGTH "${heading}" heading_length)
math(EXPR section_start "${at_heading} + ${heading_length}")
string(SUBSTRING "\n${readme}" ${section_start} -1 section)
string(FIND "${section}" "\n## " at_next_section)
if(NOT at_next_section EQUAL -1)
    string(SUBSTRING "${section}" 0 ${at_next_section} section)
endif()

set(row_repositories "")
string(REGEX MATCHALL "(^|\n)\\|[^\n]*" rows "${section}")
foreach(row IN LISTS rows)
    string(STRIP "${row}" row)
    string(REGEX MATCH "^\\|([^|]*)\\|([^|]*)\\|" cells "${row}")
    if(cells STREQUAL "")
        string(APPEND problems "  the README's row \"${row}\" does not have the Library and Version cells that every row has\n")
        continue()
    endif()
    string(STRIP "${CMAKE_MATCH_1}" library)
    string(STRIP "${CMAKE_MATCH_2}" version)
    # The header row and the one under it are not libraries.
    if(library STREQUAL "Library" OR library MATCHES "^[ :-]*$")
        continue()
    endif()
    if(version MATCHES "^system")
        continue()
    endif()
    string(REGEX REPLACE "\\]\\(.*$" "" shown "${library}")
    string(REGEX REPLACE "^\\[" "" shown "${shown}")
    string(REGEX MATCH "\\]\\(([^)]*)\\)" link "${library}")
    set(repository "${CMAKE_MATCH_1}")
    if(link STREQUAL "")
        string(APPEND problems "  the README's row for ${shown} links no repository, and its Version cell is not \"system\"\n")
        continue()
    endif()
    string(REGEX REPLACE "/*(\\.git)?/*$" "" repository "${repository}")
    list(APPEND row_repositories "${repository}")
    list(FIND fetched_repositories "${repository}" at)
    if(at EQUAL -1)
        string(APPEND problems "  the README's row for ${shown} links ${repository}, which no FetchContent_Declare fetches\n")
        continue()
    endif()
    list(GET fetched_tags ${at} tag)
    list(GET fetched_names ${at} name)
    list(GET fetched_files ${at} file)
    if(version STREQUAL "")
        string(APPEND problems "  the README's row for ${shown} has no version, and ${file} fetches ${name} at ${tag}\n")
    elseif(NOT version STREQUAL tag)
        string(APPEND problems "  the README's row for ${shown} says ${version}, and ${file} fetches ${name} at ${tag}\n")
    endif()
endforeach()

foreach(repository name tag file IN ZIP_LISTS fetched_repositories fetched_names fetched_tags fetched_files)
    list(FIND row_repositories "${repository}" at)
    if(at EQUAL -1)
        string(APPEND problems "  the README has no row for ${repository}, which ${file} fetches as ${name} at ${tag}\n")
    endif()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "the README's Dependencies table does not hold what CMake fetches:\n${problems}")
endif()
message(STATUS "the README's Dependencies table has a row for each FetchContent_Declare call (${fetched_count}), at the version it pins")
