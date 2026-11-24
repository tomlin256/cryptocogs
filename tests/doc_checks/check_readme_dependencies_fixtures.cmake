# check_readme_dependencies_fixtures.cmake — test_readme_dependencies_fixtures: the Dependencies table check accepts what it
# should and refuses what it should (plan docs-and-headers-accurate).
#
# Run by ctest as `cmake -DSCRATCH_ROOT=<dir> -P check_readme_dependencies_fixtures.cmake`. It needs no configure and no build
# tree. It makes small checkouts in SCRATCH_ROOT, each the two CMake files that declare dependencies (CMakeLists.txt and
# tests/CMakeLists.txt) and a README.md, and runs check_readme_dependencies.cmake over each in a child run, because a refusal is
# a FATAL_ERROR and that ends a `cmake -P` run. A case that must be accepted has to exit 0 and say how many declarations it held
# the table to. A case that must be refused has to exit non-zero with a message that names the row or the declaration at fault
# and the version or the repository it should say, because that message is what the person who bumped a pin reads.
#
# The check script is the one beside this script, so a hand-made change to a scratch copy of the repo must fail on the case it
# breaks. The scratch directory is made fresh and removed at the end.
#
# The call is spelled out of a variable below: test_ci_cache_key refuses a file that makes the call and is not among the files
# that the CI cache key hashes, and it cannot tell a fixture's text from a call.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED SCRATCH_ROOT)
    message(FATAL_ERROR "usage: cmake -DSCRATCH_ROOT=<dir> -P check_readme_dependencies_fixtures.cmake")
endif()

set(CHECKER "${CMAKE_CURRENT_LIST_DIR}/check_readme_dependencies.cmake")
if(NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "found no ${CHECKER}: this test runs the Dependencies table check, which is missing")
endif()
set(SCRATCH "${SCRATCH_ROOT}/readme_dependencies")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

set(CALL "FetchContent_Declare")
string(TOLOWER "${CALL}" CALL_LOWER)

# One declaration, as the project writes it.
function(declaration out name repository tag)
    set(${out} "${CALL}(\n    ${name}\n    GIT_REPOSITORY ${repository}\n    GIT_TAG        ${tag}\n)\n" PARENT_SCOPE)
endfunction()

# A checkout of the three files. ROOT is the text of CMakeLists.txt, TESTS of tests/CMakeLists.txt and README of README.md; each
# is written as given.
function(write_checkout checkout)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ROOT;TESTS;README" "")
    file(MAKE_DIRECTORY "${SCRATCH}/${checkout}/tests")
    file(WRITE "${SCRATCH}/${checkout}/CMakeLists.txt" "${arg_ROOT}")
    file(WRITE "${SCRATCH}/${checkout}/tests/CMakeLists.txt" "${arg_TESTS}")
    file(WRITE "${SCRATCH}/${checkout}/README.md" "${arg_README}")
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

# ── what the project's files look like ───────────────────────────────────────────────────────────────────────────────
set(JSON_REPOSITORY "https://github.com/nlohmann/json.git")
set(WS_REPOSITORY   "https://github.com/machinezone/IXWebSocket.git")
set(CLI_REPOSITORY  "https://github.com/CLIUtils/CLI11.git")
declaration(DECLARE_JSON nlohmann_json "${JSON_REPOSITORY}" v3.12.0)
declaration(DECLARE_WS   ixwebsocket   "${WS_REPOSITORY}"   v12.1.0)
declaration(DECLARE_CLI  CLI11         "${CLI_REPOSITORY}"  v2.4.2)
string(CONCAT ROOT_OF_TWO "${DECLARE_JSON}" "\n" "${DECLARE_WS}")

string(CONCAT README_HEAD "# lib\n\nA library.\n\n## Dependencies\n\nCMake fetches these; the pins are in CMakeLists.txt.\n\n"
       "| Library | Version | Needed by |\n|---|---|---|\n")
set(ROW_JSON "| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | the library |\n")
set(ROW_WS   "| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | v12.1.0 | examples; tests |\n")
set(ROW_CLI  "| [CLI11](https://github.com/CLIUtils/CLI11) | v2.4.2 | the examples |\n")
set(ROWS_SYSTEM "| OpenSSL | system | the library |\n| libcurl | system (libcurl 7.80.0 or later) | the REST clients |\n")
set(README_TAIL "\n## License\n\nMIT\n")
string(CONCAT README_AGREES "${README_HEAD}" "${ROW_JSON}" "${ROW_WS}" "${ROW_CLI}" "${ROWS_SYSTEM}" "${README_TAIL}")

