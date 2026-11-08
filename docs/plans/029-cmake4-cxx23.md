# CMake 4 and C++23, released as v1.0.0

**Status:** Done (2026-11-04), released as v1.0.0, apart from the checks that need the Linux container (Step 8; the Outcome
says which and why). Rob approved the plan as written, so D1 to D9 stood at the defaults below, and approved pushing this
plan's commits and linking it from #32.
**Design source:** flywheel-dag's [require_cmake4_plan.md](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/require_cmake4_plan.md)
([flywheel-dag#57](https://github.com/tomlin256/flywheel-dag/issues/57)) and
[require_cxx23_plan.md](https://github.com/tomlin256/flywheel-dag/blob/main/docs/plans/require_cxx23_plan.md)
([flywheel-dag#56](https://github.com/tomlin256/flywheel-dag/issues/56)), released together as flywheel-dag v1.0.0. This
plan applies those designs here and restates only what differs.
**Closes:** [GitHub issue #32](https://github.com/tomlin256/cryptocogs/issues/32) — "Bump CMake to 4 and move to c++23". The
issue reads: *"As per flywheel-dag. Tag as a major release."*

## Problem

The repo asks for CMake 3 and C++17 in these places (`git grep` on 2026-11-04, outside `docs/plans/`):

| Where | What |
|---|---|
| `CMakeLists.txt:1` | `cmake_minimum_required(VERSION 3.15...4.2)`, a range |
| `CMakeLists.txt:34` | `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)`, for the fetched dependencies (below) |
| `CMakeLists.txt:39` | `set(CMAKE_CXX_STANDARD 17)` |
| `src/CMakeLists.txt:38, 60, 82, 111, 136, 163` | `target_compile_features(<lib> PUBLIC cxx_std_17)` on `exchange_common`, `exchange_http` and the four adapters; the export of plan cmake-install-nits hands it to every `find_package` consumer |
| `tests/tree_checks/check_platform.cmake:14`, `check_tree_name.cmake:15` and `:143`, `check_presets.cmake:31` | `cmake_minimum_required` 3.15, 3.15 (twice; the second is in the scratch project the test writes) and 3.19 |
| `tests/CMakeLists.txt:165` | `test_presets` is registered only for CMake 3.21 or later |
| `cmake/cryptocogs_platform.cmake:25` | "Only commands CMake 3.15 has are used" |
| `CLAUDE.md:181, 216-218, 253, 1310` | "CMake 3.15+, C++17 required", the `cxx_std_17` export, the 3.15 floor, "C++17 throughout" |
| `README.md` | says neither: a user is told nothing about a compiler or a CMake |

The issue's premise is flywheel-dag's: abandon CMake 3 and C++17, and call it a major release, because every consumer has
to upgrade. Nothing in the library needs a C++23 feature yet, so this plan moves the floor and makes it real, and adopts no
C++23 feature.

### What I measured (2026-11-04)

Scratch copies of `4f08adb` under `build/probe/` (ignored by git; no tracked file was touched), configured through the host
preset, with the dependencies' sources taken from the old tree so nothing was cloned. Release, `--parallel 8`.

| Compiler, CMake | Result |
|---|---|
| Apple Clang 21.0.0, libc++, CMake 4.4.5, `-std=gnu++17` (the control) | builds in 19 s; ctest **499 of 499**; 8 warnings: the six `-Wunused-result` of [#24](https://github.com/tomlin256/cryptocogs/issues/24) and the two `-Wunused-parameter` of [#26](https://github.com/tomlin256/cryptocogs/issues/26) |
| the same at `-std=gnu++23`, floor 4.0, six `cxx_std_23` | builds in 22 s; **499 of 499**; 9 warnings: the same 8 and one new, googletest's (below) |
| the same with googletest v1.18.0 | builds; **499 of 499**; the same 8 and nothing else |
| Ubuntu 26.04 (CI's image), aarch64, GCC 15.2.0 (CI's compiler), apt's CMake 4.2.3, C++23 | builds in 58 s; **499 of 499**; 2 warnings, both ixwebsocket's (#26) |

- **The sources need no edit to compile as C++23.** 86 of the 125 translation units compile at C++23: this project's own 75
  (`gnu++23`) and the 11 of spdlog and googletest. The other 39 are ixwebsocket's 37 and backward-cpp's 2, which set C++11
  for themselves; their headers are included from C++23 code without a warning. I also grepped `include/`, `src/` and
  `tests/` (the fixture headers apart) for what C++20 and C++23 remove, deprecate or re-type: `u8` literals and `char8_t`,
  `std::result_of`, `std::iterator`, `std::is_pod`, `std::uncaught_exception`, the atomic free functions on `shared_ptr`,
  `<codecvt>`, `std::aligned_storage`, `std::allocator::construct`, `<=>`, `volatile`, `[=]` lambdas, the pre-C++17 function
  adaptors. All 23 patterns count zero.
- **One new diagnostic, and it is googletest's.** v1.16.0 prints `gtest-printers.h:528:35: warning: implicit conversion
  from 'char8_t' to 'char32_t' [-Wcharacter-conversion]` when compiled as C++20 or later on Apple Clang 21. v1.18.0
  (2026-08-10, CMake 3.16) does not, and needs only C++17, so it can land before the standard moves. flywheel-dag#56 found
  the same line.
- **A floor of 4.0 changes nothing that CMake generates.** The same CMake (4.4.5) configured `4f08adb` as it is
  (`3.15...4.2`) and with only a plain `4.0` minimum: 673 generated files each, identical once the tree roots are
  normalized. The two that differ are CMake's own compiler-ABI probe executables, not project output. So the policies of
  CMake 4.1 and 4.2, which the range sets NEW today, change nothing that CMake generates here.
- **`CMAKE_POLICY_VERSION_MINIMUM 3.5` has to stay.** CMake 4 stops on a project that asks for less than 3.5, and two
  fetched dependencies do: backward-cpp v1.6 (`cmake_minimum_required(VERSION 3.0)`) and ixwebsocket v12.0.0
  (`3.4.1...3.17.2`). The others ask 3.5 or more (CLI11 3.5, googletest v1.16.0 3.13, nlohmann/json 3.5...4.0, spdlog
  3.10...3.21, toml++ 3.14). backward-cpp and CLI11 also print "Compatibility with CMake < 3.10 will be removed from a
  future version of CMake" (#26); this plan does not change that.
- **CI's compiler is GCC 15.2.0 on the `ubuntu-26.04` image** (run 41476947007's log). The workflow does not print its
  CMake, so the runner's version is not known to me. Step 2 prints it before anything depends on it.
- **Not yet run: GCC 13.3 and Clang 18.1.3 in the container.** The recreated `ubuntu-dev` has no libcurl, OpenSSL or zlib
  development files (`dpkg-query` finds none of the three packages), so nothing configures there. That is
  [docker-ubuntu#3](https://github.com/tomlin256/docker-ubuntu/issues/3), whose fix is committed in that repository and
  not yet in the live container (D9). Step 1 runs both compilers at C++17 and Step 8 at C++23.

## The change

Seven parts, all from flywheel-dag's two plans except T3, with this repo's differences.

1. **A plain CMake 4.0 minimum** in the root `CMakeLists.txt`, with a comment that says why it is not a
   `<min>...<max>` range, and that `CMAKE_POLICY_VERSION_MINIMUM 3.5` is still there for two dependencies. Every other
   `cmake_minimum_required` in the checkout (the three check scripts, the project one of them writes) becomes 4.0, the
   `CMAKE_VERSION VERSION_GREATER_EQUAL 3.21` condition on `test_presets` goes, and `CMakePresets.json` **stays** at
   `cmakeMinimumRequired` 3.21: it says what CMake can read the file, not what the project needs, and a higher number
   turns CMake 3's clear refusal at the root (`CMake 4.0 or higher is required`) into the confusing "major version 4
   must be less than 3" (flywheel-dag#57 measured it).
2. **C++23.** `CMAKE_CXX_STANDARD 23`, and `cxx_std_23` on the six library targets, with the comment in
   `src/CMakeLists.txt` rewritten. The export hands `cxx_std_23` to a `find_package` consumer, whose target is then
   compiled as C++23 (a higher standard of its own is left alone).
3. **A guard.** `include/exchange/common/cxx_standard.hpp` (new, with the banner) holds
   ```cpp
   // cryptocogs is C++23 (cryptocogs#32). Linking cryptocogs::<adapter> from CMake asks for it (cxx_std_23); a build that
   // does not use CMake must pass -std=c++23 itself. This tests for newer than C++20, not for C++23's own value: GCC 13
   // reports 202100L for -std=c++23, so ">= 202302L" would reject it.
   #if __cplusplus <= 202002L
   #error "cryptocogs requires C++23: compile with -std=c++23, or link cryptocogs::<adapter> from CMake, which asks for it"
   #endif
   ```
   and the seven public headers that include no other `exchange/` header include it first: `common/credentials_file.hpp`,
   `tick_price.hpp`, `types.hpp`, `ws.hpp`, `rest.hpp`, `reconnect_session.hpp` and `cryptocom/auth.hpp` (read from the
   include graph on 2026-11-04). Every other of the 32 public headers reaches one of them. A header added later is caught
   by T4 below, which fails until it has the guard.
4. **googletest v1.16.0 to v1.18.0**, before the standard moves, so no commit prints the new warning.
5. **Four `cmake -P` check scripts in `tests/cmake_checks/`** (new; not a name beginning `build`, which `.gitignore`
   ignores), registered at the top level like the checks of plan platform-build-dirs:

   | Test | Script | Asserts |
   |---|---|---|
   | T1 `test_cmake_floor` | `check_cmake_floor.cmake` | the root has one plain `cmake_minimum_required`, and that is the floor; every other one outside `build*/`, `cmake-build-*/` (CLion's, also ignored) and `.git/`, scripts added later included, is exactly that version; `README.md` says "CMake 4.0 or later"; it fails if it finds no root call, so a wrong path cannot pass by checking nothing |
   | T3 `test_standard_requirement` | `check_standard_requirement.cmake` | every library target created in `src/` (the directory's `BUILDSYSTEM_TARGETS`, so a library added later is covered) has `cxx_std_23` as the highest `cxx_std_NN` in its `INTERFACE_COMPILE_FEATURES`, which is what the export copies to a consumer and CMake compiles a consumer at; read by `$<TARGET_PROPERTY:…>` at generate time, so it needs no install, no network and no compile. *(Amended at Step 5: the expression is transitive and adds nlohmann_json's `cxx_std_11` and the features of every linked library, so "no other `cxx_std_NN`" could never hold; the highest is the standard a consumer gets.)* |
   | T4 `test_standard_guard` | `check_standard_guard.cmake` | each of `include/exchange/*/*.hpp` (32 when written, 33 with the guard header itself) fails to preprocess at `-std=c++17` and at `-std=c++20` with "requires C++23" on stderr (*amended at Step 6: `-E`, not `-fsyntax-only`, because the guard is a preprocessor `#error` and compiling 64 units took 19 s*); it names every header that compiled, or failed with another message; it fails if the glob finds none. Registered for GCC and Clang, whose `-std=` and `-fsyntax-only` it passes |
   | T5 `test_package_version` | `check_package_version.cmake` | which `find_package(cryptocogs <request>)` requests `cryptocogsConfigVersion.cmake` accepts, with the table derived from `PROJECT_VERSION` (below) |

   flywheel-dag has the first, third and last. T3 is mine: flywheel-dag's consumer tests are what prove its usage
   requirement reaches a consumer, and this repo has no consumer project.
6. **Words**, each in the step that makes it true: `README.md` gets the requirement ("C++23 compiler and CMake 4.0 or
   later") and a paragraph for consumers; `CLAUDE.md` its five lines and, at the end, the test tables;
   `docs/agent-add-exchange.md` says a new adapter's library carries `cxx_std_23` (T3 fails without it).
7. **The release.** `project(CRYPTOCOGS VERSION 1.0.0 …)`. The version file keeps `COMPATIBILITY SameMajorVersion`,
   which at 1.0.0 finally means something: at 0.6.0 it accepts any 0.y, so `find_package(cryptocogs 0.5)` accepts 0.6.0
   today, and `find_package(cryptocogs 0.6)` must not be handed a C++23-only 1.0.0. T5's table, for `PROJECT_VERSION`
   M.N.P:

   | Request | Verdict |
   |---|---|
   | `M`, `M.0`, `M.N`, `M.N.P`, `M.N.0`, and `M.(N-1)` when N > 0 | accepted |
   | `M.(N+1)`, `M.N.(P+1)`, `M+1`, `M+1.0` | refused |
   | `0.5.6`, `0.6`, `0.6.0`, `0.9` while M ≥ 1: the series before the major release | refused |

## Decisions to confirm

My defaults, each one a place to edit before approving.

- **D1. The release is v1.0.0.** I read "tag as a major release" as the major version, as flywheel-dag did. It makes
  `SameMajorVersion` enforce the break (above), and from then on a change that breaks consumers needs 2.0. A tag cannot be
  taken back once a consumer pins it. The alternative is **v0.7.0**, which reads "major" as "big": the version file then
  accepts a request for 0.6 (`SameMajorVersion` ignores the minor in 0.x), so only the tag and a consumer's pin carry the
  signal. Plan platform-build-dirs (D6) refused 1.0.0 because that change said nothing about the API; this one is the break.
- **D2. The floor is a plain `cmake_minimum_required(VERSION 4.0)`.** Not the `3.15...4.2` range, whose policy
  settings are the audit a range needs and whose result I measured to be identical (673 files); not 4.2 or 4.4, which are
  only what the Mac or the runner happens to have. 4.0.7 is the version Step 8 runs to make the floor real.
  `CMAKE_POLICY_VERSION_MINIMUM 3.5` stays.
- **D3. The header guard and T4.** Without it only CMake enforces the floor, and a consumer on another build system
  learns of it when the library first uses a C++23 feature (none does). With it any build gets one line. I recommend it.
  Strike Step 6 for the CMake change alone; T3 stays.
- **D4. googletest v1.18.0** (Step 4). The alternative is a flag that silences the one new warning, which is the wrong
  answer; the other is to live with it on the Mac. It is a test-only dependency and nothing is installed or exported.
- **D5. T3 is mine, T5 is flywheel-dag's, and each can be struck on its own.** Without T3
  nothing notices a library target that loses its
  `cxx_std_23` (the top-level `CMAKE_CXX_STANDARD` still compiles it, and the export silently drops the requirement:
  the hazard plan cmake-install-nits fixed by hand). Without T5 nothing but the version number holds the promise that a 0.x consumer is
  refused.
- **D6. CI prints its CMake and compiler versions, and gets no leg for the floor.** flywheel-dag added a job on
  Kitware's 4.0.7 and removed it again ([flywheel-dag#59](https://github.com/tomlin256/flywheel-dag/issues/59)): a CMake
  before 4.4 ignores `-Werror=author`, and no CI job runs the floor now. Step 8 runs 4.0.7 by hand. CI stays one Linux job
  (a macOS leg is [#29](https://github.com/tomlin256/cryptocogs/issues/29)).
- **D7. Tag only, as v0.6.0 was.** An annotated tag `v1.0.0`, and no GitHub release object: v0.1.1 to v0.6.0 have none.
  The cost is that GitHub's "Latest release" still reads v0.0.3 (2026-03-15). The alternative is to publish a v1.0.0
  release whose notes lead with both breaks and say what each kind of consumer sees; that is public and outward-facing, so
  it is yours to say yes to.
- **D8. Pushes.** Each step's commit is pushed to `main` once it passes locally, and CI's result on the pushed commit is
  part of that step's "done"; the release step also pushes the tag. Approving the plan approves those pushes. This repo is
  public: the plan, the docs and the commit bodies describe this library only. (Git over SSH from this session was denied
  twice and then worked, `ls-remote` at 2026-11-04; if a push is refused I stop and say so.)
- **D9. The container.** The image is fixed in [docker-ubuntu#3](https://github.com/tomlin256/docker-ubuntu/issues/3)
  (you approved the Dockerfile edit on 2026-11-04): three commits there (its plan, the Dockerfile and its tests, the
  docs), not pushed, and #3 still open. The live `ubuntu-dev` gets the
  packages only when you recreate it with `./start.sh --detach`, which closes any shell open in it, so that is yours.
  Until then Step 1 installs them by hand in the running container (`sudo apt-get install -y --no-install-recommends
  libcurl4-openssl-dev libssl-dev zlib1g-dev`), as plan platform-build-dirs's D8 did; the install is lost at the recreate, and the new
  image has them anyway. The alternative is to wait for the recreate.

## Steps

Every commit subject follows this repo's `type(scope): … (step N/10)` form, and no message carries a `Co-Authored-By`
line. Each step that commits ends with a full build and a full ctest run on the Mac (`release-macos-arm64`) and in the
container (`release-linux-arm64`, from `/work/cryptocogs`), one after the other and never at once, with `--parallel 8`
(twelve compilers on 7 GB can have one killed, which reads as a code failure). Nothing is pushed until its step is green
locally.

### Step 0 — Approval

The plan is committed as Proposed so it can be read and edited. A comment on #32 links it, and a commit records the
approval here, with D1 to D9 as you decide them.

**Done when:** `git status` is clean on that commit and #32 carries the link.

Commit: `docs(plan): add plan 029 — CMake 4 and C++23, released as v1.0.0 (step 0/10)`.

### Step 1 — Start from green, on both systems

Needs D9's packages in the container. No commit. At the Step 0 commit: the Mac, then the container's
`release-linux-arm64` and `debug-linux-arm64` (GCC 13.3.0, CMake 4.4.5), and a hand-made `clang-linux-arm64` tree with
`-DCMAKE_CXX_COMPILER=clang++` (Clang 18.1.3 on libstdc++ 13): the control at C++17. flywheel-dag found a test of its own
that Clang before 19 on libstdc++ cannot build, at C++17 as well (flywheel-dag#58); I do not know that this library
is unaffected, and this finds out.

**Done when:** on each tree ctest passes in full (499 of 499), the warnings are the ones #24 and #26 track and nothing
else, and the numbers are recorded. Anything else: stop and ask.

### Step 2 — CI shows its tools

`.github/workflows/cmake-single-platform.yml` gets a step before Configure that prints `cmake --version | head -1` and
`c++ --version | head -1`. Tests: none new; a workflow is proven only by running.

**Done when:** CI is green on the pushed commit, and its log prints a CMake of 4.0 or later and GCC 15. If the runner's
CMake is older than 4.0, stop: the floor cannot move until it does.

Commit: `ci: print the CMake and compiler versions (step 2/10)`. Pushed on its own.

### Step 3 — Require CMake 4 (D2)

The root `CMakeLists.txt` (minimum and comment), the four minimums in `tests/tree_checks/`, `tests/CMakeLists.txt`
(`test_presets` unconditional, T1 registered, the comment over it), the comment in `cmake/cryptocogs_platform.cmake`,
`README.md` ("Requires CMake 4.0 or later", in the quick start), `CLAUDE.md:181` and `:253`, and the new
`tests/cmake_checks/check_cmake_floor.cmake`.

| Test | Asserts |
|---|---|
| T1 `test_cmake_floor` (new) | see the table above |
| T2 `test_platform_name`, `test_tree_name`, `test_presets` (existing) | still pass under the new minimums |

T1 is written first and seen red: it names the root's range, the four scripts at 3.15 and 3.19 and the README. Then the
change makes it green. Each hand-made change below is made on a scratch copy and never restored (Make compares mtimes in
whole seconds):

| Change | Fails |
|---|---|
| M1 a check script back at `cmake_minimum_required(VERSION 3.15)` | T1, naming the file and both versions |
| M2 a script at `4.0...4.4` | T1, naming it as a range |
| M3 the README back at "CMake 3.15 or later" | T1, naming the README |
| M4 the root at 4.2 and everything else at 4.0 | T1, naming every other file |
| M5 the root's call deleted | T1, "found 0 calls that set a minimum version" |
| M6 a script added later, in a directory of its own, with a stale minimum | T1, naming it |

**Done when:** T1 is red then green; ctest passes in full on both systems (500 of 500) with no new warning; the files
CMake generates differ from `4f08adb`'s for the same CMake only by T1's entry in `tests/CTestTestfile.cmake` (the
comparison of the probe, repeated); M1 to M6 fail as stated; CI is green.

Commit: `build: require CMake 4 (step 3/10)`.

### Step 4 — googletest v1.18.0 (D4)

`GIT_TAG v1.18.0` in the root `CMakeLists.txt`, and the README's dependency row. Tests: none new; the suite is the test.

**Done when:** the Mac's build prints the eight warnings of Step 1 and no ninth; ctest passes in full (500 of 500) on both
systems; CI is green. Anything new from a v1.18.0 header: stop and report it.

Commit: `build: update googletest to v1.18.0 (step 4/10)`.

### Step 5 — Require C++23

`CMakeLists.txt` (`CMAKE_CXX_STANDARD 23`), `src/CMakeLists.txt` (the six `cxx_std_23` and the comment over the first),
`CLAUDE.md:181, 216-218, 1310` (C++23, and that no C++23 feature is adopted), `docs/agent-add-exchange.md` (a new
adapter's library carries `cxx_std_23`), and the new `tests/cmake_checks/check_standard_requirement.cmake`.

| Test | Asserts |
|---|---|
| T3 `test_standard_requirement` (new) | see the table above |

T3 is written first and seen red against the unchanged `cxx_std_17`, naming all six targets.

| Change | Fails |
|---|---|
| M7 `exchange_common` back at `cxx_std_17` | T3, naming it (the adapters keep their own 23) |
| M8 `exchange_common` with no `target_compile_features` | T3, naming it: `cxx_std_11`, nlohmann_json's, is all it is left with |
| M9 a fifth library added in `src/` that links nothing and has none | T3, naming it (the directory's targets are what it reads) |
| M9b `exchange_http` at `cxx_std_26` | T3, naming it and the four adapters that link it: a standard nobody has chosen |
| `kraken` alone back at `cxx_std_17` | passes, on purpose: it inherits 23 from `exchange_common`, so a consumer is compiled at C++23 either way |

*(Amended at Step 5: M7 to M9 as first written were one library back at 17, one with none, and a fifth with none, which assumed the property is not transitive.)*

**Done when:** T3 is red then green; `git grep -nE "CXX_STANDARD|cxx_std" -- CMakeLists.txt src` shows 23 and nothing
else; the compile database has no `-std=…17` for this project's own sources; ctest passes in full (501 of 501) on both
systems, with the same eight warnings on the Mac and none new; M7 to M9b fail as stated and the inherited case passes;
CI is green on GCC 15.2.

Commit: `build: require C++23 (step 5/10)`. Pushed on its own, because CI's compiler is the arbiter.

### Step 6 — Refuse to compile below C++23 (D3)

`include/exchange/common/cxx_standard.hpp`, the seven includes, `tests/cmake_checks/check_standard_guard.cmake` with its
registration (`-I` for the headers' own dependencies, so that the guard is the only failure), and the README paragraph
for consumers: link `cryptocogs::<adapter>` and your target is compiled as C++23; a build outside CMake passes
`-std=c++23`; adding the library with `FetchContent` needs CMake 4.0 or later in your project too, because the library's
own `CMakeLists.txt` asks for it. (What an installed package asks of an older CMake waits for Step 8's measurement, and
the README says nothing about it until then.)

| Test | Asserts |
|---|---|
| T4 `test_standard_guard` (new) | see the table above |

T4 is written first and seen red on the tree without the guard, naming all 32 headers.

| Change | Fails |
|---|---|
| M10 the guard compares `<= 201703L` | T4, naming every header at `-std=c++20` |
| M11 the guard compares `< 202302L` | not T4, which compiles below C++23 only; the whole suite on GCC 13.3, which reports 202100L. CI cannot see this one (GCC 15.2 and Apple Clang 21 report 202302L), so the comment names the value and the container run is the check |
| M12 `cryptocom/auth.hpp` without the include | T4, naming it |
| M13 the script's glob points at a directory with no header | T4, "found no header" |

**Done when:** T4 is red then green; ctest passes in full (502 of 502) on both systems, no new warning; M10 to M13 fail as
stated, and with M11 the container build of GCC 13.3 stops at the guard; CI is green.

Commit: `feat: refuse to compile below C++23 (step 6/10)`. Pushed on its own.

### Step 7 — Hold what `find_package` accepts

`tests/cmake_checks/check_package_version.cmake`, registered when `CRYPTOCOGS_INSTALL` is on (the version file exists
only then), reading `${PROJECT_BINARY_DIR}/cryptocogsConfigVersion.cmake`.

| Test | Asserts |
|---|---|
| T5 `test_package_version` (new) | the table in "The change" 7, for the `PROJECT_VERSION` of the build |

It passes at 0.6.0 (the table for M = 0) and at 1.0.0, so it is seen red against hand-made version files, each a scratch
copy of the generated one:

| Change | Fails |
|---|---|
| M14 the 0.6.0 file asked as 1.0.0 (the bump forgotten) | T5, "a request for 1 is refused" |
| M15 `COMPATIBILITY SameMinorVersion` | T5, `1` refused at 1.0.0 |
| M16 `COMPATIBILITY AnyNewerVersion` | T5, `0.6` and `0.9` accepted at 1.0.0 |
| M17 `COMPATIBILITY ExactVersion` | T5, `1` and `1.0` refused at 1.0.0 |

**Done when:** M14 to M17 fail as stated; ctest passes in full (503 of 503) on both systems; CI is green. (M15 to M17 are
run with the bump applied to a scratch copy, so they are not in a commit.)

Commit: `test: hold what find_package accepts (step 7/10)`.

### Step 8 — Verify, on the tip

In scratch copies, so that no tree of the checkout is disturbed. The one possible commit is a README sentence, below:

- **The floor is real.** CMake 4.0.7 (Kitware's macOS tarball, its SHA-256 checked against the release's own file): a full
  configure, build and ctest on the Mac, with no CMake or compiler warning beyond the known ones. CMake 4.4.5 again.
- **CMake 3 is refused clearly.** CMake 3.31.x on the Mac prints `CMake 4.0 or higher is required` for the library as the
  top-level project, through `cmake --preset`, and for a consumer whose own minimum is lower and which pulls the library in
  with `FetchContent`. And an installed package: a scratch consumer under CMake 3.31.x configures, builds and runs
  against a prefix installed by CMake 4.4.5, or it does not, and the README says which.
- **The compilers.** In the container at C++23: GCC 13.3.0 (`release-linux-arm64`, `debug-linux-arm64`) and Clang 18.1.3
  (`clang-linux-arm64`), full ctest each, against Step 1's C++17 numbers. AddressSanitizer with UndefinedBehaviorSanitizer
  and LeakSanitizer over the full suite in a hand-made `asan-linux-arm64` tree, as plan platform-build-dirs Step 4 ran it (flags
  `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer`,
  `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1`). ThreadSanitizer is not rerun: its
  two reports are the test races of #27 and #28 and nothing here touches them.
- **Speed.** The benchmark binaries (`CRYPTOCOGS_BUILD_BENCHMARKS=ON`, plans kraken-performance-profiling,
  kraken-reserve-vector-growth and remaining-adapters-reserve-vector-growth) built at C++17 and at C++23 in
  two hand-made Mac trees, five repetitions each, interleaved, the means compared against the coefficients of variation the
  plans record (about 1% for Kraken; 3 to 5% at the smallest absolute scales). A row beyond that is repeated before it is
  called real.

**Done when:** every run passes; the refusals match; no benchmark row moves beyond the recorded noise; a sanitizer
finding is a bug outside this plan and becomes one `bug`-labelled issue, and the plan carries on. Any other result: stop
and report it, with the numbers.

Commit, only if the installed-package run shows something to say: `docs: say what an installed package asks of CMake
(step 8/10)`, one README sentence written from what the run showed, and a full build and ctest first as for any commit.

### Step 9 — Release v1.0.0 (D1, D7)

As v0.6.0 was released. Before writing the notes: `git log --format='%h %s' v0.6.0..HEAD` and
`git diff v0.6.0..HEAD --stat -- include src`, because a release carries every commit since the last tag; the `include/`
diff should be the guard header and its seven includes, and `src/` the six `cxx_std_23` lines. Then
`chore: bump version to 1.0.0` (the one `project(CRYPTOCOGS VERSION …)` line), an annotated tag
(`git tag -a v1.0.0 -m "v1.0.0 - C++23 and CMake 4.0 required"`), `main` and the tag pushed, and #32 closed with a summary
comment that leads with both breaks. Tests: none new; T5 holds the policy. The spot check is an install to a scratch
prefix and a read of `PACKAGE_VERSION` in `cryptocogsConfigVersion.cmake`.

**Done when:** ctest passes in full (503 of 503) on both systems, the installed version file reports 1.0.0, CI is green on
the release commit, the tag is pushed, and #32 is closed.

Commit: `chore: bump version to 1.0.0`.

### Step 10 — Close the plan

`CLAUDE.md`'s test tables and counts (the four new checks, the new total), the plan marked Done with its Outcome and
Progress.

Commit: `docs(plan): results for 029 — CMake 4 and C++23 (step 10/10)`.

## Designs I rejected

- **Keeping the `3.15...4.2` range, or `4.0...4.4`.** A range lets a CMake 3.15 run the project, which is the opposite of
  the issue, and sets every policy up to its maximum to NEW, which needs its own audit; the plain minimum generates the
  same 673 files.
- **A floor of 4.2 or 4.4.** That is what the Mac or the runner has. It would refuse a consumer on 4.0 or 4.1 for no reason
  the project has.
- **Dropping `CMAKE_POLICY_VERSION_MINIMUM`.** backward-cpp and ixwebsocket would stop the configure.
- **Guard on `__cplusplus >= 202302L`.** GCC 13 reports 202100L for C++23.
- **`static_assert` or a feature-test macro for the guard.** `#error` stops with one line before any other diagnostic, and no
  feature macro is common to the compilers in the table.
- **The guard's `#if` repeated in each of the seven headers.** One header says it once.
- **A consumer project as the test of the export.** It would need the install and a second configure to prove what T3 reads
  from the target: the property the export copies.
- **Silencing googletest's warning with `-Wno-character-conversion`.** v1.18.0 fixes it.
- **A CMake floor leg in CI** (D6), and **jumping to a newer presets schema** (nothing needs it).

## Not in this plan

- **Adopting a CMake 4 or C++23 feature.** `std::expected`, `std::print`, `std::move_only_function`, deducing `this`, file
  sets, `CMAKE_COMPILE_WARNING_AS_ERROR` and the like are each a design change with its own risk and its own library
  availability on GCC 13.3, so each gets its own issue after this lands.
- **A macOS CI leg** ([#29](https://github.com/tomlin256/cryptocogs/issues/29)), **sanitizers and clang-tidy as options**
  ([#30](https://github.com/tomlin256/cryptocogs/issues/30), [#33](https://github.com/tomlin256/cryptocogs/issues/33),
  [#34](https://github.com/tomlin256/cryptocogs/issues/34)), **the warnings of the test dependencies and `test_ws_client.cpp`**
  ([#26](https://github.com/tomlin256/cryptocogs/issues/26), [#24](https://github.com/tomlin256/cryptocogs/issues/24)), and the
  README's stale test paragraph ([#31](https://github.com/tomlin256/cryptocogs/issues/31)) and its other stale
  statements, found while writing this plan: the quick start omits `zlib1g-dev`, and the dependency table lists
  IXWebSocket v11.4.6 and leaves out three fetched libraries ([#35](https://github.com/tomlin256/cryptocogs/issues/35)).
- **A future CMake that drops compatibility below 3.10.** backward-cpp and CLI11 already warn; when it comes,
  `CMAKE_POLICY_VERSION_MINIMUM` or the dependencies move, in their own issue.
- **MSVC and Windows.** Not built or tested. The guard reads `__cplusplus`, which MSVC reports as 199711L without
  `/Zc:__cplusplus`.
- **Recreating the live `ubuntu-dev` container, and pushing docker-ubuntu.** Both are yours (D9).
- **The older plans.** Their "C++17" and "CMake 3.15" were true when written. `docs/plans.md` is not updated; the issue is
  the index of undone work.
- **The scratch trees on this machine** (`build-bench`, `build-compat`, `build-ubsan`, `build2`, the old tree in `build/`):
  plan platform-build-dirs left them, and so does this. My probe copies under `build/probe/` are deleted when the plan is committed.

## Self-review — risks and assumptions

- **The probes flipped `4f08adb` by hand, not the commits this plan produces.** Steps 3, 5 and 6 repeat them on the real
  commits and Step 8 on the tip. The probe of GCC 15.2 used apt's CMake 4.2.3 in a throwaway aarch64 container, not the
  runner's x86_64; CI is the arbiter, which is why Steps 2, 5 and 6 are pushed on their own.
- **The container half is unmeasured at C++23.** GCC 13.3 is the oldest compiler anyone is likely to use here, and the one
  whose `__cplusplus` is 202100L. Steps 1 and 8 are where it, and Clang 18 on libstdc++, is found out. A failure is a
  finding, not a snag: stop and ask.
- **Green tests show that the code compiles and passes, not that no C++17-to-C++23 semantic change matters.** What a grep
  can find is absent (23 patterns, zero hits), the suite passes on two compilers and libraries, and the benchmark
  comparison in Step 8 looks for a change in speed. I did not compare disassembly.
- **A consumer's own code may not survive C++23.** That is the issue's premise, and this repo cannot test it; the release
  notes say so.
- **The guard rejects a build that passes `-std=c++20` and compiled before.** That is the intent. It also turns a consumer
  that mixes standards, one target at C++17 including headers another has at C++23, into a compile error instead of an
  ODR hazard.
- **The installed package under an older CMake is unmeasured until Step 8.** The exported targets name `cxx_std_23`, which
  CMake 3.19 and older do not know, so they fail inside the targets file with CMake's own message about the feature. The
  README promises only what Step 8 shows.
- **A `FetchContent` consumer on CMake 3 is refused, and an installed-package consumer need not be.** That is deliberate
  (flywheel-dag#57's D4) and the README says so.
- **`SameMajorVersion` makes 1.0.0 a commitment:** from here a change that breaks consumers needs 2.0.
- **An existing tree re-fetches its dependencies once, perhaps.** flywheel-dag saw this when a 4.0 minimum made CMP0168 NEW.
  Here the `...4.2` range set it already on CMake 4.2 or later, so I expect nothing; Step 3 looks.
- **T3 reads `BUILDSYSTEM_TARGETS` of `src/`.** A library defined elsewhere would escape it. Today there is none.
- **CI is proven only by pushing**, at 7 to 9 minutes a run; a red Step 2, 3, 5 or 6 is fixed forward.
- **The two systems share the sources.** Editing a file while the other system builds makes a loaded machine
  time-sensitive; the steps run one system at a time.
- **The bind mount is case-insensitive**, so the Linux build cannot catch a filename-case bug; CI stays the check.
- **This repo is public.** The plan, the docs and the commit bodies describe this library only.

## Outcome

**Released as v1.0.0** (annotated tag on `6d28ad5`, 2026-11-04), CI green on it: CMake 4.4.5, GCC 15.2.0, 503 of 503. The
`include/` diff since v0.6.0 is the guard header and two lines in each of seven headers; `src/CMakeLists.txt` has the six
`cxx_std_23` lines and a comment; googletest moved to v1.18.0 for the tests. #32 is closed.

**Tests.** ctest went from 499 to 503: `test_cmake_floor`, `test_standard_requirement`, `test_standard_guard` and
`test_package_version`. Each was seen red first (the last against hand-made version files, because it passes at 0.6.0 and at
1.0.0), and eighteen hand-made changes to scratch copies, plus one case that must pass, fail them as the plan says:
M1 to M6 (floor), M7 to M9b and the inherited case (requirement), M10 to M13 (guard), M14 to M17 (version policy). The
Mac (Apple Clang 21, CMake 4.4.5) passed in full at each step (500, 501, 502, then 503) with the eight warnings of
#24 and #26 and no ninth; the container (aarch64, CMake 4.4.5: GCC 13.3 release and debug, Clang 18.1.3 on libstdc++ 13)
passed the same counts with the two of #26, up to Step 7; CI (ubuntu-26.04, GCC 15.2.0) was green on every push.

**What the steps found**

- **Step 1.** All of it passed at C++17, Clang 18 on libstdc++ included, so flywheel-dag#58 does not apply here. The three
  development libraries were already in the live container (installed by hand at 11:32, not by this plan; dpkg.log), so the
  by-hand install of D9 was a no-op. Container builds of 3 to 4 seconds are a warm ccache.
- **CI's CMake is 4.4.5 in Step 2's log and 4.4.4 in the later ones**: two runner images behind `ubuntu-latest`.
- **T3 as designed could not hold** (Step 5). `$<TARGET_PROPERTY:…,INTERFACE_COMPILE_FEATURES>` is transitive: it adds
  nlohmann_json's `cxx_std_11` and the features of every linked library, so "no other `cxx_std_NN`" fails on the correct
  tree. The first red run showed it. The check takes the highest standard, which is what CMake compiles a consumer at, and
  the plan's T3 row and mutants were amended (M9b is new, and `kraken` alone at 17 passes on purpose: it inherits 23).
- **T4 compiles too much** (Step 6). The first version compiled 64 units and took 19 to 29 seconds on the Mac; the guard is a
  preprocessor `#error`, so `-E` proves it, and the test takes 4 to 7 seconds (9 on the runner). The glob also finds the guard
  header itself, so it counts 33 headers, not 32.
- **include-cleaner** would call every include of the guard unused, so `cxx_standard.hpp` carries `IWYU pragma: always_keep`.
  An editor that parsed it with a stale compile database showed the `#error`; `clangd --check` with the refreshed
  `build/debug-macos-arm64` reports 0 errors.
- **An existing tree** configured under CMake 4.2 reconfigures under the new floor in 1.2 seconds with no clone: the
  re-fetch flywheel-dag saw does not happen here, because the old range already set CMP0168.
- **The floor is real, and CMake 3 is refused clearly** (Step 8, Kitware's tarballs checked against the releases' SHA-256
  files). CMake 4.0.7 configures with no CMake warning (the "< 3.10" deprecations of backward-cpp and CLI11 are newer than
  4.0), builds, and passes 503 of 503. CMake 3.31.12 stops the library as the top-level project, through the preset and
  through a `FetchContent` consumer, with `CMake 4.0 or higher is required. You are running version 3.31.12`. A consumer
  under 3.31.12 configured, built and ran against a package installed by 4.4.5, compiled as `-std=gnu++2b` with no
  `CMAKE_CXX_STANDARD` of its own, which shows `cxx_std_23` reaches it. **CMake 3.20.6 cannot do it on Apple Clang 21**
  ("The compiler feature cxx_std_23 is not known to CXX compiler"), so the README promises only what was measured.
- **The generated files.** Of 671 files CMake generates, the only one that differs between HEAD and Step 3 is
  `tests/CTestTestfile.cmake` (the new test and one backtrace line).

**Deviations from the plan as approved**

- Step 8 is partly done. Not run: AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer over the suite in the
  container, and a container run at the release tip. The speed comparison (C++17 against C++23, the benchmark harness
  of plans kraken-performance-profiling, kraken-reserve-vector-growth and remaining-adapters-reserve-vector-growth) was
  started and stopped at Rob's instruction ("skip the performance testing … we can test more
  after it's pushed"); the two trees built, and no number was taken. All three follow the push.
- Step 8 made a commit after all: the README sentence about an installed package, and the `v1.0.0` and `find_package(… 1)`
  snippets (`62dd391`), pushed with the release.
- T3, T4 and the mutant numbers were amended as above.

**An incident, mine.** While Step 8 ran, my scratch copies filled the host's disk: I copied the checkout with
`rsync --exclude build`, which skips only an entry named `build` and not the stale `build-bench`, `build-compat`,
`build-ubsan` and `build2` beside it, so each of some fifteen copies was 750 MB (9.8 GB in all). The disk reached 100%
(124 MiB free) at about 14:20. Docker Desktop's backend began shutting its engine down at 14:22 and has not come back (its
log ends at "setting engine linux/virtualization-framework state stopped -> stopping"), the `ubuntu-dev` container went with
it, and an AddressSanitizer run in the container died with the full disk. I deleted the copies (13 GiB are free now), did
not force-restart Docker, and on Rob's instruction ("Docker is dead so skip any use of it") used no Docker after that. The host still has little room, which the stale trees in `build/`
(about 2.8 GB) could ease.

**Left for later**

- Rob restarts Docker Desktop (`./start.sh --detach` does it, and recreates `ubuntu-dev` from the image that docker-ubuntu#3
  fixed). Then: the sanitizer run, a container run of the tip, and the benchmark comparison, in that order.
- docker-ubuntu: #3 is still open (closing it was refused by the session's permission classifier), and one docs commit
  (plan step-5-binance-rest-public's Outcome after the push) is not pushed.
- [#35](https://github.com/tomlin256/cryptocogs/issues/35), the README's other stale statements, and
  [#38](https://github.com/tomlin256/cryptocogs/issues/38), the `performance-enum-size` and unused-include findings.
- Not done: a GitHub release object for v1.0.0 (D7); "Latest release" on GitHub still reads v0.0.3.

## Progress

| Step | Status | Notes |
|---|---|---|
| 0 — Approval | Done (2026-11-04) | Approved as written: D1 to D9 stood. |
| 1 — Start from green | Done | At `3ab1e32`: Mac 499 of 499 (configure 26 s, build 18 s, ctest 6 s), the eight warnings of #24 and #26; container `release-linux-arm64` and `debug-linux-arm64` (GCC 13.3.0) and a hand-made `clang-linux-arm64` (Clang 18.1.3) 499 of 499, the two of #26. |
| 2 — CI shows its tools | Done | `a26c0e3`; run 41838340297 green: CMake 4.4.5, GCC 15.2.0, 499 of 499. |
| 3 — Require CMake 4 | Done | `cac0080`; T1 red then green, M1 to M6 as stated; 500 of 500 on the Mac and in the container; CI run 41839716936 green. |
| 4 — googletest v1.18.0 | Done | `1e3d0e2`; 500 of 500, the Mac's eight warnings and no ninth; CI run 41842073731 green. |
| 5 — Require C++23 | Done | `3931104`; T3 red then green (design amended), M7 to M9b as stated; 501 of 501 on the Mac and in the container; CI run 41844066017 green on GCC 15.2 (x86_64). |
| 6 — Refuse to compile below C++23 | Done | `d371ae4`; T4 red (32 headers at both standards) then green, M10 to M13 as stated, M11 stops GCC 13.3's `-std=gnu++23` build at the `#error`; 502 of 502; CI run 41845554569 green. |
| 7 — Hold what `find_package` accepts | Done | `a0b8b8a`; green at 0.6.0, seen red against M14 to M17; 503 of 503; CI run 41847319160 green. |
| 8 — Verify, on the tip | Partly done | CMake 4.0.7, 3.31.12 and 3.20.6 as above; README commit `62dd391`. Not run: the container sanitizers and tip, and the benchmarks (Docker down; speed skipped on request). |
| 9 — Release v1.0.0 | Done | `6d28ad5`, tag `v1.0.0`, pushed; the installed `cryptocogsConfigVersion.cmake` says 1.0.0; CI run 41849773159 green; #32 closed. |
| 10 — Close the plan | Done | This commit. |
