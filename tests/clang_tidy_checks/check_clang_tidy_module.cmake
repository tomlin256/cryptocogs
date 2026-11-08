# check_clang_tidy_module.cmake — test_clang_tidy_module: the functions in cmake/cryptocogs_clang_tidy.cmake, which
# register the opt-in clang-tidy test step (plan clang-tidy-opt-in-test-step, #34).
#
# Run by ctest as
#   cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -P check_clang_tidy_module.cmake
# It needs no configure, no build tree and no clang-tidy, so it is registered in every build and finishes in about a
# second.
#
# Accepted inputs are checked in process. A refusal is a FATAL_ERROR, and that ends a `cmake -P` run, so each one is
# tried in a child run of this same script (-DCHILD=<function> and its arguments) whose exit status and message are
# then checked. The message is part of the contract: it must name what was given and say what is wrong, because a
# configure error is read by the person who typed the value. CMake word-wraps a FATAL_ERROR at about 78 columns, so the
# words are compared, not where the lines break.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and must fail here.
# The scratch directory is made fresh and removed at the end.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -DSCRATCH_ROOT=<dir> -P check_clang_tidy_module.cmake")
    endif()
endforeach()
include("${SOURCE_DIR}/cmake/cryptocogs_clang_tidy.cmake")
set(SCRATCH "${SCRATCH_ROOT}/clang_tidy_module")

# A stand-in xcrun in a directory of its own, to be the whole PATH, so that every way the real one can answer is tried
# on every host. Sets <dir_out> to the directory.
#
#   prints   answers `--show-sdk-path` with a path.
#   fails    prints a path and then exits 1, so a failure that leaves output behind is not taken for an answer.
#   silent   exits 0 and prints nothing.
#   absent   an empty directory: no xcrun at all. The PATH is this directory and not an empty string, because an unset
#            PATH means the C library's default, which finds /usr/bin/xcrun.
function(stub_xcrun mode dir_out)
    set(dir "${SCRATCH}/xcrun_${mode}")
    file(MAKE_DIRECTORY "${dir}")
    if(mode STREQUAL "prints")
        file(WRITE "${dir}/xcrun" "#!/bin/sh\necho /the/stub/sdk\n")
    elseif(mode STREQUAL "fails")
        file(WRITE "${dir}/xcrun" "#!/bin/sh\necho /partial/sdk\nexit 1\n")
    elseif(mode STREQUAL "silent")
        file(WRITE "${dir}/xcrun" "#!/bin/sh\nexit 0\n")
    endif()
    if(EXISTS "${dir}/xcrun")
        file(CHMOD "${dir}/xcrun" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endif()
    set(${dir_out} "${dir}" PARENT_SCOPE)
endfunction()

# Child run: call one function with the arguments on the command line, and let any FATAL_ERROR end the process.
if(DEFINED CHILD)
    if(CHILD STREQUAL "tool")
        cryptocogs_clang_tidy_tool("${REQUESTED}" "${GENERATOR}" ignored)
    elseif(CHILD STREQUAL "sdk")
        stub_xcrun("${XCRUN}" dir)
        set(ENV{PATH} "${dir}")
        cryptocogs_clang_tidy_sdk(TRUE "" ignored)
    elseif(CHILD STREQUAL "test_name")
        cryptocogs_clang_tidy_test_name("${ROOT}" "${SOURCE}" ignored)
    elseif(CHILD STREQUAL "select")
        # The candidates come bar-separated, because a semicolon would not survive the command line as one argument.
        string(REPLACE "|" ";" candidates "${CANDIDATES}")
        cryptocogs_clang_tidy_select("${ROOT}" "${candidates}" ignored)
    else()
        message(FATAL_ERROR "unknown CHILD '${CHILD}'")
    endif()
    return()
endif()

set(SELF "${CMAKE_CURRENT_LIST_FILE}")
set(failures "")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

# An accepted result: the value, as a string.
function(expect_equal what got want)
    if(NOT "${got}" STREQUAL "${want}")
        string(APPEND failures "\n  ${what}: got '${got}', want '${want}'")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# A call that must be refused: a non-zero exit, and every word of WORDS in the message. CHILD is the command-line
# arguments that make the child run call the function.
function(expect_rejected what)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "CHILD;WORDS")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}" "-DSCRATCH_ROOT=${SCRATCH_ROOT}" ${arg_CHILD} -P "${SELF}"
        RESULT_VARIABLE status
        OUTPUT_QUIET
        ERROR_VARIABLE message)
    if("${status}" STREQUAL "0")
        string(APPEND failures "\n  ${what}: was accepted, want a configure error")
    else()
        string(REGEX REPLACE "[ \t\r\n]+" " " words "${message}")
        foreach(word IN LISTS arg_WORDS)
            string(FIND "${words}" "${word}" at)
            if(at EQUAL -1)
                string(APPEND failures "\n  ${what}: the error does not say '${word}':\n${message}")
            endif()
        endforeach()
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── cryptocogs_clang_tidy_tool ──────────────────────────────────────────────────────────────────────────────────────
# Empty is off, and is never refused, whatever the generator: the default configure must not notice this module.
cryptocogs_clang_tidy_tool("" "Unix Makefiles" got)
expect_equal("tool: empty request" "${got}" "")
cryptocogs_clang_tidy_tool("" "Xcode" got)
expect_equal("tool: empty request under a generator that writes no compile_commands.json" "${got}" "")

