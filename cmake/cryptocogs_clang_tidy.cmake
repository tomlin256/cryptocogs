# cryptocogs_clang_tidy.cmake — the helpers of the opt-in clang-tidy test step (plan 035, #34).
#
# The step runs clang-tidy over this repo's own translation units as ctest tests, one per file, and is off unless a configure
# asks for it:
#
#   cmake --preset debug-macos-arm64 -DCRYPTOCOGS_CLANG_TIDY=clang-tidy
#   ctest --preset debug-macos-arm64 -R '^clang_tidy_' -j8
#
# The default, empty, registers nothing and changes nothing. The functions here are the parts that need no project: they set
# one variable and touch nothing global, so tests/clang_tidy_checks/check_clang_tidy_module.cmake (test_clang_tidy_module) runs
# them under `cmake -P` in about a second, with no configure and no clang-tidy, and the root CMakeLists.txt calls the very
# same functions to build the real thing.
#
#   cryptocogs_clang_tidy_tool       the tool to run. Empty (the default) is off. Otherwise the path of a command this host can
#                                    run, found by name on PATH or given as a path; anything else is a configure error, and so
#                                    is a generator that writes no compile_commands.json, which the tests read.
#   cryptocogs_clang_tidy_sdk        the macOS SDK that the build compiles against, when the compile commands do not name one.
#   cryptocogs_clang_tidy_args       the arguments of every run.
#   cryptocogs_clang_tidy_test_name  clang_tidy_<the path under the root>, the name of one unit's test.
#   cryptocogs_clang_tidy_select     which of a list of files are units: the C++ sources under the root, less the fixtures.
#
# A lint gate that silently checks nothing passes, which is why every refusal is a FATAL_ERROR that names what it refused,
# and why these functions are tested.

# The one LLVM major, 23: Homebrew's llvm on the Mac, apt.llvm.org's clang-tidy-23 in the ubuntu-dev container. The test
# clang_tidy_version holds the tool to it, so that the Mac, the container and a CI job one day cannot drift apart unseen.
# Moving to another is one commit: this number, and whatever the new major reports.
set(CRYPTOCOGS_CLANG_TIDY_MAJOR 23)

# cryptocogs_clang_tidy_tool(<requested> <generator> <out>)
#
# Sets <out> to the clang-tidy to run: empty when <requested> is empty (the gate is off, whatever the generator), otherwise
# the path find_program gives for a command name or a path. A tool that was asked for and cannot be run is a FATAL_ERROR,
# never a gate that is silently absent, and so is a generator that writes no compile_commands.json: Xcode and the Visual
# Studio ones write none, and Ninja Multi-Config is refused with them because the file the tests read is the one a
# single-configuration tree writes.
function(cryptocogs_clang_tidy_tool requested generator out)
    set(tool "")
    if(NOT "${requested}" STREQUAL "")
        if(NOT "${generator}" MATCHES "Makefiles$|^Ninja$")
            message(FATAL_ERROR
                "CRYPTOCOGS_CLANG_TIDY=${requested} needs a generator that writes compile_commands.json "
                "(Makefiles or Ninja), not ${generator}")
        endif()
        find_program(found NAMES "${requested}" NO_CACHE)
        if(NOT found)
            message(FATAL_ERROR
                "CRYPTOCOGS_CLANG_TIDY=${requested} is not a command or a path this host can run. Give a clang-tidy on PATH, "
                "or the path to one, or leave it empty for no clang_tidy_ tests.")
        endif()
        set(tool "${found}")
    endif()
    set(${out} "${tool}" PARENT_SCOPE)
endfunction()

# cryptocogs_clang_tidy_sdk(<apple> <sysroot> <out>)
#
# Sets <out> to the SDK the build compiles against, or empty. It is only named for an Apple target whose sysroot is empty:
# /usr/bin/c++ finds the SDK itself, so the compile commands carry no -isysroot, and a clang-tidy that is not Apple's would
# take its own default. Homebrew's has MacOSX15.2.sdk compiled in, where the build uses whatever `xcrun --show-sdk-path`
# says, so without this the gate parses the headers of an SDK the build never uses.
function(cryptocogs_clang_tidy_sdk apple sysroot out)
    set(sdk "")
    if(apple AND "${sysroot}" STREQUAL "")
        execute_process(COMMAND xcrun --show-sdk-path
                        OUTPUT_VARIABLE sdk OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE status ERROR_QUIET)
        if(NOT "${status}" STREQUAL "0" OR "${sdk}" STREQUAL "")
            message(FATAL_ERROR
                "xcrun --show-sdk-path names no SDK, and clang-tidy needs one on this host. Set CMAKE_OSX_SYSROOT to an SDK "
                "to use that one.")
        endif()
    endif()
    set(${out} "${sdk}" PARENT_SCOPE)
endfunction()

