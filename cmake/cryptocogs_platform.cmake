# cryptocogs_platform.cmake — which platform a build tree is for, and the check that a tree is not
# named for another (plan 028).
#
# A CMake build tree belongs to one OS and one architecture: it records the absolute path of its
# source, the compiler, the target and the flags. One checkout is built on the Mac and in a Linux
# container that mounts it, so each platform and each kind of build gets a tree of its own, named
# <kind>-<os>-<arch> and kept under build/: build/release-macos-arm64, build/debug-linux-arm64. The
# kind is one lower-case word of letters and digits (release, debug, bench, ubsan, ...). The
# operating system and the architecture in the name are checked against the host by the guard in the
# root CMakeLists.txt, so a name cannot say a tree is for a platform it is not.
#
# Two pure functions, so that a test can feed them any host
# (tests/tree_checks/check_platform.cmake):
#
#   cryptocogs_platform(<out> <system-name> <machine>)
#       <out> is "<os>-<arch>": Darwin is macos, Linux is linux; the machine is lower-cased, aarch64 is
#       spelled arm64 and amd64 is spelled x86_64.
#
#   cryptocogs_tree_problem(<out> <name> <platform> <recorded>)
#       <name> is the tree's own name (the last component of its path), <platform> is this host's,
#       and <recorded> is the platform the tree remembers from its first configure, or empty. <out> is
#       empty, or the text of the first problem found.
#
# The same table is in flywheel-dag's cmake/flywheel_dag_platform.cmake and in flywheel's
# cmake/FlywheelPlatform.cmake: change all three together.

function(cryptocogs_platform out system_name machine)
    string(TOLOWER "${system_name}" os)
    if(os STREQUAL "darwin")
        set(os "macos")
    endif()
    string(TOLOWER "${machine}" arch)
    if(arch STREQUAL "aarch64")
        set(arch "arm64")
    elseif(arch STREQUAL "amd64")
        set(arch "x86_64")
    endif()
    set(${out} "${os}-${arch}" PARENT_SCOPE)
endfunction()

function(cryptocogs_tree_problem out name platform recorded)
    set(problem "")

    # The names are compared in lower case: the filesystem under a bind mount is case-insensitive.
    string(TOLOWER "${name}" lower)

    # A name has the shape of a platform tree when it is an optional kind, then macos or linux, then
    # a dash or the end of the name: release-linux-arm64, linux-x86_64, debug-macos. Anything else
    # (build, scratch, cmake-build-debug, release-linuxbrew) is not examined.
    if(lower MATCHES "^([a-z0-9]+-)?(macos|linux)(-|$)")
        # The platform it must be for: [<kind>-]<platform>, with anything after the architecture free.
        if(NOT lower MATCHES "^([a-z0-9]+-)?${platform}(-|$)")
            # The platform the name says: the OS and the word after it, if there is one.
            string(REGEX MATCH "^([a-z0-9]+-)?(macos|linux)(-([a-z0-9_]+))?" ignored "${lower}")
            set(kind "${CMAKE_MATCH_1}")
            set(named "${CMAKE_MATCH_2}")
            if(NOT "${CMAKE_MATCH_4}" STREQUAL "")
                string(APPEND named "-${CMAKE_MATCH_4}")
            endif()
            set(problem
                "cryptocogs: the build directory \"${name}\" is for ${named}, but this host is ${platform}. "
                "A build tree belongs to one OS and one architecture: use \"${kind}${platform}\" under build/ "
                "(cmake --list-presets shows the presets for this host).")
        endif()
    endif()

    # A tree remembers the platform of its first configure, whatever it is called.
    if(problem STREQUAL "" AND NOT "${recorded}" STREQUAL "" AND NOT "${recorded}" STREQUAL "${platform}")
        set(problem
            "cryptocogs: the build tree \"${name}\" was first configured on ${recorded}, but this host is "
            "${platform}. A build tree belongs to one OS and one architecture: use a tree named for this "
            "host, \"<kind>-${platform}\", under build/ (cmake --list-presets shows the presets for this host).")
    endif()

    string(REPLACE ";" "" problem "${problem}")
    set(${out} "${problem}" PARENT_SCOPE)
endfunction()
