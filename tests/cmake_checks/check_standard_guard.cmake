# check_standard_guard.cmake — test_standard_guard: every public header refuses to compile below C++23, and
# says why (plan 029).
#
# Run by ctest as
#   cmake -DCXX=<compiler> -DINCLUDE_DIR=<repo>/include -DEXTRA_INCLUDE_DIRS=<dir>|<dir>... -DWORK_DIR=<dir>
#         -P check_standard_guard.cmake
# and registered for GCC and Clang, whose -std= and -E it passes. The whole suite builds as C++23, so nothing
# else would notice a guard that stopped working; this holds the guards themselves to account.
#
# Each header gets a translation unit of its own that includes nothing else, and is preprocessed at C++17 and at
# C++20. The guard is an #error, a preprocessor directive, so -E proves it and the test does not parse
# nlohmann/json 64 times (the first version compiled each unit and took 19 s on the Mac). Both must fail, and the
# failure must be the guard's: a header that fails for another reason, such as a dependency it cannot find, has
# not shown that it refuses. EXTRA_INCLUDE_DIRS (the headers' own dependencies, separated by "|") are on the
# include path so that the guard is the only thing that can fail. The glob is the list of headers, so a header
# added later is covered: it fails here until it includes exchange/common/cxx_standard.hpp, directly or through
# another header. The .inl files are only ever included by their .hpp. Finding no header is an error, so a wrong
# path cannot pass by checking nothing.

cmake_minimum_required(VERSION 4.0)

foreach(var IN ITEMS CXX INCLUDE_DIR WORK_DIR)
    if(NOT ${var})
        message(FATAL_ERROR "usage: cmake -DCXX=<compiler> -DINCLUDE_DIR=<dir> "
                            "[-DEXTRA_INCLUDE_DIRS=<dir>|<dir>] -DWORK_DIR=<dir> -P check_standard_guard.cmake")
    endif()
endforeach()

set(include_flags "-I${INCLUDE_DIR}")
string(REPLACE "|" ";" extra_dirs "${EXTRA_INCLUDE_DIRS}")
foreach(dir IN LISTS extra_dirs)
    if(NOT dir STREQUAL "")
        list(APPEND include_flags "-I${dir}")
    endif()
endforeach()

file(GLOB headers RELATIVE "${INCLUDE_DIR}" "${INCLUDE_DIR}/exchange/*/*.hpp")
list(SORT headers)
list(LENGTH headers count)
if(count EQUAL 0)
    message(FATAL_ERROR "found no header under ${INCLUDE_DIR}/exchange")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(problems "")
foreach(header IN LISTS headers)
    # exchange/kraken/types.hpp and exchange/binance/types.hpp share a file name, so the unit's is the whole path
    string(REPLACE "/" "_" stem "${header}")
    set(unit "${WORK_DIR}/${stem}.cpp")
    file(WRITE "${unit}" "#include <${header}>\n")
    foreach(std IN ITEMS c++17 c++20)
        execute_process(
            COMMAND "${CXX}" -std=${std} -E ${include_flags} "${unit}"
            RESULT_VARIABLE result
            ERROR_VARIABLE  errors
            OUTPUT_QUIET)
        if(result EQUAL 0)
            string(APPEND problems "  ${header} was accepted at -std=${std}\n")
        elseif(NOT errors MATCHES "requires C\\+\\+23")
            string(SUBSTRING "${errors}" 0 300 shown)
            string(APPEND problems
                "  ${header} failed at -std=${std}, but not with the guard's message:\n${shown}\n")
        endif()
    endforeach()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR "a header does not refuse to compile below C++23:\n${problems}")
endif()
message(STATUS "all ${count} headers refuse -std=c++17 and -std=c++20")
