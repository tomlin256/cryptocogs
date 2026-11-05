# cryptocogs_warnings.cmake — the warning flags of the project's own targets (plan 036, #47).
#
# One pure function maps a compiler, whether this is the top-level project, and the CRYPTOCOGS_WARNINGS_AS_ERRORS option to the
# flags. It sets one variable and touches no global state, so tests/warning_checks/check_warning_flags.cmake (test_warning_flags)
# runs it under `cmake -P` in milliseconds, with no configure, and the root CMakeLists.txt calls the very same function to build
# the real thing.
#
#   -Wall -Wextra -Wpedantic          what flywheel-dag has on its own targets. This build had no warning flag at all.
#   -Werror=deprecated-declarations   always, with the option or without it: a use of a [[deprecated]] API is an error.
#   -Werror                           last, when the option is on: every other warning is an error too.
#
# GNU, Clang and AppleClang only. No job builds with another compiler, so flags for one would be untested. The match is exact
# and case-sensitive, so an id that merely contains "Clang" (ARMClang, say) gets nothing until it is added here on purpose.
#
# Not below the top level. A project that fetches cryptocogs keeps its own flags, so a subproject gets the empty list (and the
# root does not even declare the option there). The flags are never put on a target's INTERFACE or in the cache, for the same
# reason; test_warning_option holds that.
#
# What the function cannot say is where the list is applied, which is the other half of the design. add_compile_options reaches
# only the targets created after it in that directory and below, and this project fetches its dependencies in two files, so a
# call in the root either reaches the dependencies or misses src/ (plan 036 measured both). Each directory of the project's own
# targets therefore applies the list at its top, after any dependency it fetches: src/, tests/ (after its last fetch) and
# tests/benchmarks/. A directory added later does the same, and test_warning_option fails if it does not.

# cryptocogs_warning_flags(<compiler-id> <top-level> <werror> <out>)
#
# Sets <out> (in the caller's scope) to the list of compile options. <top-level> and <werror> are true or false by the rules of
# if(): ON, TRUE, 1 and YES are true; OFF, 0, FALSE, NO, NOTFOUND and the empty string are false. It refuses nothing: a compiler
# that gets no flags is a normal case, not an error.
function(cryptocogs_warning_flags compiler_id top_level werror out)
    set(flags "")
    if(top_level AND "${compiler_id}" MATCHES "^(GNU|Clang|AppleClang)$")
        set(flags -Wall -Wextra -Wpedantic -Werror=deprecated-declarations)
        if(werror)
            list(APPEND flags -Werror)
        endif()
    endif()
    set(${out} "${flags}" PARENT_SCOPE)
endfunction()
