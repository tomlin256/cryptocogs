# check_presets.cmake — test_presets: CMakePresets.json names one build tree per kind and platform, and
# says only what makes each name true (plans 028 and 032).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DHOST_SYSTEM_NAME=<name> [-DPRESETS_DIR=<dir>] -P check_presets.cmake
# It reads <dir>/CMakePresets.json (PRESETS_DIR defaults to SOURCE_DIR) with string(JSON) and fails,
# naming the rule, unless:
#
#   R1  "version" is at least 3, the first with "condition".
#   R2  "base" is hidden and its binaryDir is ${sourceDir}/build/${presetName}, so a preset's name is
#       its tree's name and two presets cannot share a tree.
#   R3  there is one hidden host preset per known host, named <os>-<arch> in the module's own spelling,
#       whose condition is ${hostSystemName} equal to that OS's CMake name (Darwin, Linux).
#   R4  there is one hidden kind preset per kind, and it sets CMAKE_BUILD_TYPE: release to Release,
#       debug to Debug; a sanitizer kind (asan, tsan) sets RelWithDebInfo and CRYPTOCOGS_SANITIZE to its
#       own mode, which cmake/cryptocogs_sanitize.cmake must accept; no other kind sets
#       CRYPTOCOGS_SANITIZE.
#   R5  the visible configure presets are exactly the pairs of a kind and a host, each named
#       <kind>-<os>-<arch> and inheriting base, that kind and that host; none appears twice.
#   R6  each has a build preset and a test preset of the same name, which name it as configurePreset
#       and carry its host's condition; each test preset sets execution.noTestsAction to error, so a
#       test preset that points at a tree with no tests cannot report a green run, execution.timeout
#       to 120, so that a test that blocks when it is broken fails after two minutes and not at the
#       ceiling of the job that runs it (plan 033), and output.outputOnFailure to true.
#   R7  `cmake -S <dir> --list-presets=all` lists, for the configure, build and test presets, exactly
#       the presets in the file that are for HOST_SYSTEM_NAME's OS: one per kind on Darwin, two per
#       kind on Linux, none elsewhere. This is the end-to-end check that CMake agrees with the text of
#       R3, including that a visible preset inherits its host's condition.
#
# CMake rejects a field it does not know ("$comment" is an error), so the JSON cannot carry its own
# rationale: it is here, in the plan, and in CLAUDE.md. A hand-made change to a scratch copy is passed
# in with PRESETS_DIR and must fail on the rule it breaks.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR HOST_SYSTEM_NAME)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "usage: cmake -DSOURCE_DIR=<repo> -DHOST_SYSTEM_NAME=<name> [-DPRESETS_DIR=<dir>] -P check_presets.cmake")
    endif()
endforeach()
if(NOT DEFINED PRESETS_DIR)
    set(PRESETS_DIR "${SOURCE_DIR}")
endif()
include("${SOURCE_DIR}/cmake/cryptocogs_platform.cmake")
include("${SOURCE_DIR}/cmake/cryptocogs_sanitize.cmake")

set(file "${PRESETS_DIR}/CMakePresets.json")
if(NOT EXISTS "${file}")
    message(FATAL_ERROR "test_presets failed:\n  ${file} is missing")
endif()
file(READ "${file}" json)

# What the file must say. A host is <os>:<CMake system name>:<machine as the OS reports it>; a kind is
# <name>:<CMAKE_BUILD_TYPE>:<CRYPTOCOGS_SANITIZE, or empty>.
set(hosts "Darwin:arm64" "Linux:aarch64" "Linux:x86_64")
set(kinds
    "release:Release:"
    "debug:Debug:"
    "asan:RelWithDebInfo:asan"
    "tsan:RelWithDebInfo:tsan")

set(failures "")

# The member of a JSON object (a path of keys), or "" when it is absent.
function(json_member out object)
    string(JSON value ERROR_VARIABLE err GET "${object}" ${ARGN})
    if(err)
        set(value "")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

# The element count of a JSON array member, or 0.
function(json_count out object)
    string(JSON n ERROR_VARIABLE err LENGTH "${object}" ${ARGN})
    if(err)
        set(n 0)
    endif()
    set(${out} "${n}" PARENT_SCOPE)
endfunction()

# The elements of an array member: sets <out> to a list of the preset names in <section>, and
# <out>_json_<i> to each preset's object. Names are unique keys for the lookups below.
function(find_preset out object section name)
    json_count(count "${object}" ${section})
    set(found "")
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON item GET "${object}" ${section} ${i})
            json_member(item_name "${item}" name)
            if(item_name STREQUAL name)
                set(found "${item}")
                break()
            endif()
        endforeach()
    endif()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

# The names of the presets in a section; <visible_only> skips the hidden ones.
function(preset_names out object section visible_only)
    json_count(count "${object}" ${section})
    set(names "")
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON item GET "${object}" ${section} ${i})
            json_member(item_name "${item}" name)
            json_member(hidden "${item}" hidden)
            if(NOT (visible_only AND hidden STREQUAL "ON"))
                list(APPEND names "${item_name}")
            endif()
        endforeach()
    endif()
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

