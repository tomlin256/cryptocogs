# cryptocogs_clang_tidy.cmake — the helpers of the opt-in clang-tidy test step (plan clang-tidy-opt-in-test-step, #34).
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
# And the one that needs a project, cryptocogs_clang_tidy_add_tests, which reads the units from the targets and registers the
# tests (test_clang_tidy_wiring covers it, with a stub project):
#
#   cryptocogs_clang_tidy_add_tests  the clang_tidy_ tests: one per unit, and the six that prove the gate bites.
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
#                            The project wants warnings as errors (plan clang-tidy-opt-in-test-step, D10; the build's own are
#                            #47), and with a compile command that carries -Werror=deprecated-declarations a clang-tidy newer than
#                            the SDK's libc++ stops on nlohmann/json's CBOR declarations, which name std::char_traits<unsigned
#                            char>, a specialization libc++ deprecates: the error is in the SDK's headers, from a use in a
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
# once, sorted, less a dependency's sources, which sit under a directory called _deps (wherever its build tree is: the default
# of a fetched dependency, and a source directory that was named into another tree's _deps), and less the two fixture
# directories under tests/ whose defects are deliberate (tests/clang_tidy_checks/canaries, which the canaries read, and
# tests/sanitize_checks/canaries, which only the sanitizer trees compile). Paths are normalised first, so a path with a dot
# segment is the file it names. Headers and template bodies are not units: clang-tidy reaches them through the sources that
# include them. This is a filter and reads no disk, so the caller says where the candidates come from: the targets of the
# project at configure time, and the compile database at test time, each less what is generated into the build tree.
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
        # A dependency's source: a directory called _deps on the way from the root, which is a name and not a substring.
        set(relative "${path}")
        cmake_path(RELATIVE_PATH relative BASE_DIRECTORY "${base}")
        string(FIND "/${relative}/" "/_deps/" at_deps)
        if(NOT at_deps EQUAL -1)
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