# A name on PATH resolves to a path that exists; a path is accepted as it is.
foreach(generator IN ITEMS "Unix Makefiles" "Ninja")
    cryptocogs_clang_tidy_tool("sh" "${generator}" got)
    cmake_path(GET got FILENAME leaf)
    expect_equal("tool: sh under ${generator}, its file name" "${leaf}" "sh")
    if(NOT EXISTS "${got}")
        string(APPEND failures "\n  tool: sh under ${generator} resolved to '${got}', which does not exist")
    endif()
endforeach()
cryptocogs_clang_tidy_tool("${CMAKE_COMMAND}" "Unix Makefiles" got)
expect_equal("tool: a path" "${got}" "${CMAKE_COMMAND}")

expect_rejected("tool: a command no host has"
    CHILD -DCHILD=tool -DREQUESTED=no-such-clang-tidy-for-cryptocogs "-DGENERATOR=Unix Makefiles"
    WORDS "CRYPTOCOGS_CLANG_TIDY=no-such-clang-tidy-for-cryptocogs" "not a command or a path")
# Xcode and Visual Studio write no compile_commands.json, and neither does Ninja Multi-Config in the one file the
# tests read, so all three are refused with the generator named.
foreach(generator IN ITEMS "Xcode" "Visual Studio 17 2022" "Ninja Multi-Config")
    expect_rejected("tool: ${generator}"
        CHILD -DCHILD=tool -DREQUESTED=sh "-DGENERATOR=${generator}"
        WORDS "CRYPTOCOGS_CLANG_TIDY=sh" "compile_commands.json" "${generator}")
endforeach()

