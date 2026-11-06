# Development recipes

Procedures that `CLAUDE.md` points to. Not loaded automatically: read one when you need it.

The Linux half of the work runs in the `ubuntu-dev` container. `~/src` is bind-mounted at `/work`, so the checkout is `/work/cryptocogs`, and the container user is the Mac user, so nothing it writes is owned by root. To run a script in the container, pipe it to the shell there: `docker exec -i ubuntu-dev bash -s < script.sh`. Do not `docker cp` into it: the file lands owned by root in the sticky `/tmp`, and only `docker exec -u root` removes it.

## CI's compiler (GCC 15.2) locally, in a throwaway container

Apple clang and GCC 13 accept code that GCC 15 rejects, and the workflows that build do so with `-Werror` (the default). Build every configuration the workflows build, not Release alone: `release-`, `asan-` and `tsan-linux-…` (a Release tree missed a failure that only the tsan job showed, plan 036).

```bash
docker run -d -v ~/src:/work:ro ubuntu:26.04 sleep infinity      # a container of its own, with the sources read-only
# in it, from apt: g++ cmake make git libssl-dev libcurl4-openssl-dev zlib1g-dev
cmake -S /work/cryptocogs -B /tmp/x/<preset name> -DCMAKE_BUILD_TYPE=<as the preset> -DCRYPTOCOGS_SANITIZE=<as the preset> \
      -DFETCHCONTENT_SOURCE_DIR_<DEP>=/work/cryptocogs/build/debug-linux-arm64/_deps/<dep>-src       # each of the seven dependencies
cmake --build /tmp/x/<preset name> -j4 -- -k                                                         # -k lists every failure
```

- Docker Desktop's VM has 7.75 GiB shared by every container. At `-j12`, next to another container's build, three `cc1plus` were killed (`c++: fatal error: Killed signal terminated program cc1plus` is not a finding), so build the three trees at `-j4`.
- On an Apple-silicon host the container compiles the tsan tree but its tests die with `SIGILL` at the first thread (aarch64 libtsan on Docker Desktop's kernel); only CI runs them.

## What the compilers have disagreed about

- Apple's libc++ marks `std::future::get` `[[nodiscard]]` and libstdc++ does not: a discarded `fut.get()` warns on the Mac only.
- GCC 13 (`-Wmissing-field-initializers`) and clang 23 (`-Wmissing-designated-field-initializers`) warn about a designated-initialiser list that leaves members out; Apple clang 21 does not.
- GCC 15 at `-O2` under ThreadSanitizer reports the destructor of a local whose `std::optional<std::string>` is never engaged as `-Wmaybe-uninitialized` (a false positive; Release, ASan and GCC 13 do not). The four tests in `test_ws_client.cpp` that hit it move the request into `subscribe_async` instead of copying it.

## Proving a leak fix on Linux

macOS has no LeakSanitizer, so the proof that a test catches a leak is a Linux run of the old code. It needs the dependency sources of a configured `debug-linux-arm64` tree, so that the scratch tree configures offline.

1. Put a copy of `HEAD` in the container's `/tmp`, and the old file over its new one, through stdin:

   ```bash
   docker exec ubuntu-dev mkdir -p /tmp/leakproof
   git -C ~/src/cryptocogs archive HEAD | docker exec -i ubuntu-dev tar -x -C /tmp/leakproof
   git -C ~/src/cryptocogs show <old-rev>:<path/to/file> | docker exec -i ubuntu-dev tee /tmp/leakproof/<path/to/file> > /dev/null
   ```

2. Configure the `asan-linux-arm64` preset offline, build only the test's target and run it, as one script (`docker exec -i ubuntu-dev bash -s < leakproof.sh`):

   ```bash
   set -e
   cd /tmp/leakproof
   deps=/work/cryptocogs/build/debug-linux-arm64/_deps
   cmake --preset asan-linux-arm64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
         -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$deps/nlohmann_json-src \
         -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=$deps/tomlplusplus-src \
         -DFETCHCONTENT_SOURCE_DIR_IXWEBSOCKET=$deps/ixwebsocket-src \
         -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$deps/googletest-src \
         -DFETCHCONTENT_SOURCE_DIR_BACKWARD=$deps/backward-src \
         -DFETCHCONTENT_SOURCE_DIR_SPDLOG=$deps/spdlog-src \
         -DFETCHCONTENT_SOURCE_DIR_CLI11=$deps/cli11-src
   cmake --build --preset asan-linux-arm64 --target <the test's target> -j4
   ASAN_OPTIONS=detect_leaks=1 build/asan-linux-arm64/bin/<the test's target>
   ```

3. The old code is reported with its allocation site and the test is red. Put the fixed file back the same way (`git show HEAD:<path> | docker exec -i ubuntu-dev tee …`) and run the script again: it is clean.
4. Remove the copy when done: `docker exec ubuntu-dev rm -rf /tmp/leakproof`.

## Before the release notes

`CLAUDE.md` "Releasing" says what a release is; this is what to check before the notes are written.

1. `git log vPREV..HEAD` lists every commit since the last tag, not only the planned fix, and `gh issue view <n>` on each fix since the tag: the previous notes can list one as still open.
2. Measure `sizeof` of the public classes at `vPREV` (`git archive vPREV include` into a scratch directory) and at `HEAD` with a ten-line program. A layout change is what makes a major release, and a class that grew in a minor release is said in the notes (`CurlHttpClient` went from 40 to 56 bytes in 2.1.0).
3. Tag the bump commit with `git tag -a vX.Y.Z <bump sha> -F file`. No line of the file starts with `#`, because git strips those. `gh api repos/<owner>/cryptocogs/git/ref/tags/vX.Y.Z` shows the tag points at the bump commit.
4. A later docs commit marks the plans the release carries as released.
