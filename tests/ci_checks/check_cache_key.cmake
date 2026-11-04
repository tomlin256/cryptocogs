# check_cache_key.cmake — test_ci_cache_key: the CI cache of the fetched dependencies is keyed over every file that pins
# one (plan 030).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_cache_key.cmake`. The workflow restores build/<preset>/_deps under a
# key that ends in hashFiles(<files>), and on a hit it passes FETCHCONTENT_FULLY_DISCONNECTED=ON, which makes FetchContent
# trust the restored source directories completely. So a dependency whose tag is bumped in a file that the key does not hash
# is built from the old source in a green run, and nothing says so. This holds the list to the checkout: every
# CMakeLists.txt, *.cmake and *.cmake.in that declares a dependency must be in it, and every path in it must be a file
# (hashFiles of a missing file is an empty string, not an error, so a typo would quietly stop covering a file).
#
# It looks for the call by name, in a file's code and not in its comments. A pin added another way (ExternalProject_Add, a
# wrapper macro whose callers hold the tags) is invisible to it. It reads one line of the workflow, the one that sets a key
# beginning deps- and ends in hashFiles(<files>); if that line moves, or is split over several, the check fails and says
# so. It never passes by reading nothing.
#
# A build tree holds other projects' files, so build*/ and cmake-build-*/ (CLion's, both ignored by git) are skipped, and
# so is .git/.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_cache_key.cmake")
endif()

# ── the workflow's key ───────────────────────────────────────────────────────
set(workflow "${SOURCE_DIR}/.github/workflows/cmake-single-platform.yml")
if(NOT EXISTS "${workflow}")
    message(FATAL_ERROR "${workflow} does not exist")
endif()
file(READ "${workflow}" yaml)

# The lines that set a key beginning deps-.
string(REGEX MATCHALL "\n[ \t]*key:[ \t]*deps-[^\n]*" key_lines "\n${yaml}")
list(LENGTH key_lines key_count)
if(NOT key_count EQUAL 1)
    message(FATAL_ERROR "found ${key_count} lines in ${workflow} that set a key beginning deps-; there must be one, on one "
                        "line, ending in hashFiles(<files>)")
endif()

string(REGEX MATCH "hashFiles\\(([^)]*)\\)" call "${key_lines}")
if(call STREQUAL "")
    message(FATAL_ERROR "the deps- key in ${workflow} has no hashFiles(<files>) call:${key_lines}")
endif()

# The files it hashes: the arguments, split on commas, stripped, and unquoted.
set(hashed "")
string(REPLACE "," ";" arguments "${CMAKE_MATCH_1}")
foreach(argument IN LISTS arguments)
    string(STRIP "${argument}" argument)
    string(REGEX REPLACE "^'(.*)'$" "\\1" argument "${argument}")
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" argument "${argument}")
    if(argument MATCHES "[*?]")
        message(FATAL_ERROR "the deps- key hashes '${argument}', a pattern; this check needs the files named one by one, "
                            "so that it can say which one is missing")
    endif()
    list(APPEND hashed "${argument}")
endforeach()
list(LENGTH hashed hashed_count)

# ── every file that may declare a dependency ─────────────────────────────────
file(GLOB top RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*")
set(files "")
foreach(entry IN LISTS top)
    if(entry MATCHES "^(build.*|cmake-build-.*|\\.git)$")
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

set(declaring "")
foreach(file IN LISTS files)
    file(READ "${SOURCE_DIR}/${file}" text)
    # Comments are not code: whole-line ones go, so that a file that only talks about the call is not asked to be hashed.
    string(REGEX REPLACE "\n[ \t]*#[^\n]*" "\n" text "\n${text}")
    string(TOLOWER "${text}" text)
    if(text MATCHES "fetchcontent_declare[ \t\r\n]*\\(")
        list(APPEND declaring "${file}")
    endif()
endforeach()
list(LENGTH declaring declaring_count)
if(declaring_count EQUAL 0)
    message(FATAL_ERROR "found no file under ${SOURCE_DIR} that declares a dependency: is SOURCE_DIR the repo?")
endif()

# ── the two against each other ───────────────────────────────────────────────
set(problems "")
foreach(file IN LISTS declaring)
    list(FIND hashed "${file}" at)
    if(at EQUAL -1)
        string(APPEND problems "  ${file} declares a dependency and is not in the hashFiles list\n")
    endif()
endforeach()
foreach(path IN LISTS hashed)
    if(NOT EXISTS "${SOURCE_DIR}/${path}" OR IS_DIRECTORY "${SOURCE_DIR}/${path}")
        string(APPEND problems "  ${path} is in the hashFiles list and is not a file in the checkout\n")
    endif()
endforeach()

if(NOT problems STREQUAL "")
    string(REPLACE ";" ", " hashed_text "${hashed}")
    message(FATAL_ERROR "the deps- key in ${workflow} hashes ${hashed_text}, but:\n${problems}")
endif()
message(STATUS "the deps- cache key hashes ${hashed_count} files, and the ${declaring_count} that declare a dependency "
               "are among them")