# cryptocogs_clang_tidy_args(<extra> <sdk> <out>)
#
# Sets <out> to the arguments of every run, in this order: the gate's own, the SDK if <sdk> is not empty, and <extra>, the
# caller's list (CRYPTOCOGS_CLANG_TIDY_ARGS).
#
#   --warnings-as-errors=*   every finding is an error, so a unit is red or green. It is not in .clang-tidy, because clangd
#                            would then show every hint in the editor as an error.
#   -Wno-error=deprecated-declarations
#                            The project wants warnings as errors (plan 035, D10; the build's own are #47), and with a
#                            compile command that carries -Werror=deprecated-declarations a clang-tidy newer than the SDK's
#                            libc++ stops on nlohmann/json's CBOR declarations, which name std::char_traits<unsigned char>,
#                            a specialization libc++ deprecates: the error is in the SDK's headers, from a use in a
#                            dependency's, and Apple clang building the same headers is silent. Here a deprecation is a
#                            warning, --warnings-as-errors makes it an error where it is in this repo's own files, and the
#                            header filter drops it in the SDK's and the dependencies'. It is inert while the compile
#                            commands carry no -Werror, which is the case until the build gets its own.
function(cryptocogs_clang_tidy_args extra sdk out)
    set(args --warnings-as-errors=* --extra-arg=-Wno-error=deprecated-declarations)
    if(NOT "${sdk}" STREQUAL "")
        list(APPEND args --extra-arg=-isysroot "--extra-arg=${sdk}")
    endif()
    list(APPEND args ${extra})
    set(${out} "${args}" PARENT_SCOPE)
endfunction()

# cryptocogs_clang_tidy_test_name(<root> <source> <out>)
#
# Sets <out> to clang_tidy_ and the path of <source> under <root>, without its extension and with `/` as `_`:
# <root>/src/kraken/auth.cpp is clang_tidy_src_kraken_auth. The stem alone would not do: two directories hold a file of the
# same name. A source outside <root> is a FATAL_ERROR.
function(cryptocogs_clang_tidy_test_name root source out)
    set(base "${root}")
    cmake_path(NORMAL_PATH base)
    set(path "${source}")
    cmake_path(NORMAL_PATH path)
    cmake_path(IS_PREFIX base "${path}" under)
    if(NOT under)
        message(FATAL_ERROR "clang-tidy source ${source} is not under ${root}")
    endif()
    cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${base}")
    cmake_path(REMOVE_EXTENSION path LAST_ONLY)
    string(REPLACE "/" "_" name "${path}")
    set(${out} "clang_tidy_${name}" PARENT_SCOPE)
endfunction()

# cryptocogs_clang_tidy_select(<root> <candidates> <out>)
#
# Sets <out> to the units among <candidates>, a list of absolute paths: the .cpp, .cc and .cxx files under <root>, each
# once, sorted, less the two fixture directories under tests/ whose defects are deliberate (tests/clang_tidy_checks/canaries,
# which the canaries read, and tests/sanitize_checks/canaries, which only the sanitizer trees compile). Paths are normalised
# first, so a path with a dot segment is the file it names. Headers and template bodies are not units: clang-tidy reaches
# them through the sources that include them. This is a filter and reads no disk, so the caller says where the candidates
# come from: the targets of the project at configure time, and the compile database at test time.
#
# No unit at all is a FATAL_ERROR, since a gate over nothing passes on nothing, and so are two units that would be one test
# name: a_b/c.cpp and a/b_c.cpp are both clang_tidy_a_b_c, and a test dropped silently is a file nobody checks.
function(cryptocogs_clang_tidy_select root candidates out)
    set(base "${root}")
    cmake_path(NORMAL_PATH base)

    set(fixture_dirs "")
    foreach(directory IN ITEMS tests/clang_tidy_checks/canaries tests/sanitize_checks/canaries)
        set(fixture "${base}/${directory}")
        cmake_path(NORMAL_PATH fixture)
        list(APPEND fixture_dirs "${fixture}")
    endforeach()

    set(selected "")
    foreach(candidate IN LISTS candidates)
        set(path "${candidate}")
        cmake_path(NORMAL_PATH path)
        cmake_path(GET path EXTENSION LAST_ONLY extension)
        if(NOT extension MATCHES "^\\.(cpp|cc|cxx)$")
            continue()
        endif()
        cmake_path(IS_PREFIX base "${path}" under_root)
        if(NOT under_root)
            continue()
        endif()
        set(in_fixtures FALSE)
        foreach(fixture_dir IN LISTS fixture_dirs)
            cmake_path(IS_PREFIX fixture_dir "${path}" in_fixture)
            if(in_fixture)
                set(in_fixtures TRUE)
            endif()
        endforeach()
        if(NOT in_fixtures)
            list(APPEND selected "${path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES selected)
    list(SORT selected)

    list(LENGTH selected found)
    if(found EQUAL 0)
        list(LENGTH candidates given)
        message(FATAL_ERROR
            "found no .cpp, .cc or .cxx source under ${root} among ${given} candidate(s): a clang-tidy gate over no files "
            "passes on nothing")
    endif()
    foreach(source IN LISTS selected)
        cryptocogs_clang_tidy_test_name("${base}" "${source}" name)
        if(DEFINED owner_${name})
            message(FATAL_ERROR "${owner_${name}} and ${source} would both be the test ${name}")
        endif()
        set(owner_${name} "${source}")
    endforeach()
    set(${out} "${selected}" PARENT_SCOPE)
endfunction()
