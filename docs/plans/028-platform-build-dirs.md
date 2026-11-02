# Plan 028 — One build tree per platform

**Status:** Approved (2026-11-02) — Rob approved the plan as written, so D1 to D8 stand at the defaults below.
**Design source:** flywheel-dag's [platform_build_dirs_plan.md](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/platform_build_dirs_plan.md)
([flywheel-dag#46](https://github.com/tomlin256/flywheel-dag/issues/46)). This plan applies that design here and restates
only what differs. The design source names the tree `build/<kind>-<os>-<arch>` too (D1): it had `build-<os>-<arch>`
before Rob's refinement of 2026-11-02 and was brought in line the same day, and it is released as flywheel-dag v0.3.0,
whose Outcome records what its steps found.
**Closes:** [GitHub issue #25](https://github.com/tomlin256/cryptocogs/issues/25) — "One build tree per platform:
build/&lt;kind&gt;-&lt;os&gt;-&lt;arch&gt;, presets, a guard, and a Linux proof"

## Problem

One checkout is built on more than one platform on the same host: natively on macOS (arm64), and on Linux in the
`ubuntu-dev` container, which bind-mounts `~/src` at `/work`, either as arm64 (native speed) or as x86_64 (emulated,
and CI's architecture). A CMake build tree belongs to one platform: it records the absolute path of its source
(`/Users/rob/src/cryptocogs`, `/work/cryptocogs`), the compiler, the target and the flags. Everything in this repo
names the one tree `build/`, so the second platform has nowhere to build. The container sees the Mac's five trees
(`build`, `build-bench`, `build-ubsan`, and the stale `build-compat` and `build2`) and can use none of them: CMake
stops with "The current CMakeCache.txt directory … is different than the directory … where CMakeCache.txt was
created", the error flywheel-dag#46 records.

The fix is a tree of its own for each platform and each kind of build, named `<kind>-<os>-<arch>` and kept under
`build/`: `build/release-macos-arm64`, `build/debug-macos-arm64`, `build/release-linux-arm64`,
`build/release-linux-x86_64`. The kind says what the tree is for (`release` and `debug` have presets here; `bench` and
`ubsan` are made by hand), and the operating system and architecture are checked against the host. `build/` stops
being a tree and becomes the directory that holds them.

Whatever only Linux shows therefore waits for a push and a runner. The last defect fixed here,
[#23](https://github.com/tomlin256/cryptocogs/issues/23) (`base64_decode` left-shifting a signed `int` past its
range), was found with an UndefinedBehaviorSanitizer tree made by hand on the Mac. LeakSanitizer does not exist on
macOS (Apple's runtime on arm64). libstdc++ and GCC behave differently from libc++ and Apple Clang. CI runs on x86_64
Linux alone. Linux/aarch64 is also a third ABI: plain `char` is unsigned there, and signed on x86_64 and on Apple's
arm64, which is the kind of difference the byte-level code in this library (signing, base64, hex) should meet before
a user does.

**A prerequisite outside this repo.** The `ubuntu-dev` image has no libcurl, OpenSSL or zlib development packages
(checked 2026-11-01: `curl.h` and `openssl/ssl.h` are absent), and the root `CMakeLists.txt` has
`find_package(CURL REQUIRED)` and `find_package(OpenSSL REQUIRED)`; the ixwebsocket it fetches for the tests needs
zlib. `libcurl4-openssl-dev` only suggests the other two on Ubuntu 24.04, so all three are named. Nothing builds here
in the container until they are installed (D8).

## What names the tree

Found with `git grep` on 2026-11-01, outside `docs/plans/`:

| File | What |
|---|---|
| `.clangd` | `CompilationDatabase: build` |
| `.github/workflows/cmake-single-platform.yml` | configure, build and the Test step's `working-directory` |
| `README.md` | 32 lines: 27 `./build/bin/…` usage examples, the quickstart, a `-L…/build/src` link line, `cd build && ctest` |
| `CLAUDE.md` | 66 lines: the build commands, the outputs table and the two test tables |
| `docs/agent-add-exchange.md` | 4 lines (`cmake --build build`, `cd build && ctest`, and two success checks) |

Nothing in `CMakeLists.txt`, `src/` or `tests/` names `build`: they use `CMAKE_BINARY_DIR`, and
`CMAKE_RUNTIME_OUTPUT_DIRECTORY` is `${CMAKE_BINARY_DIR}/bin`. `.gitignore` already ignores `build*`. The older plans
keep `build/`; they record what was run. A consumer that fetches or installs this library builds in its own tree and
is not affected. `CLAUDE.md` says 487 tests; `ctest -N` in the Mac's `build/` says 496 today.

`build/` is today the primary tree itself, with its `CMakeCache.txt` directly in it. It stays as it is: the new trees
nest inside it, share no file with it, and it keeps working until it is deleted (see Not in this plan).

## The change

Four parts, as in the design source.

1. **A platform tag**, `cmake/cryptocogs_platform.cmake` (new): `<os>-<arch>` from CMake's host system name and
   machine. `Darwin` is `macos`, `Linux` is `linux`; the machine is lower-cased, `aarch64` is spelled `arm64` and
   `amd64` is spelled `x86_64`. Two pure functions, so a test can feed them any host:
   `cryptocogs_platform(<out> <system-name> <machine>)` and
   `cryptocogs_tree_problem(<out> <binary-dir-name> <platform> <recorded>)`. They are flywheel-dag's two, with the
   prefix changed, the kind added to the name's shape, and the error text carrying this project's name. They use only
   commands this repo's CMake floor (3.15) has: `get_filename_component`, `string(REGEX …)`, `string(TOLOWER …)`; not
   `cmake_path`.

2. **A guard**, in the root `CMakeLists.txt` between `cmake_minimum_required()` and `project()`, for the top-level
   project only (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`, the test this file already uses for
   `CRYPTOCOGS_INSTALL`). Two halves, either of which stops the configure with an error that names the tree to use:
   - A directory whose name has the shape of a platform tree, an optional kind and then `macos` or `linux`
     (`release-linux-arm64`, `linux-x86_64`, `debug-macos`), must be `[<kind>-]<os>-<arch>` for this host, with
     anything after the architecture free (`release-linux-arm64-gcc13`). Names are compared in lower case, since the
     bind mount is case-insensitive. Other names (`build`, `scratch`, `cmake-build-debug`, `release-linuxbrew`) are
     not examined.
   - Every tree remembers the platform of its first configure in an internal cache entry,
     `CRYPTOCOGS_TREE_PLATFORM`, and refuses a configure from another. That covers a free-form name.

   ```text
   CMake Error at CMakeLists.txt:NN (message):
     cryptocogs: the build directory "release-linux-x86_64" is for linux-x86_64, but this host is
     linux-arm64. A build tree belongs to one OS and one architecture: use "release-linux-arm64"
     under build/ (cmake --list-presets shows the presets for this host).
   ```

3. **Presets**, `CMakePresets.json` (new), the one file that spells out the trees. Version 3, and:
   - hidden `base`, whose `binaryDir` is `${sourceDir}/build/${presetName}`;
   - three hidden host presets, `macos-arm64`, `linux-arm64` and `linux-x86_64`, each carrying the `condition`
     (`${hostSystemName}` equal to `Darwin` or `Linux`);
   - two hidden kind presets, `release` and `debug`, each setting `CMAKE_BUILD_TYPE` (D2);
   - six visible presets, one for every kind and host, named `<kind>-<os>-<arch>` (`release-macos-arm64`,
     `debug-linux-x86_64`, …), each inheriting `base`, its kind and its host. `${presetName}` therefore gives the
     tree its name, and two presets cannot share a tree.

   Each visible preset has a build preset and a test preset of the same name and condition. The test presets set
   `output.outputOnFailure` and `execution.noTestsAction: error`; they set no timeout, because CI passes none today.
   Commands: `cmake --preset release-macos-arm64`, `cmake --build --preset release-macos-arm64`,
   `ctest --preset release-macos-arm64`. `cmake --list-presets` shows the ones for the host's OS.

4. **Everything that names the tree** moves onto the presets: `.clangd`, the CI workflow, `README.md`, `CLAUDE.md` and
   `docs/agent-add-exchange.md`. Then a release.

**What I checked, and what I did not.** The CMake facts the design rests on (the platform answers before `project()`;
`${presetName}` in an inherited `binaryDir`; `condition`; `noTestsAction`; an older CMake ignores the file) were
checked by the design source on CMake 3.18.4, 3.28.3 and 4.2.3. A fact it listed as well, that a refused configure
leaves the cache byte-identical, holds only for a project of one directory; Step 4 says what it does instead. I have
not re-run the others here. Steps 2 and 3 re-prove them as tests, on this repo's CMake: 4.2 on the Mac, 3.28 in the
container, and the runners'. Two things were new to this layout, and the design source has checked both since
(2026-11-02, CMake 4.2.3 and 3.28.3), as Steps 1 and 3 did here: that a visible preset inherits its host's `condition`
through `inherits`, and that a tree nested in the old `build/` tree configures and builds independently of it. This
repo's floor is 3.15, below the 3.18.4 the design source used (see the self-review).

## Decisions to confirm

My defaults, each one a place to edit before approving.

- **D1. The layout is `build/<kind>-<os>-<arch>`.** Rob's refinement (2026-11-02), which the design source adopted the
  same day; its first version had `build-<os>-<arch>`. The kind is one lower-case word: `release` and `debug` have presets, and any other (`bench`,
  `ubsan`, `asan`) is made by hand. `build/` as the parent keeps every tree under one ignored directory, so
  `.gitignore`, an `rsync --exclude build` and an editor's excludes keep working, `rm -rf build` clears them all, and CI
  caches have one root. The costs are longer names (`cmake --preset release-macos-arm64`) and that today's tree stays
  inside the new parent as stale content. The rest of the design stands: presets name the trees, both halves of the
  guard, `.clangd` points at the editing host's tree. The alternative is the design's first version, `build-<os>-<arch>`.
- **D2. The presets make the kind true; the guard does not check it.** Because the kind is in the name, each kind
  preset sets `CMAKE_BUILD_TYPE` (`release` is `Release`, `debug` is `Debug`). The design source sets the build type from the kind too
  (its D4); the presets still set no warning flag and no `CRYPTOCOGS_BUILD_*` option, and CI keeps its
  own flags on its command line. `test_presets` pins kind to build type. The guard examines the operating system and the
  architecture only, because only those fail hard (an `Exec format error`, a link error); a kind that lies on a
  hand-made tree is its maker's. The alternative is for the guard to compare the kind with `CMAKE_BUILD_TYPE` as well:
  more code, and the build type is often unset when a hand-made tree is configured.
- **D3. The module is a copy, not shared.** `cmake/cryptocogs_platform.cmake` is flywheel-dag's module with the prefix
  changed, and its test tables are copies (the boundary rows this plan added are in the design source's table now). This library
  depends on nothing of
  flywheel-dag's and must not start to: the guard runs before `project()`, before anything could be fetched, and a
  shared repo for forty lines costs more than the drift it removes. When a table changes in one repo, change the
  other; each module's header comment says so.
- **D4. CI stays one Linux job.** `ubuntu-latest` (x86_64), `Release`, preset `release-linux-x86_64`; the preset
  carries the build type, so the workflow stops passing `-DCMAKE_BUILD_TYPE`. No macOS leg here: it is a new job
  whose first run may find macOS-only failures, and it belongs in its own issue. The step that touches the workflow
  also moves `actions/checkout` from `@v4` (`using: node20`, a deprecated runtime) to `@v6` (`using: node24`), because
  a deprecation annotation is a failure, and audits every `uses:` in the file (there is only that one).
- **D5. The README keeps `build/` in its usage examples.** The 27 `./build/bin/…` lines, the quickstart and the link
  line are for a user with one platform, who needs no preset and may have CMake older than 3.21; the guard does not
  examine `build`. A short README paragraph says what `build/` stands for and points contributors to `CLAUDE.md`.
  The contributor line (`cd build && ctest`), `CLAUDE.md` and `docs/agent-add-exchange.md` move to the presets.
  The alternative is to rewrite all 32 README lines onto `<tree>`: platform-specific noise for a library's users.
- **D6. v0.6.0.** Before 1.0 a minor release may break here too (v0.5.0 changed the credentials file format; v0.1.1
  removed the compatibility shim). No header, signature or installed file changes, but every documented build command
  moves and a top-level configure refuses a directory named for a platform that is not its own, so it is a minor
  release and not v0.5.7. It is not v1.0.0, which would promise an API this change says nothing about. The package
  version file uses `SameMajorVersion`, which for 0.x accepts any 0.y, so `find_package(cryptocogs 0.5)` accepts
  0.6.0: the tag and a consumer's pin carry the signal, the version file does not. The release is made as v0.5.6 was:
  a separate `chore: bump version to 0.6.0` commit (the one `project(CRYPTOCOGS VERSION …)` line), an annotated tag,
  and `main` and the tag pushed. No GitHub release object: v0.1.1 to v0.5.6 have none (the newest is v0.0.3).
- **D7. Pushes.** Each step's commit is pushed to `main` once it passes locally, and CI's result on the pushed commit
  is part of that step's "done". The release step also pushes the tag. Approving the plan approves those pushes. This
  repo is public: code, docs, commits and issues added here describe this library and name no project that uses it.
- **D8. The container gets libcurl, OpenSSL and zlib development packages before Step 1.** Default: add
  `libcurl4-openssl-dev`, `libssl-dev` and `zlib1g-dev` to the container image (its own repo and its own plan) and
  recreate the container; meanwhile `sudo apt-get install -y libcurl4-openssl-dev libssl-dev zlib1g-dev` in the
  running one makes Step 1
  possible, and is lost when it is recreated. The alternative is the one-off alone. Nothing in this repo changes either way.

## Steps

Every commit subject follows this repo's `type(scope): … (step N/8)` form.

### Step 0 — Approval

The plan is committed as Proposed so it can be read and edited. Nothing else lands until it is approved. A comment on
#25 links it.

**Done when:** `git status` is clean on that commit and #25 carries the link.

Commit: `docs(plan): add plan 028 — one build tree per platform (step 0/8)`.

### Step 1 — Start from green, on both systems

Needs D8's packages. No commit. The trees are named by hand, since the presets do not exist yet, with the names the
presets will give them, and built as CI builds: `Release`. On the Mac, then in the container from `/work/cryptocogs`
(`docker exec ubuntu-dev`; Ubuntu 24.04 on aarch64, GCC 13.3.0, Clang 18.1.3, CMake 3.28.3, ccache, 12 CPUs, 7 GB of
RAM), one after the other and never at once:

```bash
cmake -B build/release-macos-arm64 -DCMAKE_BUILD_TYPE=Release      # build/release-linux-arm64 in the container
cmake --build build/release-macos-arm64 --parallel 8
ctest --test-dir build/release-macos-arm64 --output-on-failure
```

`--parallel 8` and not 12: twelve compilers on 7 GB can have one OOM-killed, which reads as a code failure.

**Done when:** on each system the configure prints no CMake warning, ctest passes in full (the Mac's count is
recorded; `build/` lists 496 today, and the Linux count is recorded too) and every compiler warning is recorded. A
warning other than [#24](https://github.com/tomlin256/cryptocogs/issues/24) (`-Wunused-result` in
`test_ws_client.cpp`, known) is a finding. This is the suite's first run on Linux/aarch64. Anything else: stop and
ask. A failure here is not this plan's to work around.

### Step 2 — The platform tag and the guard

New: `cmake/cryptocogs_platform.cmake`, `tests/tree_checks/check_platform.cmake` and
`tests/tree_checks/check_tree_name.cmake`; edited: `CMakeLists.txt` and `tests/CMakeLists.txt` (the two `add_test`s
sit beside the unit tests, under `CRYPTOCOGS_BUILD_TESTS`). ctest goes from 496 tests to 498. The directory is
`tests/tree_checks/` and not `tests/build_checks/`, which the repo's `.gitignore` (`build*`) would ignore.

**`test_platform_name`** (`check_platform.cmake`, run with `cmake -DSOURCE_DIR=… -P`) holds a fixed table, so it does
not depend on the host it runs on. Each name is a tree's own name, not a path:

| Group | Cases |
|---|---|
| tag | `Darwin`/`arm64` is `macos-arm64`; `Linux`/`aarch64` is `linux-arm64`; `Linux`/`x86_64` and `Linux`/`AMD64` are `linux-x86_64`; `Darwin`/`x86_64` is `macos-x86_64`; `Windows`/`AMD64` is `windows-x86_64` |
| accepted | `release-linux-arm64`, `debug-linux-arm64-gcc13` and `linux-arm64` on `linux-arm64`; `bench-macos-arm64` on `macos-arm64`; `build`, `scratch`, `vscode`, `cmake-build-debug`, `release-linuxbrew` on any; a tree that remembers its own platform; a first configure |
| refused, and the text names both platforms | `release-linux-x86_64` on `linux-arm64`; `linux-x86_64` (no kind) on `linux-arm64`; `debug-macos-arm64` on `linux-arm64`; `release-linux-aarch64` (not the canonical spelling) on `linux-arm64`; `release-linux-arm64x` (the architecture is a prefix of a longer word) on `linux-arm64`; `Release-Linux-X86_64` (case) on `linux-arm64`; a tree that remembers `macos-arm64` on `linux-arm64` |
| refused | `release-linux` (a kind and an OS, no architecture); `release-linux-asan` (no architecture); `linux` |

**`test_tree_name`** (`check_tree_name.cmake`) configures the real top-level project, with the tests, benchmarks and
install rules off (`-DCRYPTOCOGS_BUILD_TESTS=OFF`, so ixwebsocket, googletest, spdlog, CLI11 and backward-cpp are not
needed) and the two dependencies it still fetches taken from the sources this build already has
(`FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON`, `FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS`, with
`FETCHCONTENT_FULLY_DISCONNECTED=ON`), so it needs no network and no compile. Under a scratch directory, in
`<scratch>/build/<name>`, with the host's own platform as the oracle and the other architecture as the lie:

| Case | Expected |
|---|---|
| `release-<host platform>` | configures; the cache holds `CRYPTOCOGS_TREE_PLATFORM` set to the host's platform; configuring it again also succeeds |
| `build` | configures, and records the platform |
| `<host platform>` (no kind) | configures |
| a free-form tree seeded with `-DCRYPTOCOGS_TREE_PLATFORM=<the other platform>` | refused, and the output names both |
| `release-<host OS>-<other architecture>` | refused, the output names the tree, and the cache holds no `CMAKE_CXX_COMPILER` entry |
| `<host OS>-<other architecture>` (no kind) | refused |
| `release-<host OS>` | refused |
| this repo added with `add_subdirectory()` to a scratch parent project, in a tree named for the other architecture | configures (the guard examines the top-level project only) |

Both tests are written first and seen red: the table test fails with the module missing, and `test_tree_name` fails
because the lying configure succeeds. Then the module and the wiring are added. Each hand-made change below is made to
a scratch copy of the repo, which the tests reach through `-DSOURCE_DIR`, and must fail the test and the case named:

| Change | Test: case |
|---|---|
| `aarch64` is not spelled `arm64` | table: `Linux`/`aarch64` |
| `amd64` is not spelled `x86_64` | table: `Linux`/`AMD64` |
| `Darwin` is not `macos` | table: both `Darwin` rows |
| a suffix after the architecture is refused | table: `debug-linux-arm64-gcc13` |
| a kind in front is refused | table: `release-linux-arm64` |
| the kind is required for a name to be examined | table: `linux-x86_64`; configure: the kind-less lie |
| the architecture is matched as a prefix, not a whole word | table: `release-linux-arm64x` |
| names are compared case-sensitively | table: `Release-Linux-X86_64` |
| a name for the wrong architecture is accepted | table: `release-linux-x86_64` |
| a name with no architecture is accepted | table: `release-linux` |
| the remembered platform is ignored | table: the tree that remembers `macos-arm64`; configure: the seeded tree |
| the wiring is removed from `CMakeLists.txt` | configure: the lying tree |
| the platform is never recorded | configure: the cache entry |
| the guard runs after `project()` | configure: the refused tree's compiler entry |
| the guard runs on a subproject too | configure: the subdirectory case |
| the guard passes `CMAKE_HOST_SYSTEM_PROCESSOR`, which is empty before `project()` | configure: the truthful tree is refused |

**Done when:** both tests fail before the module exists and each change above fails as stated; ctest passes in full on
the Mac (498 of 498); `cmake --build build/release-macos-arm64` from Step 1 reconfigures through the guard and rebuilds
nothing; the CI run on the pushed commit is green, where the new tests run on x86_64 Linux.

Commit: `build: refuse a build tree named for another platform (step 2/8)`.

### Step 3 — The presets, and the test that pins them

New: `CMakePresets.json`, `tests/tree_checks/check_presets.cmake`, and a `test_presets` entry in
`tests/CMakeLists.txt` (for the top level and CMake 3.21 or later). ctest goes from 498 tests to 499.

`check_presets.cmake` runs as `cmake -DSOURCE_DIR=<repo> -DHOST_SYSTEM_NAME=<name> [-DPRESETS_DIR=<dir>] -P`. It reads
`<dir>/CMakePresets.json` with `string(JSON)` and fails, naming the rule, unless:

| Rule | Holds |
|---|---|
| R1 | `version` is at least 3 (the first with `condition`) |
| R2 | `base` is hidden and its `binaryDir` is `${sourceDir}/build/${presetName}` |
| R3 | there is one hidden host preset per known host, named `<os>-<arch>` in the module's own spelling, whose `condition` is `${hostSystemName}` equal to that OS's CMake name (`Darwin`, `Linux`) |
| R4 | there is one hidden kind preset per kind, and it sets `CMAKE_BUILD_TYPE`: `release` to `Release`, `debug` to `Debug` |
| R5 | the visible configure presets are exactly the pairs of a kind and a host, each named `<kind>-<os>-<arch>` and inheriting `base`, that kind and that host; none appears twice |
| R6 | each has a build preset and a test preset of the same name, which name it as `configurePreset` and carry its host's condition; each test preset sets `execution.noTestsAction` to `error` and `output.outputOnFailure` to true |
| R7 | `cmake -S <dir> --list-presets=all` lists, for the configure, build and test presets, exactly the presets in the file that are for `HOST_SYSTEM_NAME`'s OS: two of each on Darwin, four on Linux, none elsewhere |

R7 is the end-to-end check that CMake agrees with the file's text, and it is where a visible preset's inheritance of
its host's `condition` is tested here (the design source checked it on CMake 4.2.3 and 3.28.3).

The test is written first and shown red against a tree with no `CMakePresets.json`. Then the presets are added. Each
hand-made change below is made to a scratch copy and passed with `-DPRESETS_DIR`, and must fail on the rule named:

| Change | Fails |
|---|---|
| no `CMakePresets.json` | the file is missing |
| `"version": 2` | R1 |
| `base`'s `binaryDir` is `${sourceDir}/build-${presetName}` (the layout this plan replaced) | R2 |
| `base` loses `"hidden": true` | R2 |
| the `linux-arm64` host preset's `rhs` is `Darwin` | R3 |
| the `release` kind preset sets `Debug` | R4 |
| the `debug` kind preset loses its `CMAKE_BUILD_TYPE` | R4 |
| `release-linux-arm64` is renamed `release-linux-aarch64` | R5 |
| `debug-macos-arm64` loses its kind from `inherits` | R5 |
| the `debug-linux-x86_64` configure preset is deleted | R5 |
| the `release-linux-x86_64` build preset is deleted | R6 |
| the `release-macos-arm64` test preset's `rhs` is `Linux` | R6 |
| a test preset loses `noTestsAction` | R6 |
| the unmodified file, with `-DHOST_SYSTEM_NAME=Linux` on the Mac | R7 |

**Done when:** the test fails without the file and each change above fails as stated; ctest passes in full on the Mac
(499 of 499); `cmake --preset release-macos-arm64` and then `cmake --build --preset release-macos-arm64` reuse the
Step 1 tree and rebuild nothing; `cmake --preset debug-macos-arm64` configures `build/debug-macos-arm64` with
`CMAKE_BUILD_TYPE` set to `Debug` in its cache (configure only); CI is green. The floor probe: a configure under the
oldest CMake at hand (Ubuntu 20.04's 3.16.3, in a throwaway container) ignores the file and succeeds, or the gap is
recorded as unverified.

Commit: `build: presets name one build tree per kind and platform (step 3/8)`.

### Step 4 — The real test: Linux, through the presets

No commit. In the container, from `/work/cryptocogs`, with `build/release-linux-arm64` removed first so the whole path
runs: `cmake --preset release-linux-arm64`, `cmake --build --preset release-linux-arm64 --parallel 8`,
`ctest --preset release-linux-arm64`; then the same for `debug-linux-arm64`. Then:

1. **The trees side by side.** `shasum` of the two release `CMakeCache.txt` files (Mac and container) before and
   after the other platform's run is unchanged; a build of each tree after the other's run rebuilds nothing;
   `git status` is clean, so no tree is tracked.
2. **The architecture in the name, from an x86_64 host.** In a throwaway `--platform linux/amd64` container (an
   `ubuntu:24.04` image with `build-essential`, `cmake`, `git`, `libcurl4-openssl-dev`, `libssl-dev` and `zlib1g-dev`, removed
   afterwards; `ubuntu-dev` is not touched), on the same checkout:
   - `cmake --preset release-linux-arm64` is refused by the guard, naming both platforms, and the arm64 tree is
     intact afterwards: its settings are unchanged, and the next valid configure restores its cache and rebuilds
     nothing. (The design source's plan had the cache byte-identical, from a scratch project of one directory, and corrected itself
     after its own Step 4 found the same; that holds only while the command line adds no `-D`.
     A preset puts `-DCMAKE_BUILD_TYPE` on it, and CMake saves the cache as it stood at the failure, so
     `CMAKE_BUILD_TYPE` becomes an untyped command-line entry and the entries made later in the configure are
     dropped until the next valid one. Found on 2026-11-02.)
   - `cmake --preset release-linux-x86_64` configures and writes `build/release-linux-x86_64` beside
     `build/release-linux-arm64`;
   - `test_tick_price` builds in it and passes under `ctest --preset release-linux-x86_64 -R TickPrice`, and
     `readelf -h` reports x86-64 for its binary and AArch64 for the arm64 tree's.
   The whole x86_64 suite stays CI's: here it would run emulated.
3. **A probe, by hand and not committed, of the reason for the issue.** AddressSanitizer, UndefinedBehaviorSanitizer
   and LeakSanitizer over the full suite in `build/asan-linux-arm64` (`cmake -S . -B build/asan-linux-arm64
   -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined
   -fno-omit-frame-pointer"`, so the fetched dependencies are instrumented too;
   `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1`), and ThreadSanitizer in
   `build/tsan-linux-arm64` (`-fsanitize=thread`). GCC's runtime starts in the container (`vm.mmap_rnd_bits` reads 18 as
   root, and a threaded program built with `-fsanitize=thread` ran, 2026-11-01); Clang's does not (its runtime library is
   not installed). LeakSanitizer is the part macOS cannot do. A sanitizer finding is a bug outside this plan: one
   `bug`-labelled issue per finding, reported, and this plan carries on.

**Done when:** `ctest --preset release-linux-arm64` and `ctest --preset debug-linux-arm64` pass in full (Step 1's Linux
count plus 3), the three checks in 1 hold, every line of 2 holds (the first bullet as amended above), and the outcomes in 3 are recorded as a pass, as
issues, or as "cannot run, and why".

### Step 5 — CI

`.github/workflows/cmake-single-platform.yml` (a GitHub starter template; its comments that stop being true go, such
as "in a 'build' subdirectory"):

- Configure: `cmake --preset release-linux-x86_64`. The preset carries the build type, so `-DCMAKE_BUILD_TYPE` and
  the `BUILD_TYPE` environment variable go.
- Build: `cmake --build --preset release-linux-x86_64`.
- Test: `ctest --preset release-linux-x86_64`, with no `working-directory`; the test preset carries
  `--output-on-failure`.
- `actions/checkout@v4` becomes `@v6` (D4), and every `uses:` is audited. The Install step names `libcurl4-openssl-dev`,
  `libssl-dev` and `zlib1g-dev`: the first only suggests the other two on Ubuntu, and the runner image has them today.

It is its own commit because a workflow can be proven only by running it, and a red run should be the workflow's and
not the docs'.

Tests: none new; `test_presets` (R2, R4, R5) holds the name CI builds the path from, and that it is `Release`. The
workflow is parsed as YAML before the push.

**Done when:** ctest passes in full locally (499 of 499) and CI is green on the pushed commit with 499 tests, and the
run's annotations carry no deprecation notice (I read the annotations pane, not only the conclusion).

Commit: `ci: configure, build and test through the presets; checkout@v6 (step 5/8)`.

### Step 6 — Documentation and `.clangd`

- `.clangd`: `CompilationDatabase: build/debug-macos-arm64`, with a comment that it is the editing host's tree.
- `README.md` (D5): the usage examples stay; "Running the tests" moves to `ctest --preset <kind>-<os>-<arch>`; a short
  paragraph says what `build/` stands for in this file and points contributors to `CLAUDE.md`.
- `CLAUDE.md`: "Common build commands" moves to the presets; a new "Build trees" subsection gives the layout
  (`build/<kind>-<os>-<arch>`), the guard and its reason, the kinds and what each preset sets, and the hand-made kinds
  (`bench`, `ubsan`, `asan`, `tsan`); the "Build outputs" and "Running tests" tables write `<tree>/…` for `build/…`,
  with one line saying what `<tree>` is; the stale test count is corrected to what `ctest -N` says. The container's
  own commands stay out of the repo: they describe one machine.
- `docs/agent-add-exchange.md`: its four lines move to the presets.

Tests: none new; prose and a config line. The leftover check is

```bash
git grep -n -E 'build/(bin|src|_deps|CMakeCache|compile_commands)' -- . ':!docs/plans'
git grep -n -e 'build2' -e 'build-bench' -e 'build-ubsan' -e 'build-compat' -- . ':!docs/plans'
git grep -n -E '(-B|--build|--test-dir|-C|mkdir|cd) +build( |$|`)' -- . ':!docs/plans'
```

which must return the README's usage examples, quickstart and link line (D5) and nothing else.
`clangd --check=include/exchange/common/types.hpp` must report the compile command from
`build/debug-macos-arm64/compile_commands.json`.

**Done when:** the leftover check returns only what D5 keeps, clangd resolves the Mac's database, ctest passes in full
(499 of 499) and CI is green.

Commit: `docs: name the per-platform build trees (step 6/8)`.

### Step 7 — Release v0.6.0

As v0.5.6 was released. Before writing the notes I run `git log v0.5.6..HEAD` and
`git diff v0.5.6..HEAD --stat -- include src`, because a release carries every commit since the last tag. Then:
`chore: bump version to 0.6.0` (the one `project(CRYPTOCOGS VERSION …)` line), an annotated tag
(`git tag -a v0.6.0 -m "v0.6.0 - one build tree per kind and platform: build/<kind>-<os>-<arch>, presets, a guard"`),
`main` and the tag pushed, and #25 closed with a summary comment.

Tests: none new; no test reads the version. The spot check is an install to a scratch prefix and a read of the
`PACKAGE_VERSION` in `cryptocogsConfigVersion.cmake`.

**Done when:** ctest passes in full with no warning (499 of 499), the installed version file reports 0.6.0, CI is
green on the release commit, the tag is pushed, and #25 is closed.

Commit: `chore: bump version to 0.6.0`.

### Step 8 — Close the plan

Mark the plan done and fill in the Outcome.

Commit: `docs(plan): results for 028 — one build tree per platform (step 8/8)`.

## Not in this plan

- **A macOS CI leg**, and **a sanitizer option**: each its own issue. The `bench`, `ubsan`, `asan` and `tsan` kinds
  stay hand-made, which the guard allows, and have no preset.
- **The older plans in `docs/plans/`** keep `build/`. They record what was run.
- **The stale trees on this machine.** The old tree is the contents of `build/` that are not a
  `<kind>-<os>-<arch>` directory (874 MB). `build-bench` (800 MB), `build-compat` (115 MB), `build-ubsan` (548 MB) and
  `build2` (439 MB) are siblings. All are ignored by git and cannot be moved and still work, because each cache holds
  the Mac's absolute path. I leave them; delete the siblings whole, and the old tree's files without the new
  directories, when convenient (2.8 GB, with 37 GiB free on the host). Until then `cmake --build build` still builds
  the old tree, which is stale, and no document names it.
- **Hosts without a preset** (an Intel Mac, Windows, another Linux architecture): the guard accepts a truthful
  `release-macos-x86_64` and a free-form name needs nothing. A preset is three JSON objects when someone has the host.
- **Cross-compiling.** The guard names the host. A cross build uses a free-form name.
- **Anything outside this repo that names `build/`**, such as a consumer's own tree or its pinned tag.

## Self-review — risks and assumptions

- **The container cannot configure this repo yet** (D8). Step 1 waits for the packages.
- **The two modules are copies.** The design source says `build/<kind>-<os>-<arch>` too (flywheel-dag v0.3.0), so the
  plans agree; the modules' tables and error texts must be kept the same by hand (D3).
- **Step 1 may find that the suite does not pass on Linux/aarch64.** It has never run there. Plain `char` is unsigned
  on Linux/aarch64 and signed on x86_64 and on Apple's arm64, and the signing, base64 and hex code is where a
  difference would show (the file #23's undefined behaviour was in). A failure is a finding, not a snag: stop and ask.
  A pass proves a Linux build passes, not CI's exact compiler, so the CI run after each push stays the check.
- **Twelve parallel compilers on 7 GB.** `--parallel 8` is a guess; if a compile dies with "Killed" the number goes
  down, and the number used is recorded in the Outcome.
- **The first Linux build is slow.** It reads and writes a bind mount between the Mac and the container's VM, and it
  builds ixwebsocket, googletest, spdlog, CLI11 and backward-cpp before this library's own sources. ccache is on in
  the image by default, so a second build is fast. A timeout is a finding, not a reason to raise a number.
- **The x86_64 proof is partial on purpose.** It shows the name, the guard and one binary, because the container is
  emulated; CI runs the whole suite on x86_64. If the emulated build is too slow, the binary is one object file, and
  the Outcome says so.
- **New trees nest inside the old one.** `build/` is today a tree with its own `CMakeCache.txt`, `CMakeFiles/`, `bin/`
  and `_deps/`. A tree in `build/release-macos-arm64` shares nothing with it, and neither CMake nor CTest descends into
  a subdirectory it does not know; Step 1 proves it, because a surprise there stops the plan. The old tree stays
  buildable by a stale `cmake --build build`, which is the price of not deleting it first. `rm -rf build` would take
  the new trees with it.
- **Names are long.** `cmake --preset release-macos-arm64` is the price of putting the kind and the platform in the
  name; `cmake --list-presets` prints them.
- **The guard changes what a top-level configure accepts.** A directory named for a platform that is not the host's
  now fails. None of the five trees here has such a name (`build`, `build-bench`, `build-compat`, `build-ubsan`,
  `build2` are not platform trees), which is why the release is a minor one. Running under Rosetta on an Apple Silicon
  Mac is a different platform (`x86_64`) from the native one, and the guard treats it as one. A consumer's tree is
  never examined.
- **A visible preset inherits its host's `condition`** is a fact about CMake that the design source checked (4.2.3 and
  3.28.3, 2026-11-02) and R7 tests end to end here; if a CMake did not inherit it, each visible preset would repeat
  the condition and R5 says so.
- **The floor is CMake 3.15, below the 3.18.4 the design source checked.** The module uses only commands 3.15 has, and
  CMake reads `CMakePresets.json` only for `--preset` or `--list-presets` (3.19 and later), so an older CMake ignores
  it. Step 3's probe on 3.16.3 tests that; 3.15 itself is not available to me.
- **The version file does not enforce the break** (D6): `SameMajorVersion` accepts any 0.y for a 0.x request.
- **`CLAUDE.md` is 90 KB with 66 build references.** The substitution is mechanical, and the leftover check is what
  makes a miss visible.
- **CI is proven only by pushing.** The runner's CMake is assumed to be 3.21 or later (`ubuntu-latest` ships a current
  one). A red Step 5 is fixed forward.
- **The two systems share the sources.** Editing a file while the other system builds, or running both builds at
  once, makes a loaded machine time-sensitive. The steps run one system at a time.
- **The bind mount is case-insensitive** (APFS), so the Linux build here cannot catch a filename-case bug. CI stays
  the check for that. It is also why the guard compares names in lower case.
- **Disk.** The host has 37 GiB free; each Linux tree is about the size of the Mac's (0.5 to 0.9 GB).
- **`.clangd` is wrong on another host** until its one line is edited.
- **This repo is public.** The plan, the docs and the commit bodies describe this library only.

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 — Approval | Done | Approved as written, 2026-11-02 |
| 1 — Start from green | Done (2026-11-02) | Mac and container: 496 of 496, Release, GCC 13.3.0 / Apple Clang 21 / CMake 3.28.3 / 4.2.3. Mac: configure 29 s, build 20 s (`--parallel 8`), ctest 2.6 s. Container: configure, build and ctest 46 s (`--parallel 8`), ctest 1.4 s. Not warning-free, none of it this repo's own files: CMake deprecation warnings from backward-cpp (and CLI11 on 4.2) and `-Wunused-parameter` from ixwebsocket ([#26](https://github.com/tomlin256/cryptocogs/issues/26)), `-Wunused-result` on Apple Clang ([#24](https://github.com/tomlin256/cryptocogs/issues/24)). The container also needed `zlib1g-dev`, which D8 had not named: `libcurl4-openssl-dev` only suggests it. A tree nested in the old `build/` configures and builds on its own. |
| 2 — Platform tag and guard | Done (2026-11-02) | 498 of 498 on the Mac and in the container; `cmake --build` of the Step 1 tree reconfigured through the guard and compiled nothing. Both tests were seen red first; 17 hand-made changes (the 16 rows, `kind required` and `remembered` each checked in both tests) fail as stated. |
| 3 — Presets and their test | Done (2026-11-02) | 499 of 499 through `ctest --preset release-macos-arm64`; the Step 1 tree was reused and nothing rebuilt; `debug-macos-arm64` configures with `CMAKE_BUILD_TYPE` `Debug`. `cmake --list-presets=all` on the Mac lists only the two Mac presets, so a visible preset does inherit its host's `condition` (CMake 4.2.3). Seen red against a tree with no file; the 14 hand-made changes fail on the rule named. Floor probe, Ubuntu 20.04 / CMake 3.16.3 / GCC 9.4 (arm64 container): the file is ignored (`cmake --preset` is an unknown option there), the guard accepts `release-linux-arm64`, refuses `release-linux-x86_64` with the right text, and accepts a free-form name; 3.15 itself was not available. |
| 4 — Linux, through the presets | Done (2026-11-02) | Container, GCC 13.3.0, CMake 3.28.3, `--parallel 8`: `release-linux-arm64` 499 of 499 (configure, build and ctest from nothing: 31 s) and `debug-linux-arm64` 499 of 499 (55 s). Side by side: the Mac tree's cache has the same SHA-1 after the Linux run, the Linux tree's after the Mac run, and each rebuilt nothing after the other's run; `git status` shows no tree. Emulated `linux/amd64` container (Ubuntu 24.04, GCC 13.3.0): `cmake --preset release-linux-arm64` is refused naming both platforms; `release-linux-x86_64` configures (56 s) beside it, `test_tick_price` builds (13 s) and its 11 tests pass; `readelf` says x86-64 for it and AArch64 for the arm64 tree's. The refusal rewrote the arm64 tree's cache (see above: not byte-identical); the next valid configure restored it, 0 compiles, 499 of 499. Sanitizers (hand-made `asan` and `tsan` kinds, `RelWithDebInfo`): ASan + UBSan + LeakSanitizer 499 of 499, no report (configure 40 s, build 133 s, ctest 8 s); TSan: 497 of 499, two reports, both races in the tests and not the library: [#27](https://github.com/tomlin256/cryptocogs/issues/27) (`Execute.BlocksUntilResponse`) and [#28](https://github.com/tomlin256/cryptocogs/issues/28) (`WsReconnectSession.BackoffResetsAfterSuccessfulReconnect`). |
| 5 — CI | Done (2026-11-02) | `cmake-single-platform.yml` configures, builds and tests through `release-linux-x86_64`; `checkout@v4` (node20) became `@v6` (node24), the only action in the file. CI green with 499 tests. The run's annotations hold one notice, that the `ubuntu-latest` label is migrating to Ubuntu 26, and no deprecation. |
| 6 — Documentation | Done (2026-11-02) | `.clangd` reads `build/debug-macos-arm64` (clangd loaded its `compile_commands.json`); `CLAUDE.md` has a "Build trees" subsection, preset commands, `<tree>` in its 59 path lines, the zlib dependency and the corrected test count; `docs/agent-add-exchange.md` and the README's tests command moved to the presets. The leftover check returns only the README's quick start, usage examples, install snippet and link line (D5). |
| 7 — Release v0.6.0 | | |
| 8 — Close the plan | | |