# ── accepted ─────────────────────────────────────────────────────────────────────────────────────────────────────────
# Two declarations in one file, one in the other, a row for each, system rows that are not held to anything, a ";" in a cell, and a
# section after the table.
write_checkout(table_agrees ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${README_AGREES}")
expect_check(table_agrees ACCEPT WORDS "FetchContent_Declare call (3)")

# A declaration that is only in a comment is not one: the table needs no row for it. Nor is a pin that a comment inside a call
# holds, or the text after a # on the line of the call.
string(CONCAT root_with_comments
    "# ${CALL}(\n#     CLI11\n#     GIT_REPOSITORY ${CLI_REPOSITORY}\n#     GIT_TAG v2.4.0\n# )\n"
    "${CALL}(\n    ixwebsocket  # the tag below is the pin\n    GIT_REPOSITORY ${WS_REPOSITORY}\n"
    "    # GIT_TAG v12.0.0\n    GIT_TAG        v12.1.0  # not v12.0.0\n)  # ${CALL}(x GIT_REPOSITORY y GIT_TAG z)\n")
string(CONCAT readme_ws_only "${README_HEAD}" "${ROW_WS}" "${README_TAIL}")
write_checkout(declarations_in_comments ROOT "${root_with_comments}" TESTS "# nothing here\n" README "${readme_ws_only}")
expect_check(declarations_in_comments ACCEPT WORDS "FetchContent_Declare call (1)")

# The tag is whatever the project pins, not a version number that the check knows: a commit hash is a pin too.
declaration(DECLARE_HASH tool https://github.com/example/tool 0123456789abcdef0123456789abcdef01234567)
string(CONCAT readme_hash "${README_HEAD}" "| [tool](https://github.com/example/tool) | 0123456789abcdef0123456789abcdef01234567 | x |\n")
write_checkout(tag_is_a_hash ROOT "${DECLARE_HASH}" TESTS "" README "${readme_hash}")
expect_check(tag_is_a_hash ACCEPT WORDS "FetchContent_Declare call (1)")

# ── refused: the table says another version ──────────────────────────────────────────────────────────────────────────
string(REPLACE "v12.1.0 | examples" "v11.4.6 | examples" README_WS_STALE "${README_AGREES}")
write_checkout(pin_is_stale_in_the_root ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${README_WS_STALE}")
expect_check(pin_is_stale_in_the_root REFUSE
             WORDS "the README's row for IXWebSocket says v11.4.6" "CMakeLists.txt fetches ixwebsocket at v12.1.0")

string(REPLACE "v2.4.2 | the examples" "v2.4.1 | the examples" README_CLI_STALE "${README_AGREES}")
write_checkout(pin_is_stale_in_the_tests_file ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${README_CLI_STALE}")
expect_check(pin_is_stale_in_the_tests_file REFUSE
             WORDS "the README's row for CLI11 says v2.4.1" "tests/CMakeLists.txt fetches CLI11 at v2.4.2")

# The same pin bumped in the file and not in the table, which is how it happens.
declaration(DECLARE_JSON_BUMPED nlohmann_json "${JSON_REPOSITORY}" v3.13.0)
string(CONCAT root_bumped "${DECLARE_JSON_BUMPED}" "\n" "${DECLARE_WS}")
write_checkout(pin_is_bumped ROOT "${root_bumped}" TESTS "${DECLARE_CLI}" README "${README_AGREES}")
expect_check(pin_is_bumped REFUSE
             WORDS "the README's row for nlohmann/json says v3.12.0" "CMakeLists.txt fetches nlohmann_json at v3.13.0")

# A call written in lower case is the same call.
string(CONCAT declare_ws_lower "${CALL_LOWER}(\n    ixwebsocket\n    GIT_REPOSITORY ${WS_REPOSITORY}\n    GIT_TAG        v12.1.0\n)\n")
string(CONCAT root_lower "${DECLARE_JSON}" "\n" "${declare_ws_lower}")
write_checkout(lower_case_call_is_seen ROOT "${root_lower}" TESTS "${DECLARE_CLI}" README "${README_WS_STALE}")
expect_check(lower_case_call_is_seen REFUSE WORDS "the README's row for IXWebSocket says v11.4.6")

# A row without a version says nothing about the pin.
string(REPLACE "| v2.4.2 | the examples" "|  | the examples" README_NO_VERSION "${README_AGREES}")
write_checkout(row_has_no_version ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${README_NO_VERSION}")
expect_check(row_has_no_version REFUSE WORDS "the README's row for CLI11 has no version" "tests/CMakeLists.txt fetches CLI11 at v2.4.2")

# ── refused: a library and a row do not meet ─────────────────────────────────────────────────────────────────────────
string(CONCAT readme_no_cli "${README_HEAD}" "${ROW_JSON}" "${ROW_WS}" "${ROWS_SYSTEM}" "${README_TAIL}")
write_checkout(library_has_no_row ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_no_cli}")
expect_check(library_has_no_row REFUSE
             WORDS "the README has no row for https://github.com/CLIUtils/CLI11"
                   "which tests/CMakeLists.txt fetches as CLI11 at v2.4.2")

string(CONCAT readme_extra_row "${README_HEAD}" "${ROW_JSON}" "${ROW_WS}" "${ROW_CLI}"
       "| [spdlog](https://github.com/gabime/spdlog) | v1.17.0 | the examples |\n" "${README_TAIL}")
write_checkout(row_has_no_library ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_extra_row}")
expect_check(row_has_no_library REFUSE
             WORDS "the README's row for spdlog links https://github.com/gabime/spdlog, which no FetchContent_Declare fetches")

string(CONCAT readme_unlinked_row "${README_HEAD}" "${ROW_JSON}" "${ROW_WS}" "| CLI11 | v2.4.2 | the examples |\n" "${README_TAIL}")
write_checkout(row_has_no_link ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_unlinked_row}")
expect_check(row_has_no_link REFUSE
             WORDS "the README's row for CLI11 links no repository" "no row for https://github.com/CLIUtils/CLI11")

# A row of one cell has no version to compare, and is not skipped as if it were not a row.
string(CONCAT readme_short_row "${README_HEAD}" "${ROW_JSON}" "${ROW_WS}" "| CLI11 |\n" "${README_TAIL}")
write_checkout(row_has_one_cell ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_short_row}")
expect_check(row_has_one_cell REFUSE
             WORDS "the README's row \"| CLI11 |\" does not have the Library and Version cells")

# ── refused: no table ────────────────────────────────────────────────────────────────────────────────────────────────
write_checkout(table_is_missing ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "# lib\n\nSee CMakeLists.txt.\n")
expect_check(table_is_missing REFUSE WORDS "README.md has no \"## Dependencies\" section")

string(REPLACE "## Dependencies" "## Requirements" README_RENAMED "${README_AGREES}")
write_checkout(heading_is_renamed ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${README_RENAMED}")
expect_check(heading_is_renamed REFUSE WORDS "README.md has no \"## Dependencies\" section")

# A table in another section is not the Dependencies table.
string(CONCAT readme_other_section "# lib\n\n## Dependencies\n\nSee below.\n\n## Notes\n\n"
       "| Library | Version | Needed by |\n|---|---|---|\n" "${ROW_JSON}" "${ROW_WS}" "${ROW_CLI}")
write_checkout(table_is_in_another_section ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_other_section}")
expect_check(table_is_in_another_section REFUSE WORDS "the README has no row for https://github.com/nlohmann/json")

# ── refused: a declaration that the table cannot be held to ──────────────────────────────────────────────────────────
write_checkout(declaration_has_no_tag
    ROOT "${CALL}(\n    tool\n    GIT_REPOSITORY https://github.com/example/tool\n)\n" TESTS "" README "${README_AGREES}")
expect_check(declaration_has_no_tag REFUSE WORDS "the declaration of tool in CMakeLists.txt names no GIT_TAG")

write_checkout(declaration_has_no_repository
    ROOT "${CALL}(\n    tool\n    URL https://example.com/tool.tar.gz\n)\n" TESTS "" README "${README_AGREES}")
expect_check(declaration_has_no_repository REFUSE WORDS "the declaration of tool in CMakeLists.txt names no GIT_REPOSITORY")

write_checkout(tag_is_a_variable
    ROOT "${CALL}(\n    tool\n    GIT_REPOSITORY https://github.com/example/tool\n    GIT_TAG \${TOOL_PIN}\n)\n" TESTS ""
    README "${README_AGREES}")
expect_check(tag_is_a_variable REFUSE WORDS "the declaration of tool in CMakeLists.txt has GIT_TAG '\${TOOL_PIN}'" "not a literal")

write_checkout(repository_is_declared_twice ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_WS}" README "${README_AGREES}")
expect_check(repository_is_declared_twice REFUSE
             WORDS "the declaration of ixwebsocket in tests/CMakeLists.txt names https://github.com/machinezone/IXWebSocket"
                   "the declaration of ixwebsocket in CMakeLists.txt names too")

write_checkout(nothing_is_declared ROOT "# no dependencies\n" TESTS "" README "${README_AGREES}")
expect_check(nothing_is_declared REFUSE WORDS "found no FetchContent_Declare call")

# ── refused: every problem is named, so that one run puts the table right ────────────────────────────────────────────
string(CONCAT readme_three_problems "${README_HEAD}" "| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.0 | the library |\n"
       "| [spdlog](https://github.com/gabime/spdlog) | v1.17.0 | the examples |\n" "${README_TAIL}")
write_checkout(every_problem_is_named ROOT "${ROOT_OF_TWO}" TESTS "${DECLARE_CLI}" README "${readme_three_problems}")
expect_check(every_problem_is_named REFUSE
             WORDS "the README's row for nlohmann/json says v3.11.0"
                   "the README's row for spdlog links https://github.com/gabime/spdlog"
                   "the README has no row for https://github.com/machinezone/IXWebSocket"
                   "the README has no row for https://github.com/CLIUtils/CLI11")

file(REMOVE_RECURSE "${SCRATCH}")
if(NOT failures STREQUAL "")
    message(FATAL_ERROR "the Dependencies table check does not behave as specified:${failures}")
endif()
message(STATUS "the Dependencies table check accepts the checkouts whose table agrees with the declarations and refuses the ones where it does not")