# ── cryptocogs_clang_tidy_sdk ───────────────────────────────────────────────────────────────────────────────────────
# The SDK is only named when the target is Apple and the compile commands carry no -isysroot of their own.
cryptocogs_clang_tidy_sdk(FALSE "" got)
expect_equal("sdk: not Apple" "${got}" "")
cryptocogs_clang_tidy_sdk(FALSE "/some/sdk" got)
expect_equal("sdk: not Apple, with a sysroot" "${got}" "")
cryptocogs_clang_tidy_sdk(TRUE "/some/sdk" got)
expect_equal("sdk: Apple with a sysroot, which the compile commands already carry" "${got}" "")
# With no sysroot, an Apple target asks xcrun, and takes what it prints, less the newline.
stub_xcrun(prints dir)
set(path_before "$ENV{PATH}")
set(ENV{PATH} "${dir}")
cryptocogs_clang_tidy_sdk(TRUE "" got)
set(ENV{PATH} "${path_before}")
expect_equal("sdk: Apple, no sysroot, xcrun answers" "${got}" "/the/stub/sdk")
if(CMAKE_HOST_APPLE)
    # And the real one agrees with itself, on the host that has it.
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE oracle OUTPUT_STRIP_TRAILING_WHITESPACE)
    cryptocogs_clang_tidy_sdk(TRUE "" got)
    expect_equal("sdk: Apple, no sysroot, the real xcrun" "${got}" "${oracle}")
    if(NOT IS_DIRECTORY "${got}")
        string(APPEND failures "\n  sdk: Apple, no sysroot, the real xcrun: '${got}' is not a directory")
    endif()
endif()
# Anything but an answer is an error that says what to do.
foreach(xcrun IN ITEMS absent fails silent)
    expect_rejected("sdk: Apple, no sysroot, xcrun ${xcrun}"
        CHILD -DCHILD=sdk -DXCRUN=${xcrun}
        WORDS "xcrun --show-sdk-path" "no SDK" "CMAKE_OSX_SYSROOT")
endforeach()

# ── cryptocogs_clang_tidy_args ──────────────────────────────────────────────────────────────────────────────────────
# Order is the contract: the gate's own arguments, then the SDK, then the caller's. The override of the deprecation
# error is among the gate's own (plan clang-tidy-opt-in-test-step, D10): it is inert until the compile commands carry
# -Werror=deprecated-declarations, and then it is what keeps a clang-tidy newer than the SDK's libc++ from stopping on
# nlohmann/json.
set(base "--warnings-as-errors=*;--extra-arg=-Wno-error=deprecated-declarations")
set(sysroot "--extra-arg=-isysroot;--extra-arg=/the/sdk")
cryptocogs_clang_tidy_args("" "" got)
expect_equal("args: none" "${got}" "${base}")
cryptocogs_clang_tidy_args("--first;--second=2" "" got)
expect_equal("args: extras" "${got}" "${base};--first;--second=2")
cryptocogs_clang_tidy_args("" "/the/sdk" got)
expect_equal("args: an SDK" "${got}" "${base};${sysroot}")
cryptocogs_clang_tidy_args("--first" "/the/sdk" got)
expect_equal("args: an SDK and extras" "${got}" "${base};${sysroot};--first")

# ── cryptocogs_clang_tidy_test_name ─────────────────────────────────────────────────────────────────────────────────
cryptocogs_clang_tidy_test_name("/r" "/r/src/kraken/auth.cpp" got)
expect_equal("test_name: a library source" "${got}" "clang_tidy_src_kraken_auth")
cryptocogs_clang_tidy_test_name("/r" "/r/tests/examples/binance/binance_rest_client_example.cpp" got)
expect_equal("test_name: a nested directory" "${got}"
    "clang_tidy_tests_examples_binance_binance_rest_client_example")
cryptocogs_clang_tidy_test_name("/r" "/r/tests/unit/test_signature.cpp" got)
expect_equal("test_name: a unit test" "${got}" "clang_tidy_tests_unit_test_signature")
cryptocogs_clang_tidy_test_name("/r/" "/r/src/./kraken/auth.cpp" got)
expect_equal("test_name: a trailing slash and a dot segment" "${got}" "clang_tidy_src_kraken_auth")
# A root spelled with dot segments is the directory it names.
cryptocogs_clang_tidy_test_name("/x/../r/." "/r/src/kraken/auth.cpp" got)
expect_equal("test_name: a root with a dot-dot and a dot segment" "${got}" "clang_tidy_src_kraken_auth")
expect_rejected("test_name: a source outside the root"
    CHILD -DCHILD=test_name -DROOT=/r -DSOURCE=/elsewhere/x.cpp
    WORDS "/elsewhere/x.cpp" "not under" "/r")
