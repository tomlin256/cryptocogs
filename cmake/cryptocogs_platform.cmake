# cryptocogs_platform.cmake — which platform a build tree is for, and the check that a tree is not
# named for another platform, nor for a sanitizer it is not configured for (plans 028 and 032).
#
# A CMake build tree belongs to one OS and one architecture: it records the absolute path of its
# source, the compiler, the target and the flags. One checkout is built on the Mac and in a Linux
# container that mounts it, so each platform and each kind of build gets a tree of its own, named
# <kind>-<os>-<arch> and kept under build/: build/release-macos-arm64, build/asan-linux-arm64. The
# kind is one lower-case word, a letter and then letters and digits: a CMake build type (release,
# debug), a sanitizer mode (asan, tsan), or whatever a hand-made tree wants (bench, ...). The
# operating system and the architecture in the name are checked against the host by the guard in the
# root CMakeLists.txt, so a name cannot say a tree is for a platform it is not, and a tree named for a
# sanitizer is configured for it.
#
# Two pure functions, so that a test can feed them any host
# (tests/tree_checks/check_platform.cmake):
#
#   cryptocogs_platform(<out> <system-name> <machine>)
#       <out> is "<os>-<arch>": the system name Darwin is macos, any other is lower-cased (Linux is
#       linux); the machine is lower-cased, aarch64 is spelled arm64 and amd64 is spelled x86_64.
#
#   cryptocogs_tree_problem(<out> <name> <platform> <recorded> <sanitize>)
#       <name> is the tree's own name (the last component of its path), <platform> is this host's,
#       <recorded> is the platform the tree remembers from its first configure, or empty, and
#       <sanitize> is CRYPTOCOGS_SANITIZE, or empty. <out> is empty, or the text of the first problem
#       found.
#
# The rules, in the order they are checked:
#
#   1. A name has the shape of a platform tree when it is an optional kind, then macos or linux, then
#      a dash or the end of the name (release-linux-arm64, linux-x86_64, debug-macos). It must then be
#      [<kind>-]<platform>, with anything after the architecture free (release-linux-arm64-gcc13).
#      Anything else (build, scratch, cmake-build-debug, release-linuxbrew) is not examined; a kind
#      starts with a letter, so 2025-macos-arm64 is among them. Names are compared in lower case: the
#      filesystem under a bind mount is case-insensitive.
#   2. A platform tree whose kind is asan or tsan must have CRYPTOCOGS_SANITIZE set to that mode: a
#      tree named tsan-... that is really uninstrumented is a green sanitizer run over nothing. Nothing
#      else is required of a sanitizer tree; release-linux-arm64 with CRYPTOCOGS_SANITIZE=asan stays
#      legal.
#   3. A tree remembers the platform of its first configure, whatever it is called, and refuses a
#      configure from another.
#
# The platform table (cryptocogs_platform) and the three rules are the same in flywheel-dag's
# cmake/flywheel_dag_platform.cmake: change both together. The error texts are each copy's own.

function(cryptocogs_platform out system_name machine)
    if(system_name STREQUAL "Darwin")
        set(os "macos")
    else()
        string(TOLOWER "${system_name}" os)
    endif()
    string(TOLOWER "${machine}" arch)
    if(arch STREQUAL "aarch64")
        set(arch "arm64")
    elseif(arch STREQUAL "amd64")
        set(arch "x86_64")
    endif()
    set(${out} "${os}-${arch}" PARENT_SCOPE)
endfunction()

function(cryptocogs_tree_problem out name platform recorded sanitize)
    set(problem "")

    # The names are compared in lower case: the filesystem under a bind mount is case-insensitive.
    string(TOLOWER "${name}" lower)

    # Rule 1. A name has the shape of a platform tree when it is an optional kind (a letter, then letters
    # and digits), then macos or linux, then a dash or the end of the name: release-linux-arm64,
    # linux-x86_64, debug-macos. Anything else (build, scratch, cmake-build-debug, release-linuxbrew,
    # 2025-macos-arm64) is not examined.
    if(lower MATCHES "^([a-z][a-z0-9]*-)?(macos|linux)(-|$)")
        # The platform it must be for: [<kind>-]<platform>, with anything after the architecture free.
        if(NOT lower MATCHES "^([a-z][a-z0-9]*-)?${platform}(-|$)")
            # The platform the name says: the OS and the word after it, if there is one.
            string(REGEX MATCH "^([a-z][a-z0-9]*-)?(macos|linux)(-([a-z0-9_]+))?" ignored "${lower}")
            set(kind "${CMAKE_MATCH_1}")
            set(named "${CMAKE_MATCH_2}")
            if(NOT "${CMAKE_MATCH_4}" STREQUAL "")
                string(APPEND named "-${CMAKE_MATCH_4}")
            endif()
            set(problem
                "cryptocogs: the build directory \"${name}\" is for ${named}, but this host is ${platform}. "
                "A build tree belongs to one OS and one architecture: use \"${kind}${platform}\" under build/ "
                "(cmake --list-presets shows the presets for this host).")
        elseif(lower MATCHES "^(asan|tsan)-")
            # Rule 2. The tree is for this platform and its kind is a sanitizer: it must be configured for that
            # sanitizer. The mode is compared exactly, as the option compares it.
            set(mode "${CMAKE_MATCH_1}")
            if(NOT "${sanitize}" STREQUAL "${mode}")
                if("${sanitize}" STREQUAL "")
                    set(set_to "OFF")
                else()
                    set(set_to "${sanitize}")
                endif()
                set(problem
                    "cryptocogs: the build directory \"${name}\" is named for the ${mode} sanitizer, but "
                    "CRYPTOCOGS_SANITIZE is ${set_to}. A sanitizer tree is configured for the mode in its name: "
                    "use -DCRYPTOCOGS_SANITIZE=${mode} (cmake --preset ${mode}-${platform}), or name the tree "
                    "for what it is.")
            endif()
        endif()
    endif()

    # Rule 3. A tree remembers the platform of its first configure, whatever it is called.
    if(problem STREQUAL "" AND NOT "${recorded}" STREQUAL "" AND NOT "${recorded}" STREQUAL "${platform}")
        set(problem
            "cryptocogs: the build tree \"${name}\" was first configured on ${recorded}, but this host is "
            "${platform}. A build tree belongs to one OS and one architecture: use a tree named for this "
            "host, \"<kind>-${platform}\", under build/ (cmake --list-presets shows the presets for this host).")
    endif()

    string(REPLACE ";" "" problem "${problem}")
    set(${out} "${problem}" PARENT_SCOPE)
endfunction()