# True when the preset's condition is ${hostSystemName} equal to <os>.
function(condition_is out preset os)
    json_member(type "${preset}" condition type)
    json_member(lhs "${preset}" condition lhs)
    json_member(rhs "${preset}" condition rhs)
    if(type STREQUAL "equals" AND lhs STREQUAL "\${hostSystemName}" AND rhs STREQUAL "${os}")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# True when the preset's "inherits" (a string or an array) contains <name>.
function(inherits_from out preset name)
    set(${out} FALSE PARENT_SCOPE)
    string(JSON type ERROR_VARIABLE err TYPE "${preset}" inherits)
    if(err)
        return()
    endif()
    if(type STREQUAL "STRING")
        json_member(only "${preset}" inherits)
        if(only STREQUAL name)
            set(${out} TRUE PARENT_SCOPE)
        endif()
    else()
        json_count(count "${preset}" inherits)
        if(count GREATER 0)
            math(EXPR last "${count} - 1")
            foreach(i RANGE ${last})
                string(JSON item GET "${preset}" inherits ${i})
                if(item STREQUAL name)
                    set(${out} TRUE PARENT_SCOPE)
                endif()
            endforeach()
        endif()
    endif()
endfunction()

# ── R1 ────────────────────────────────────────────────────────────────────────
json_member(version "${json}" version)
if(NOT version MATCHES "^[0-9]+$" OR version LESS 3)
    string(APPEND failures "\n  R1: version is '${version}', want at least 3")
endif()

# ── R2 ────────────────────────────────────────────────────────────────────────
find_preset(base "${json}" configurePresets base)
json_member(base_hidden "${base}" hidden)
json_member(base_dir "${base}" binaryDir)
if(NOT base_hidden STREQUAL "ON")
    string(APPEND failures "\n  R2: 'base' is not hidden")
endif()
if(NOT base_dir STREQUAL "\${sourceDir}/build/\${presetName}")
    string(APPEND failures
        "\n  R2: base's binaryDir is '${base_dir}', want '\${sourceDir}/build/\${presetName}'")
endif()

# ── R3, R4: the hidden host and kind presets ──────────────────────────────────
set(expected_hosts "")   # name:Darwin|Linux
foreach(host IN LISTS hosts)
    string(REGEX MATCH "^([^:]+):(.+)$" ignored "${host}")
    set(os "${CMAKE_MATCH_1}")
    cryptocogs_platform(tag "${os}" "${CMAKE_MATCH_2}")
    list(APPEND expected_hosts "${tag}:${os}")
    find_preset(preset "${json}" configurePresets "${tag}")
    json_member(hidden "${preset}" hidden)
    if(preset STREQUAL "")
        string(APPEND failures "\n  R3: there is no host preset '${tag}'")
    else()
        if(NOT hidden STREQUAL "ON")
            string(APPEND failures "\n  R3: host preset '${tag}' is not hidden")
        endif()
        condition_is(ok "${preset}" "${os}")
        if(NOT ok)
            string(APPEND failures
                "\n  R3: host preset '${tag}' has no condition \${hostSystemName} equals ${os}")
        endif()
    endif()
endforeach()

set(expected_kinds "")
foreach(kind IN LISTS kinds)
    string(REGEX MATCH "^([^:]+):([^:]+):([^:]*)$" ignored "${kind}")
    set(kind_name "${CMAKE_MATCH_1}")
    set(build_type "${CMAKE_MATCH_2}")
    set(mode "${CMAKE_MATCH_3}")
    list(APPEND expected_kinds "${kind_name}")
    find_preset(preset "${json}" configurePresets "${kind_name}")
    json_member(hidden "${preset}" hidden)
    json_member(got_type "${preset}" cacheVariables CMAKE_BUILD_TYPE)
    json_member(got_mode "${preset}" cacheVariables CRYPTOCOGS_SANITIZE)
    if(preset STREQUAL "")
        string(APPEND failures "\n  R4: there is no kind preset '${kind_name}'")
    else()
        if(NOT hidden STREQUAL "ON")
            string(APPEND failures "\n  R4: kind preset '${kind_name}' is not hidden")
        endif()
        if(NOT got_type STREQUAL build_type)
            string(APPEND failures
                "\n  R4: kind preset '${kind_name}' sets CMAKE_BUILD_TYPE to '${got_type}', want '${build_type}'")
        endif()
        if(NOT got_mode STREQUAL mode)
            string(APPEND failures
                "\n  R4: kind preset '${kind_name}' sets CRYPTOCOGS_SANITIZE to '${got_mode}', want '${mode}'")
        endif()
    endif()
    if(NOT mode STREQUAL "")
        # The module must accept the mode the table names; an unknown one is a FATAL_ERROR there.
        cryptocogs_sanitizer_flags("${mode}" ignored_compile ignored_link)
    endif()
endforeach()

