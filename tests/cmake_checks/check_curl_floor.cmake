# check_curl_floor.cmake — test_curl_floor: the project has one libcurl floor, and everything that states one
# states the same.
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_curl_floor.cmake`. src/exchange/common/http_client.cpp
# sets an option that libcurl has from one version on, so with an older libcurl the library does not compile.
# Three places say which version, each to its own reader: the root CMakeLists.txt, where it looks libcurl up
# for whoever builds the library; cmake/cryptocogsConfig.cmake.in, where the installed package looks it up for
# whoever links it; and README.md, for whoever reads before installing anything.
#
# The floor is the version in the root's lookup, which must be there and must be a plain version. The package
# config's lookup must ask for the same one, and README.md must say "libcurl <floor> or later". Otherwise a
# floor that is raised in one place leaves the other two promising a libcurl that no longer builds the library.
#
# A line comment is not a lookup: what follows a # is not read.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail
# here; check_curl_floor_fixtures.cmake runs this script over small checkouts that agree and that disagree.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_curl_floor.cmake")
endif()

# What each call of COMMAND for CURL in FILE asks for, in order: the argument after CURL if it begins with a
# digit (a version, or a range of them), and "none" if the call names no version.
function(curl_versions_in file command out)
    if(NOT EXISTS "${SOURCE_DIR}/${file}")
        message(FATAL_ERROR "found no ${file} in ${SOURCE_DIR}: it is one of the three places that state the libcurl floor")
    endif()
    file(READ "${SOURCE_DIR}/${file}" text)
    string(REGEX REPLACE "#[^\n]*" "" text "${text}")
    set(call_regex "${command}\\([ \t\r\n]*CURL([^)]*)\\)")
    string(REGEX MATCHALL "${call_regex}" calls "${text}")
    set(versions "")
    foreach(call IN LISTS calls)
        string(REGEX REPLACE "${call_regex}" "\\1" arguments "${call}")
        string(REGEX MATCH "^[ \t\r\n]+([0-9][^ \t\r\n]*)" version "${arguments}")
        if(version STREQUAL "")
            list(APPEND versions "none")
        else()
            list(APPEND versions "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out} "${versions}" PARENT_SCOPE)
endfunction()

# ── the floor ────────────────────────────────────────────────────────────────
curl_versions_in("CMakeLists.txt" "find_package" root_versions)
list(LENGTH root_versions root_count)
if(NOT root_count EQUAL 1)
    message(FATAL_ERROR "found ${root_count} calls of find_package for CURL in ${SOURCE_DIR}/CMakeLists.txt; "
                        "the libcurl floor is the version in its one")
endif()
set(floor "${root_versions}")
if(floor STREQUAL "none")
    message(FATAL_ERROR "${SOURCE_DIR}/CMakeLists.txt looks CURL up without a minimum version, so the project "
                        "states no libcurl floor: a libcurl too old for src/exchange/common/http_client.cpp is "
                        "found, and the build fails in the compiler")
endif()
if(NOT floor MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "the libcurl floor in ${SOURCE_DIR}/CMakeLists.txt is '${floor}', not a plain version")
endif()

set(problems "")

# ── the package config ───────────────────────────────────────────────────────
set(config "cmake/cryptocogsConfig.cmake.in")
curl_versions_in("${config}" "find_dependency" config_versions)
list(LENGTH config_versions config_count)
if(NOT config_count EQUAL 1)
    string(APPEND problems "  ${config} has ${config_count} calls of find_dependency for CURL, not one\n")
else()
    if(config_versions STREQUAL "none")
        string(APPEND problems "  ${config} looks CURL up without a minimum version, not ${floor}\n")
    elseif(NOT config_versions STREQUAL floor)
        string(APPEND problems "  ${config} asks for CURL ${config_versions}, not ${floor}\n")
    endif()
endif()

# ── the README ───────────────────────────────────────────────────────────────
if(NOT EXISTS "${SOURCE_DIR}/README.md")
    message(FATAL_ERROR "found no README.md in ${SOURCE_DIR}: it is one of the three places that state the libcurl floor")
endif()
file(READ "${SOURCE_DIR}/README.md" readme)
string(FIND "${readme}" "libcurl ${floor} or later" at)
if(at EQUAL -1)
    string(APPEND problems "  README.md does not say \"libcurl ${floor} or later\"\n")
endif()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "the libcurl floor is ${floor} (CMakeLists.txt), but:\n${problems}")
endif()
message(STATUS "the libcurl floor is ${floor}: CMakeLists.txt, ${config} and README.md all say so")