# _cryptocogs_clang_tidy_walk(<directory> <out>)
#
# Sets <out> to the absolute paths of the sources of the targets made in <directory> and in every subdirectory below it, the
# dependencies' included: the executables and the libraries (static, shared, module and object), not the interface ones and
# not the custom ones. Which of them are this project's is cryptocogs_clang_tidy_select's to say, by where they are; the walk
# only drops a source that is generated into the build tree, which the project does not own. A source is made absolute against
# the directory of the target that names it. One that holds a generator expression cannot be read here and is left out;
# clang_tidy_units is what notices a unit the walk missed. This needs a project (the targets exist only in a configure), so it
# is the one part of the module that `cmake -P` cannot run, and test_clang_tidy_wiring covers it.
function(_cryptocogs_clang_tidy_walk directory out)
    set(build "${CMAKE_BINARY_DIR}")
    cmake_path(NORMAL_PATH build)

    set(sources "")
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(NOT type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            continue()
        endif()
        get_target_property(target_sources ${target} SOURCES)
        get_target_property(target_directory ${target} SOURCE_DIR)
        if(NOT target_sources)
            continue()
        endif()
        foreach(source IN LISTS target_sources)
            if(source MATCHES "\\$<")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${source}")
                set(source "${target_directory}/${source}")
            endif()
            cmake_path(NORMAL_PATH source)
            cmake_path(IS_PREFIX build "${source}" generated)
            if(NOT generated)
                list(APPEND sources "${source}")
            endif()
        endforeach()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _cryptocogs_clang_tidy_walk("${subdirectory}" below)
        list(APPEND sources ${below})
    endforeach()
    set(${out} "${sources}" PARENT_SCOPE)
endfunction()

# cryptocogs_clang_tidy_add_tests(<tool> <root> <extra>)
#
# Registers the tests below in the calling directory, for the tool cryptocogs_clang_tidy_tool resolved. An empty <tool>
# registers nothing and copies nothing: that is the opt-in rule. <root> is the source root, where .clang-tidy and
# tests/clang_tidy_checks/ are; <extra> is the caller's list of extra arguments. The compile database is the one in
# PROJECT_BINARY_DIR. Nothing is built: clang-tidy reads that database and the sources, and the dependencies' headers are in
# place by the end of the configure. The call comes after the last target of the project is made (the end of the root
# CMakeLists.txt), because the units are read from the targets.
#
#   clang_tidy_<path>     one per unit: a .cpp that a target of this project compiles, named from its path under the root, so
#                         that `ctest -R '^clang_tidy_src_binance_'` runs one adapter's. A unit that no target compiles is not
#                         one, and an adapter that is switched off has no targets. D9 of plan clang-tidy-opt-in-test-step has the
#                         reasons.
#   clang_tidy_version    the tool is LLVM CRYPTOCOGS_CLANG_TIDY_MAJOR. The Mac, the container and a CI job one day run one
#                         major, and this is what stops them drifting apart unseen.
#   clang_tidy_checks     .clang-tidy enables the families it should, and none of the checks it removes.
#   clang_tidy_units      the registered units are exactly the units in the tree's compile database, at test time.
#   clang_tidy_canary_*   three proofs that the gate bites, on fixtures that are in no compile database: a defect in the unit,
#                         a defect in a header at this repo's kind of path, and the same defect in a header at a dependency's
#                         kind of path, which must not be reported.
#
# Every run names the config file, so that the file clang_tidy_checks lists, the file the canaries read and the file the units
# read are one file, whatever a subdirectory might hold later. A lint gate that silently checks nothing passes, which is why
# the gate itself is tested.
function(cryptocogs_clang_tidy_add_tests tool root extra)
    if("${tool}" STREQUAL "")
        return()
    endif()

    set(config "--config-file=${root}/.clang-tidy")
    cryptocogs_clang_tidy_sdk("${APPLE}" "${CMAKE_OSX_SYSROOT}" sdk)
    cryptocogs_clang_tidy_args("${extra}" "${sdk}" args)
    _cryptocogs_clang_tidy_walk("${root}" candidates)
    cryptocogs_clang_tidy_select("${root}" "${candidates}" units)

    foreach(unit IN LISTS units)
        cryptocogs_clang_tidy_test_name("${root}" "${unit}" name)
        add_test(NAME ${name}
                 COMMAND ${tool} -p ${PROJECT_BINARY_DIR} --quiet ${config} ${args} ${unit})
    endforeach()

    add_test(NAME clang_tidy_version COMMAND ${tool} --version)
    set_tests_properties(clang_tidy_version PROPERTIES
        PASS_REGULAR_EXPRESSION "LLVM version ${CRYPTOCOGS_CLANG_TIDY_MAJOR}\\.")

    # The list prints sorted, so one expression asks for a check of each family, in order.
    add_test(NAME clang_tidy_checks COMMAND ${tool} ${config} --list-checks)
    set_tests_properties(clang_tidy_checks PROPERTIES
        PASS_REGULAR_EXPRESSION
            "bugprone-use-after-move.*cppcoreguidelines-pro-type-member-init.*performance-unnecessary-value-param"
        FAIL_REGULAR_EXPRESSION
            "bugprone-easily-swappable-parameters|bugprone-narrowing-conversions|readability-|modernize-")

    # The units proof reads the database at test time, so it is handed the list as the registration made it.
    string(REPLACE ";" "|" registered "${units}")
    add_test(NAME clang_tidy_units
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${root}
                     -DBUILD_DIR=${PROJECT_BINARY_DIR}
                     "-DREGISTERED=${registered}"
                     -P ${root}/tests/clang_tidy_checks/check_clang_tidy_units.cmake)

    # The canary: one unit with a defect of its own, and two headers that carry the same defect at the two kinds of path the
    # header filter must tell apart. The fixtures are copied under the build tree so that their paths are
    # .../include/exchange/... and .../_deps/canary-src/tests/..., not tests/..., which the filter would admit whatever else it
    # did. The dependency's path matches the filter's inclusion (/tests/), so only its exclusion keeps it out. All three tests
    # run one command: the output has to show the unit's error, the header's error, and no word of the dependency header.
    # "error:", not "warning:", is --warnings-as-errors biting; a check's name is followed by a comma or the bracket, because
    # the flag appends ",-warnings-as-errors".
    set(fixtures ${root}/tests/clang_tidy_checks/canaries)
    set(canary ${PROJECT_BINARY_DIR}/clang_tidy_canary)
    configure_file(${fixtures}/canary.cpp ${canary}/canary.cpp COPYONLY)
    configure_file(${fixtures}/canary_own.hpp ${canary}/include/exchange/canary_own.hpp COPYONLY)
    configure_file(${fixtures}/canary_dep.hpp ${canary}/_deps/canary-src/tests/canary_dep.hpp COPYONLY)
    set(command
        ${tool} ${config} ${args} ${canary}/canary.cpp -- -std=c++23
        -I${canary}/include -I${canary}/_deps/canary-src/tests)
    set(unit_error "canary\\.cpp:[0-9]+:[0-9]+: error: [^\n]*\\[bugprone-use-after-move[],]")
    add_test(NAME clang_tidy_canary_source COMMAND ${command})
    set_tests_properties(clang_tidy_canary_source PROPERTIES
        PASS_REGULAR_EXPRESSION "${unit_error}")
    add_test(NAME clang_tidy_canary_own_header COMMAND ${command})
    set_tests_properties(clang_tidy_canary_own_header PROPERTIES
        PASS_REGULAR_EXPRESSION
            "include/exchange/canary_own\\.hpp:[0-9]+:[0-9]+: error: [^\n]*\\[performance-unnecessary-value-param[],]")
    add_test(NAME clang_tidy_canary_dependency_header COMMAND ${command})
    set_tests_properties(clang_tidy_canary_dependency_header PROPERTIES
        PASS_REGULAR_EXPRESSION "${unit_error}"
        FAIL_REGULAR_EXPRESSION "canary_dep\\.hpp")
endfunction()