# ── R5, R6: the visible presets, one for every kind and host ──────────────────
set(expected_visible "")   # names, for R5 and R7
foreach(kind IN LISTS expected_kinds)
    foreach(host IN LISTS expected_hosts)
        string(REGEX MATCH "^([^:]+):(.+)$" ignored "${host}")
        set(tag "${CMAKE_MATCH_1}")
        set(os "${CMAKE_MATCH_2}")
        set(name "${kind}-${tag}")
        list(APPEND expected_visible "${name}")

        find_preset(preset "${json}" configurePresets "${name}")
        if(preset STREQUAL "")
            string(APPEND failures "\n  R5: there is no configure preset '${name}'")
        else()
            json_member(hidden "${preset}" hidden)
            if(hidden STREQUAL "ON")
                string(APPEND failures "\n  R5: '${name}' is hidden")
            endif()
            foreach(parent IN ITEMS base "${kind}" "${tag}")
                inherits_from(ok "${preset}" "${parent}")
                if(NOT ok)
                    string(APPEND failures "\n  R5: '${name}' does not inherit '${parent}'")
                endif()
            endforeach()
        endif()

        foreach(section IN ITEMS buildPresets testPresets)
            find_preset(preset "${json}" ${section} "${name}")
            if(preset STREQUAL "")
                string(APPEND failures "\n  R6: there is no ${section} entry '${name}'")
                continue()
            endif()
            json_member(configure "${preset}" configurePreset)
            if(NOT configure STREQUAL name)
                string(APPEND failures
                    "\n  R6: ${section} '${name}' names configurePreset '${configure}', want '${name}'")
            endif()
            condition_is(ok "${preset}" "${os}")
            if(NOT ok)
                string(APPEND failures
                    "\n  R6: ${section} '${name}' has no condition \${hostSystemName} equals ${os}")
            endif()
            if(section STREQUAL "testPresets")
                json_member(no_tests "${preset}" execution noTestsAction)
                json_member(on_failure "${preset}" output outputOnFailure)
                json_member(timeout "${preset}" execution timeout)
                if(NOT no_tests STREQUAL "error")
                    string(APPEND failures
                        "\n  R6: test preset '${name}' sets noTestsAction to '${no_tests}', want 'error'")
                endif()
                if(NOT on_failure STREQUAL "ON")
                    string(APPEND failures "\n  R6: test preset '${name}' does not set outputOnFailure")
                endif()
                if(NOT timeout STREQUAL "120")
                    string(APPEND failures
                        "\n  R6: test preset '${name}' sets execution.timeout to '${timeout}', want 120")
                endif()
            endif()
        endforeach()
    endforeach()
endforeach()

# The visible configure presets are exactly those, none twice.
preset_names(visible "${json}" configurePresets TRUE)
set(sorted_visible "${visible}")
list(SORT sorted_visible)
set(sorted_expected "${expected_visible}")
list(SORT sorted_expected)
if(NOT sorted_visible STREQUAL sorted_expected)
    string(APPEND failures
        "\n  R5: the visible configure presets are '${sorted_visible}', want '${sorted_expected}'")
endif()
set(unique_visible "${visible}")
list(REMOVE_DUPLICATES unique_visible)
list(LENGTH visible n_visible)
list(LENGTH unique_visible n_unique)
if(NOT n_visible EQUAL n_unique)
    string(APPEND failures "\n  R5: a visible configure preset appears twice: '${visible}'")
endif()

# ── R7: CMake's own listing for the host ──────────────────────────────────────
set(host_expected "")
foreach(host IN LISTS expected_hosts)
    string(REGEX MATCH "^([^:]+):(.+)$" ignored "${host}")
    if(CMAKE_MATCH_2 STREQUAL HOST_SYSTEM_NAME)
        foreach(kind IN LISTS expected_kinds)
            list(APPEND host_expected "${kind}-${CMAKE_MATCH_1}")
        endforeach()
    endif()
endforeach()
list(SORT host_expected)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${PRESETS_DIR}" --list-presets=all
    RESULT_VARIABLE list_rc
    OUTPUT_VARIABLE list_out
    ERROR_VARIABLE list_err)
if(NOT list_rc EQUAL 0)
    string(APPEND failures "\n  R7: cmake --list-presets=all failed (${list_rc}): ${list_err}")
else()
    string(REPLACE "\n" ";" lines "${list_out}")
    set(section "")
    foreach(section_name IN ITEMS configure build test)
        set(listed_${section_name} "")
    endforeach()
    foreach(line IN LISTS lines)
        if(line MATCHES "^Available (configure|build|test) presets:")
            set(section "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^Available ")
            set(section "")
        elseif(section AND line MATCHES "^  \"([^\"]+)\"")
            list(APPEND listed_${section} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    foreach(section_name IN ITEMS configure build test)
        set(listed "${listed_${section_name}}")
        list(SORT listed)
        if(NOT listed STREQUAL host_expected)
            string(APPEND failures
                "\n  R7: on ${HOST_SYSTEM_NAME} cmake lists the ${section_name} presets '${listed}', want '${host_expected}'")
        endif()
    endforeach()
endif()

if(failures)
    message(FATAL_ERROR "test_presets failed:${failures}")
endif()
message(STATUS "test_presets: ok")
