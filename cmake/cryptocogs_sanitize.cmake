# cryptocogs_sanitize.cmake — the CRYPTOCOGS_SANITIZE modes (plan sanitizer-option-and-presets, #30).
#
# One pure function maps a mode to the options that switch it on. It sets two variables and touches no global
# state, so tests/sanitize_checks/check_sanitize_flags.cmake (test_sanitize_flags) runs it under `cmake -P` in
# milliseconds, with no configure, and the root CMakeLists.txt calls the very same function to build the real
# thing.
#
#   OFF (or empty)   no sanitizer. The default.
#   asan             AddressSanitizer + UndefinedBehaviorSanitizer.
#   tsan             ThreadSanitizer.
#
# Modes, not free-form flags. ASan and TSan cannot share a binary, and an unrecognised value is a configure
# error: a mistyped -DCRYPTOCOGS_SANITIZE=adress that quietly meant "no sanitizer" would give a sanitizer tree
# that is green because it sanitised nothing. The match is exact and case-sensitive for the same reason. The
# guard in cmake/cryptocogs_platform.cmake compares a tree's name with these modes, which is the other reason
# for a closed set.
#
# -fno-omit-frame-pointer keeps the stacks in a report readable. The build type is left alone: the presets
# choose RelWithDebInfo (-O2 -g), which carries the line information a report needs.
#
# -fno-sanitize-recover=undefined (asan only). UBSan's default is to print a report and carry on, which leaves
# a green test run over a real finding. With the flag the first report aborts the process, so the test fails.
# ASan aborts on its own and TSan exits non-zero once it has reported, so neither needs it.
#
# The options go in front of every FetchContent dependency (see the root CMakeLists.txt), so third-party code is
# instrumented too. ThreadSanitizer needs that: it only sees synchronisation it can intercept or instrument, so
# a lock-free handshake inside uninstrumented code reads as a race on our side.

# cryptocogs_sanitizer_flags(<mode> <compile-out> <link-out>)
#
# Sets <compile-out> and <link-out> (in the caller's scope) to the lists of compile and link options for <mode>;
# both are empty for OFF. Any other value is a FATAL_ERROR that names it and lists the modes.
function(cryptocogs_sanitizer_flags mode compile_out link_out)
    if("${mode}" STREQUAL "" OR "${mode}" STREQUAL "OFF")
        set(compile "")
        set(link "")
    elseif("${mode}" STREQUAL "asan")
        set(compile -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
        set(link -fsanitize=address,undefined)
    elseif("${mode}" STREQUAL "tsan")
        set(compile -fsanitize=thread -fno-omit-frame-pointer)
        set(link -fsanitize=thread)
    else()
        message(FATAL_ERROR
            "CRYPTOCOGS_SANITIZE='${mode}' is not a sanitizer mode. Use one of: OFF, asan, tsan.")
    endif()
    set(${compile_out} "${compile}" PARENT_SCOPE)
    set(${link_out} "${link}" PARENT_SCOPE)
endfunction()
