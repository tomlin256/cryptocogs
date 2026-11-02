# check_platform.cmake — test_platform_name: the platform tag and the build-tree name check in
# cmake/cryptocogs_platform.cmake (plan 028).
#
# Run by ctest as `cmake -DSOURCE_DIR=<repo> -P check_platform.cmake`. It needs no configure and no
# build tree, so it finishes in milliseconds, and it holds a fixed table, so it does not depend on the
# host it runs on. The same table, less the cases this repo's guard does not have, is in flywheel-dag's
# and flywheel's copies of the module: change all three together.
#
# SOURCE_DIR is a parameter so that a hand-made change can be made to a scratch copy of the repo and
# must fail here.
#
# A name is a tree's own name (the last component of its path), as the guard passes it.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo> -P check_platform.cmake")
endif()
include("${SOURCE_DIR}/cmake/cryptocogs_platform.cmake")

set(failures "")

# The tag for a host's CMake system name and machine.
function(expect_tag system machine want)
    cryptocogs_platform(got "${system}" "${machine}")
    if(NOT got STREQUAL want)
        string(APPEND failures "\n  tag ${system}/${machine}: got '${got}', want '${want}'")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# A tree that must pass: no problem text.
function(expect_accepted name platform recorded)
    cryptocogs_tree_problem(problem "${name}" "${platform}" "${recorded}")
    if(NOT problem STREQUAL "")
        string(APPEND failures
            "\n  accepted '${name}' on ${platform} (recorded '${recorded}'): refused with: ${problem}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# A tree that must be refused, with text that names both platforms: the one the tree is for and the
# one this host is.
function(expect_refused_naming name platform recorded name_platform)
    cryptocogs_tree_problem(problem "${name}" "${platform}" "${recorded}")
    if(problem STREQUAL "")
        string(APPEND failures
            "\n  refused '${name}' on ${platform} (recorded '${recorded}'): accepted")
    else()
        foreach(want IN ITEMS "${name_platform}" "${platform}")
            string(FIND "${problem}" "${want}" at)
            if(at EQUAL -1)
                string(APPEND failures
                    "\n  refused '${name}' on ${platform}: the text does not name '${want}': ${problem}")
            endif()
        endforeach()
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# A tree that must be refused; the text is not examined.
function(expect_refused name platform recorded)
    cryptocogs_tree_problem(problem "${name}" "${platform}" "${recorded}")
    if(problem STREQUAL "")
        string(APPEND failures
            "\n  refused '${name}' on ${platform} (recorded '${recorded}'): accepted")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ── tag ───────────────────────────────────────────────────────────────────────
expect_tag(Darwin  arm64   macos-arm64)
expect_tag(Linux   aarch64 linux-arm64)
expect_tag(Linux   x86_64  linux-x86_64)
expect_tag(Linux   AMD64   linux-x86_64)
expect_tag(Darwin  x86_64  macos-x86_64)
expect_tag(Windows AMD64   windows-x86_64)

# ── accepted ──────────────────────────────────────────────────────────────────
expect_accepted(release-linux-arm64        linux-arm64 "")
expect_accepted(debug-linux-arm64-gcc13    linux-arm64 "")
expect_accepted(linux-arm64                linux-arm64 "")
expect_accepted(bench-macos-arm64          macos-arm64 "")
# Names that are not platform trees are not examined, on any host.
foreach(host IN ITEMS linux-arm64 macos-arm64)
    foreach(name IN ITEMS build scratch vscode cmake-build-debug release-linuxbrew)
        expect_accepted(${name} ${host} "")
    endforeach()
endforeach()
# A tree that remembers its own platform.
expect_accepted(scratch linux-arm64 linux-arm64)
expect_accepted(release-linux-arm64 linux-arm64 linux-arm64)

# ── refused, and the text names both platforms ────────────────────────────────
expect_refused_naming(release-linux-x86_64  linux-arm64 "" linux-x86_64)
expect_refused_naming(linux-x86_64          linux-arm64 "" linux-x86_64)
expect_refused_naming(debug-macos-arm64     linux-arm64 "" macos-arm64)
expect_refused_naming(release-linux-aarch64 linux-arm64 "" linux-aarch64)
expect_refused_naming(release-linux-arm64x  linux-arm64 "" linux-arm64x)
expect_refused_naming(Release-Linux-X86_64  linux-arm64 "" linux-x86_64)
# A tree that remembers another platform, whatever it is called.
expect_refused_naming(scratch               linux-arm64 macos-arm64 macos-arm64)

# ── refused ───────────────────────────────────────────────────────────────────
expect_refused(release-linux      linux-arm64 "")
expect_refused(release-linux-asan linux-arm64 "")
expect_refused(linux              linux-arm64 "")

if(failures)
    message(FATAL_ERROR "test_platform_name failed:${failures}")
endif()
message(STATUS "test_platform_name: ok")