expect_rejected("test_name: a sibling directory that only shares the root's spelling"
    CHILD -DCHILD=test_name -DROOT=/r -DSOURCE=/r2/x.cpp
    WORDS "/r2/x.cpp" "not under")

# ── cryptocogs_clang_tidy_select ────────────────────────────────────────────────────────────────────────────────────
# One list with every case: C++ sources in the three spellings of the extension; one listed twice, and one listed again
# by a path that normalises to it; a header and a template body (not sources); a C file (the project has none, and the
# gate is for C++); a dependency's sources, under a directory called _deps, in the build tree and not; a directory whose name only
# contains _deps; the two fixture directories under tests/, whose defects are deliberate; a sibling directory that only shares
# the root's spelling; and a file elsewhere.
set(candidates
    "/r/tests/unit/test_y.cpp"
    "/r/src/kraken/types.cpp"
    "/r/src/kraken/auth.cpp"
    "/r/src/kraken/auth.cpp"
    "/r/src/./kraken/../kraken/types.cpp"
    "/r/tests/examples/binance/e.cc"
    "/r/tests/unit/test_x.cxx"
    "/r/include/exchange/common/types.hpp"
    "/r/src/exchange/common/ws_client.inl"
    "/r/src/legacy.c"
    "/r/build/debug/_deps/x-src/x.cpp"
    "/r/third_party/_deps/y.cpp"
    "/r/src/my_deps/a.cpp"
    "/r/tests/clang_tidy_checks/canaries/canary.cpp"
    "/r/tests/sanitize_checks/canaries/canary_asan.cpp"
    "/r2/src/elsewhere.cpp"
    "/elsewhere/x.cpp")
set(want "/r/src/kraken/auth.cpp;/r/src/kraken/types.cpp;/r/src/my_deps/a.cpp;/r/tests/examples/binance/e.cc;/r/tests/unit/test_x.cxx;/r/tests/unit/test_y.cpp")
cryptocogs_clang_tidy_select("/r" "${candidates}" got)
expect_equal("select: sources under the root, sorted, once each, less the fixtures" "${got}" "${want}")
cryptocogs_clang_tidy_select("/r/" "${candidates}" got)
expect_equal("select: the same under a root with a trailing slash" "${got}" "${want}")
cryptocogs_clang_tidy_select("/x/../r/." "${candidates}" got)
expect_equal("select: the same under a root with a dot-dot and a dot segment" "${got}" "${want}")

# Two sources that would be one test name: a_b/c.cpp and a/b_c.cpp both give clang_tidy_a_b_c.
expect_rejected("select: two sources with one test name"
    CHILD -DCHILD=select -DROOT=/r "-DCANDIDATES=/r/a_b/c.cpp|/r/a/b_c.cpp"
    WORDS "clang_tidy_a_b_c" "/r/a_b/c.cpp" "/r/a/b_c.cpp")

# A gate over no files passes on nothing: no candidate at all, and candidates of which none is a source of the project.
expect_rejected("select: no candidate at all"
    CHILD -DCHILD=select -DROOT=/r "-DCANDIDATES="
    WORDS "no .cpp" "/r")
expect_rejected("select: only a header, a fixture and a file elsewhere"
    CHILD -DCHILD=select -DROOT=/r
    "-DCANDIDATES=/r/include/a.hpp|/r/tests/clang_tidy_checks/canaries/canary.cpp|/elsewhere/x.cpp"
    WORDS "no .cpp" "/r")

file(REMOVE_RECURSE "${SCRATCH}")
if(NOT "${failures}" STREQUAL "")
    message(FATAL_ERROR "cmake/cryptocogs_clang_tidy.cmake is wrong:${failures}")
endif()
message(STATUS "cryptocogs_clang_tidy.cmake: the tool, the SDK, the arguments, the test names and the selection behave, "
               "and every refusal names what it refused")
