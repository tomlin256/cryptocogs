# check_standard_requirement.cmake — test_standard_requirement: linking any library built in src/ compiles the
# consumer as the C++ standard the project requires (plan 029).
#
# Run by ctest as
#   cmake -DTARGET_FEATURES=<name>=<feature>+<feature>|<name>=... -DWANT=cxx_std_23
#         -P check_standard_requirement.cmake
# TARGET_FEATURES is made at generate time (tests/CMakeLists.txt) from each library target's
# INTERFACE_COMPILE_FEATURES, which is what the install export hands to a find_package consumer: the
# top-level CMAKE_CXX_STANDARD is directory-scoped, compiles the project itself and reaches no consumer, so
# a library that loses its target_compile_features still builds and passes every other test while the
# export quietly drops the requirement (plan 017 fixed that once, by hand).
#
# The generator expression is transitive: it adds the features of every library a target links, such as
# nlohmann_json's cxx_std_11, so a target's list is longer than its own. CMake compiles a consumer at the
# highest standard it is given, so the check reads the highest and holds it to WANT: lower than WANT is the
# failure this guards against, and higher is a standard nobody has chosen yet. The targets are those that
# src/ creates (the directory's BUILDSYSTEM_TARGETS), so a library added later is held to it too. One that
# links no other cryptocogs library and forgets its target_compile_features fails here, by name; one that
# links exchange_common inherits its requirement, and a consumer of it is compiled correctly either way.
#
# It fails if it is given no target, so a wrong registration cannot pass by checking nothing.

cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS TARGET_FEATURES WANT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "usage: cmake -DTARGET_FEATURES=<name>=<feature>+<feature>|<name>=... "
                            "-DWANT=<cxx_std_NN> -P check_standard_requirement.cmake")
    endif()
endforeach()

string(REPLACE "|" ";" entries "${TARGET_FEATURES}")

set(problems "")
set(checked 0)
foreach(entry IN LISTS entries)
    if(entry STREQUAL "")
        continue()
    endif()
    if(NOT entry MATCHES "^([^=]+)=(.*)$")
        message(FATAL_ERROR "cannot read the entry '${entry}' of TARGET_FEATURES")
    endif()
    set(name "${CMAKE_MATCH_1}")
    string(REPLACE "+" ";" features "${CMAKE_MATCH_2}")
    math(EXPR checked "${checked} + 1")

    set(highest "")
    foreach(feature IN LISTS features)
        if(feature MATCHES "^cxx_std_([0-9]+)$")
            set(standard "${CMAKE_MATCH_1}")
            if(highest STREQUAL "" OR standard GREATER highest)
                set(highest "${standard}")
            endif()
        endif()
    endforeach()

    if(highest STREQUAL "")
        string(APPEND problems "  ${name} asks for no C++ standard, not ${WANT}\n")
    elseif(NOT "cxx_std_${highest}" STREQUAL WANT)
        string(APPEND problems "  ${name} asks for cxx_std_${highest}, not ${WANT}\n")
    endif()
endforeach()

if(checked EQUAL 0)
    message(FATAL_ERROR "found no library target to check")
endif()
if(NOT problems STREQUAL "")
    message(FATAL_ERROR "a library does not compile its consumers as ${WANT}:\n${problems}")
endif()
message(STATUS "all ${checked} libraries compile their consumers as ${WANT}")
