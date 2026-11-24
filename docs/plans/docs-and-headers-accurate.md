# Docs and headers are accurate: the six issues of milestone 1

**Status:** Approved as written (2026-11-24), so D1 to D5 stand at their defaults. Steps 0, 1 and 2 are done; Step 3 is next.

Closes [#31](https://github.com/tomlin256/cryptocogs/issues/31), [#35](https://github.com/tomlin256/cryptocogs/issues/35),
[#53](https://github.com/tomlin256/cryptocogs/issues/53), [#62](https://github.com/tomlin256/cryptocogs/issues/62),
[#65](https://github.com/tomlin256/cryptocogs/issues/65) and the second half of
[#38](https://github.com/tomlin256/cryptocogs/issues/38) (its first half shipped in v2.0.0).

**Milestone:** [Docs and headers are accurate](https://github.com/tomlin256/cryptocogs/milestone/1): "README, the add-an-exchange
playbook, example comments, plan-doc links and public headers say what the code and the build do today."

**Follows:** [plan unnumbered-plan-docs](unnumbered-plan-docs.md), whose Step 3 left 8 GitHub texts for #65.
**Not here:** README sections for Coinbase and Crypto.com ([#71](https://github.com/tomlin256/cryptocogs/issues/71)), and the other
includes that the sweep below reports in public headers ([#72](https://github.com/tomlin256/cryptocogs/issues/72)); a comment in
`common/rest.hpp` that the playbook audit found false ([#73](https://github.com/tomlin256/cryptocogs/issues/73)); the description of the
`CRYPTOCOGS_BUILD_BENCHMARKS` option, which says Kraken only ([#74](https://github.com/tomlin256/cryptocogs/issues/74)); a rewrite of the
names that 8 old texts carry (D1); a release (D5).

## Problem

Six issues, one cause: text that was true when it was written and was not changed when the tree was. Measured at `3096404`.

| Issue | It says | Today |
|---|---|---|
| #31 | README's *Running the tests* says 312 tests in twelve executables, tabulates twelve binaries, and gives 147 and 176 for two single-venue builds | `ctest -N` lists 598 in `debug-macos-arm64`; `tests/unit/CMakeLists.txt` makes 31 executables; there are four venue flags, not two |
| #35 | The quick start omits `zlib1g-dev`; the table lists IXWebSocket v11.4.6; toml++, CLI11, backward-cpp and Google Benchmark are not listed | All three hold. The pin is **v12.1.0**, not the v12.0.0 of the issue: #36 moved it while the issue was open |
| #38 | `<cstdio>` and `<type_traits>` are unused in `common/ws.hpp` | Confirmed (below). The other half, the 14 enums, shipped in v2.0.0 |
| #53 | `kraken_example.cpp:11` names `kraken_types.hpp`, removed in v0.1.1 | Holds, and `public_ws.cpp:10` names `kraken_ws_api.hpp` for the same reason |
| #62 | The playbook has line-number citations that drifted, and §4 mentions a removed compat shim | Both hold; the audit found three more false statements and one omission (below) |
| #65 | 12 links in 8 GitHub texts point at plan files that were renamed | Holds: 2 issue bodies and 6 comments; the four plans exist under their new names |

### What I checked (2026-11-23, `3096404`)

**README.** Statements about the build or the venue set that are false today, beyond the two issues:
- the intro says Kraken and Binance and names two venue flags (L3 to L11); the option table (L335 to L343) has two venue flags of four;
- "three peer static libraries" (L329) and "the four static libraries" (L380): `cmake --install` of `release-macos-arm64` into a scratch
  prefix holds **six** (`exchange_common`, `exchange_http` and the four venues) and the public headers of the four venues and `common`;
- the link snippets (L360, L385) say "and/or `cryptocogs::binance`".

The README has one line about Coinbase and Crypto.com (L499) and no section for either. That is an omission, not a false statement, and
it is a larger piece of writing than this plan: see D2.

**The dependencies, as the build fetches them** (`CMakeLists.txt`, `tests/CMakeLists.txt`):

| Library | Pin | Fetched |
|---|---|---|
| nlohmann/json | v3.12.0 | always; its headers are vendored into the install |
| toml++ | v3.4.0 | always; reads the credentials file, build-time only |
| IXWebSocket | v12.1.0 | with `CRYPTOCOGS_BUILD_TESTS`; examples and tests only |
| Google Test | v1.18.0 | with `CRYPTOCOGS_BUILD_TESTS` |
| spdlog, CLI11, backward-cpp | v1.17.0, v2.4.2, v1.6 | with `CRYPTOCOGS_BUILD_TESTS`; examples |
| Google Benchmark | v1.9.5 | with `CRYPTOCOGS_BUILD_BENCHMARKS` |

OpenSSL, libcurl (7.80.0 or later, held by `test_curl_floor`) and zlib are the system's.

**The playbook** (`docs/agent-add-exchange.md`), claim by claim against the tree. The 13 commit hashes it cites all resolve and are on
`main`. What does not hold:
1. Line citations, 7 of them: `rest.hpp:32` is `:35`, `rest.hpp:57` is `:60`, `rest_api.hpp:55` is `:51`, `rest_api.hpp:119` is `:83`,
   `rest_api.hpp:71` is `:62`; `rest.hpp:77,87` and `types.hpp:47` are right today and rot the same way.
2. §4 says a venue that "needs a compat shim" adds a dependency rule; the shim went in v0.1.1.
3. §4 cites `TickPrice` in `exchange/kraken/types.hpp`; it is `exchange/common/tick_price.hpp`.
4. §3 *Test patterns* names `make_test_client(fn)`; that is Kraken's. Binance, Coinbase and Crypto.com have
   `make_<name>_test_client(fn)`.
5. §2 says private requests implement `build(const Credentials&)`. Kraken's and Crypto.com's do; Binance's and Coinbase's `build()`
   takes none and the client signs. `exchange/common/rest.hpp` says the base mandates no signature.
6. §3 item 8 does not say that the root `CMakeLists.txt` installs `include/exchange/<name>` behind each venue's flag. A venue that
   leaves that out builds and passes every test, and ships no headers.

**Unused includes.** `clang-include-cleaner` (LLVM 23, the library that clangd's unused-include hint is built on) run over the 34
public `.hpp` files (it cannot process `ix_ws_connection.hpp`, which needs ixwebsocket on its include path, so 33 were read): for
`common/ws.hpp` it lists `<cstdio>` and `<type_traits>` (and `nlohmann/json.hpp`, a choice: see Not here).
`<cstdio>` is used only by `src/exchange/common/ws.cpp`, which includes it itself. `std::is_base_of_v` is used once near it, in
`ws_client.inl`, which reaches `<type_traits>` through `ws_client.hpp` and so through `ws.hpp`: that include must become
`ws_client.hpp`'s own. `clangd --check` prints no include diagnostics, so the hint itself was not reproduced; the tool is the proof.
The same sweep lists three more unused includes (`<utility>` in `binance/rest_client.hpp` and `coinbase/rest_client.hpp`; `kraken/ws_api.hpp` in
`kraken/ws_client.hpp`) and `json_fwd.hpp` for `json.hpp` in 14 headers, and 43 includes that a header uses and reaches only through
another one. Taking an include out of a public header can break a project that took the type from it, so those wait for their own
issue (#72).

**File names that exist nowhere.** A sweep for `*.hpp`, `*.inl` and `*.cpp` names in `include/`, `src/`, `tests/examples`,
`tests/unit`, `tests/benchmarks`, the README and the two docs (157 files, 4.5 s in the shell) finds 9: the two stale comments, and 7
placeholders in the playbook (`<name>_auth.cpp`). Nothing else.

**#65.** The four plans the 12 links need exist as `ci-cache.md`, `tsan-races-in-the-tests.md`, `clang-tidy-opt-in-test-step.md` and
`warnings-as-errors.md`. `check_public_leaks.py --tree` over the 8 texts exits 1 with 28 findings, every one a name the text already
carries; the names were left as they are (Rob's decision of 2026-11-07), so the audit cannot be the gate for an edit that changes only a
link.

## Decisions to confirm

Defaults stand if you approve the plan as written.

- **D1. The 8 texts of #65.** *Default:* edit only the 12 links, in the 6 comments and 2 issue bodies, to the new names. The names
  stay. The gate becomes "no new finding": the audit is run over the 8 texts before and after and must print the same 28 findings;
  each edit is read back and compared with what was sent. GitHub keeps each text's earlier version, as it did for the 50 items of
  plan unnumbered-plan-docs. *Alternative:* leave the 12 links dead and close #65 with a comment that says so.
- **D2. README scope.** *Default:* #31 and #35 as written, and every other statement about the build or the venue set that is false
  today (the list above): four venues in the intro, option table and snippets; six libraries, not three or four; the dependency list.
  No new section for Coinbase or Crypto.com: a new issue, in this milestone because it serves its goal, so the milestone stays open for
  it. *Alternative:* #31 and #35 only (the other falsehoods become issues). *Alternative:* write the two sections here (a plan of
  its own: each needs its examples run and its credentials described).
- **D3. A check that holds the dependency table.** *Default:* `test_readme_dependencies` and `test_readme_dependencies_fixtures` in
  `tests/doc_checks/`: every `FetchContent_Declare` in the two files that `test_ci_cache_key` holds the cache key to
  (`CMakeLists.txt`, `tests/CMakeLists.txt`: 8 of them) has a row in the README's *Dependencies* table that links its repository and
  says its `GIT_TAG`, and no row names a library that is not fetched. A pin that moves then turns a test red until the table is
  edited, and the table cannot drift unnoticed. Cost, sized on `test_curl_floor`
  (94 lines and 136 of fixtures): about 100 lines and 120 of fixtures, 2 registrations, a clause of about 120 bytes in `CLAUDE.md`
  (19643 bytes of its 21000); the run time is measured in Step 2. *Alternative:* drop the version column, name the two files that
  pin the versions, and add no check.
- **D4. A check for file names in comments that exist nowhere.** *Default:* no. The sweep found two instances, both from one event (the
  shim's removal), and a check needs an allow-list for the playbook's placeholders and for third-party headers. *Alternative:* yes,
  `test_comment_file_names`, with its fixtures, in Step 1.
- **D5. No release.** The only change a downstream project can see is two includes fewer in `common/ws.hpp`: code that took `<cstdio>`
  or `<type_traits>` from it without including them stops compiling. *Default:* it ships with the next release. *Alternative:* v2.2.1
  after Step 3.

## Steps

Commits go straight to `main` and are pushed after each checkpoint (the README and the headers start CI runs; they are looked at in
passing, never waited for). No commit message carries a closing keyword: each issue is closed by hand after its closing comment, and
every text posted is audited with `check_public_leaks.py --tree` first. A checkout can hold another session's files (`.DS_Store` is
untracked now): commits are made by explicit path.

### Step 0 — Approval

You approve or edit. Then: commit this plan (`docs(plan): docs-and-headers-accurate approved as written`, status line changed), push
(documentation only: no CI run), comment on each of the six issues with the plan's URL, and open two issues, each with its kind:
*README: sections for the Coinbase and Crypto.com adapters* (`documentation`, in this milestone, which stays open for it), and *Public
headers: unused includes that `clang-include-cleaner` reports* (`enhancement`, listing the three of the sweep and the 14 `json_fwd`
suggestions; no milestone, because it concerns hygiene and not accuracy). *Done when:* the six issues and the plan link each other and
the two issues exist.

### Step 1 — #53 and #62: example comments and the playbook

*Start from green:* `debug-macos-arm64`, full build and ctest; the count is recorded, and anything red stops the plan there.

*Change.*
- `tests/examples/kraken_example.cpp:11` says `exchange/kraken/types.hpp`; `tests/examples/public_ws.cpp:10` says
  `exchange/kraken/ws_api.hpp`.
- The playbook: no line citation (each becomes the symbol, with its file, as the other citations already do); §4's shim sentence
  goes; `TickPrice` is cited at `exchange/common/tick_price.hpp`; the test pattern names `make_<name>_test_client(fn)` and says
  Kraken's has no venue in its name; §2 says what the common base says about `build` and shows both ways; item 8 gains the install
  rule (and, in one line, the two places that say "all venues off" and the benchmark block). The audit is redone claim by claim after
  the edit and every claim is recorded in the *Outcome*, so that a claim I did not look at is visible as one.
- If D4 is yes: the check and its fixtures here.

*Tests.* None is new (comments and prose). `test_kraken_example` holds the frames the demo prints, which no comment changes;
`test_plan_docs` reads the playbook for `docs/plans/` paths. A hand-made change per edit, in a scratch copy (`rsync -a --exclude
'build*' --exclude .git`, deleted once recorded): the old text asserted to occur once, the new one grepped for.

*Checkpoint.* Full build and ctest of `debug-macos-arm64`, no warning. Commit, `git fetch`, push, and close #53 and #62 with their
comments. *Done when:* the sweep of *What I checked* finds only the playbook's 7 placeholders; no `.hpp:`, `.inl:` or `.cpp:` followed by
digits is left in the playbook (`grep -nE '\.(hpp|inl|cpp):[0-9]'` prints nothing); and the build and the ctest are as green as at the start.

### Step 2 — #31 and #35: the README

*Start from green* as in Step 1.

*The gap, shown (D3 yes):* the check is written first and run over the unedited README. Expected: red, naming IXWebSocket v11.4.6
against v12.1.0 and the four libraries the table omits; then green after the edit.

*Change.*
- The quick start's `apt` line names `libssl-dev libcurl4-openssl-dev zlib1g-dev`, as `CLAUDE.md` and the three workflows do.
- The fetched-dependencies sentence and the table (library, version, what needs it): one row for each of the eight fetched libraries,
  and the system's OpenSSL, libcurl and zlib; the libcurl row still says "libcurl 7.80.0 or later" (`test_curl_floor`).
- *Running the tests* states no count and lists no binary: it says what runs (the unit tests per venue, the exchange-agnostic ones, the
  `cmake -P` checks of the repository, clang-tidy when asked for), that `ctest --preset <tree> -N` lists them, that each venue flag
  drops only its own suite, and keeps the clang-tidy paragraph. A number or a binary name that is not stated cannot go stale.
- D2: the intro, option table, "three peer static libraries" and "the four static libraries" (six, with every flag on), the snippets.
- D3: the check, its fixtures (the table agrees; a pin is stale; a library has no row; a row has no library; a declaration is only in a
  comment; the table is missing; a row has no version), `tests/CMakeLists.txt` registrations like `test_curl_floor`'s, and the one
  clause in `CLAUDE.md`.

*Tests.* D3: `test_readme_dependencies` over the tree, and `test_readme_dependencies_fixtures` over small checkouts that agree and
disagree; six hand-made changes in a scratch copy (a pin bumped in `CMakeLists.txt`, the same in `tests/CMakeLists.txt`, a row
deleted, a row added, a version edited in the README, the table heading renamed), each of which must turn a test red. Existing tests
that read the README: `test_cmake_floor`, `test_curl_floor`; and `test_claude_md_budget` for the clause.

*Checkpoint.* Full build and ctest of `debug-macos-arm64`; in `ubuntu-dev`, `debug-linux-arm64` reconfigured and the new tests run
(a CMake script, but the second platform costs a minute and CI runs it there). Commit, push, close #31 and #35 with their comments.
*Done when:* the check red on the unedited README and green on the edited one; the six changes red; every test green; the README holds no
number of tests, and every fetched library with its pin.

### Step 3 — #38: `common/ws.hpp`

*Start from green* as in Step 1, with the clang-tidy gate on for this step (`-DCRYPTOCOGS_CLANG_TIDY=clang-tidy`, off again after).

*The gap, shown:* `clang-include-cleaner` over `ws.hpp` at `3096404` lists `<cstdio>` and `<type_traits>` (recorded above).

*Change.* `<cstdio>` and `<type_traits>` leave `ws.hpp`; `<type_traits>` joins `ws_client.hpp`, which is the header whose `.inl`
uses it. A file that fails to build because it took either from `ws.hpp` gets its own include: that is the fix, and not a reason to
keep one in `ws.hpp`.

*Tests.* No new test: an include changes no behaviour and no layout, and the unit that could notice a missing one is the build of
every unit that includes the header, on both standard libraries. `clang-include-cleaner` is run again: it must list neither
include for `ws.hpp`, and nothing new for `ws_client.hpp`.

*Checkpoint.* As in plan curl-prereq-refusal-test: `debug-macos-arm64` full build and ctest with the gate on; in `ubuntu-dev`
`debug-linux-arm64` built and run in full and `release-`, `asan-` and `tsan-linux-arm64` built (GCC 13.3); the three configurations
built with GCC 15.2 in the throwaway container of `docs/dev-recipes.md` (libstdc++ 15 is where a missing include shows first).
Commit, push, close #38 with its comment (the enum half: v2.0.0; this half: the commit). *Done when:* the tool lists neither include,
every build above has no warning and no error, and every run is green.

### Step 4 — #65: the 8 held texts (D1)

*Change.* For each of the 8 texts: fetch it to a scratch file; replace the old file name by the new one in each of its links (4
names, 12 places) and nothing else; `diff` against the original shows only those lines; the audit run over the 8 edited files prints the
same 28 findings as before; each new path is checked with `git cat-file -e main:docs/plans/<name>.md`; the text is posted
(`gh issue edit N --body-file` for the two bodies, `gh api -X PATCH repos/tomlin256/cryptocogs/issues/comments/ID -F body=@file`
for the six comments, ids 6637049785, 6638191537, 6640307854, 6657532294, 6658667473 and 6664853187); each is read back and compared
with what was sent. A difference in the findings, or any text that does not read back equal, stops the step. Then a comment on #65
(audited) with the table of the 12 links, and #65 closed.

*Tests.* None in the repo: it edits no file of it. The audit before and after, and the read-back, are the measure.

*Done when:* the 12 links resolve (each is fetched once), the audit's findings are unchanged, and #65 is closed.

### Step 5 — Record

This plan's *Outcome*: commits, the figures measured above and again at each checkpoint, the playbook's claims and what each was
found to be, and what differed from the plan. Status line to Done. Full ctest (`test_plan_docs` reads the plans). Commit, `git fetch`,
push. *Done when:* pushed, and the *Outcome* holds measured figures only. The milestone is left open if the issue of Step 0 is.

### Step 6 — After the push

Not blocking. `asan-macos-arm64` and `tsan-macos-arm64`, full suites, with the options in `CLAUDE.md`. The push's CI runs are read
once they have finished, and not waited for. A finding gets its own issue. *Done when* the results are in the *Outcome*.

## Designs I rejected

- **Six plans, one per issue.** Five of the six are an edit and a grep; the plan is the same each time.
- **Scrub the names in the 8 texts.** It reverses the decision of 2026-11-07; D1 changes the links and nothing else.
- **`misc-include-cleaner` in `.clang-tidy`.** It reports 14 `json_fwd.hpp` suggestions and every `.inl` include as unused: a gate
  that is red on choices, and `.clang-tidy` is the only list of checks.
- **Keep the version column and trust the editor.** The pin moved on from v12.0.0 to v12.1.0 while #35 was open (#36).
- **Write Coinbase and Crypto.com sections here.** Each needs its examples run and its credentials explained: a documentation project
  that would hold six small fixes to its pace.

## Self-review — risks and assumptions

- **A README edit starts CI.** Only `docs/**` and `CLAUDE.md` are in `paths-ignore`: `README.md` is read by `test_cmake_floor` and
  `test_curl_floor` and must not be. Two runs per push; they are read, not waited for.
- **The table parser is tied to the table's shape.** A table that is reshaped fails with the row at fault; the fixtures include a
  missing table and a row without a version, so that "no table" cannot pass as "no mismatch".
- **A removed include is source-visible.** The grep finds no use of the two outside `ws.cpp` and `ws_client.inl`, and the builds of
  Step 3 are the proof; nothing outside this tree is known. D5 says what the change is and when it ships.
- **`clang-include-cleaner` is the proof, not clangd.** The tool and the hint share a library, but I did not see the hint. If a hint
  stays after Step 3, it is for a different reason and gets its own issue.
- **The playbook audit can miss a claim.** The *Outcome* lists each claim checked, so a reader sees what was not.
- **GitHub keeps the earlier version of an edited text.** D1 changes no name, so it publishes nothing that was not already there.
  The edits go through the same `gh` that edited the other 50.
- **This plan names no old numbered path with its directory in front,** because `test_plan_docs` reads every file of the checkout
  for `docs/plans/<x>` and the old names are gone.
- **Hash and date assumptions:** every figure is at `3096404`, 2026-11-23, Apple clang 21 and LLVM 23; each is measured again in
  its step and the *Outcome* records the new figure, not this one.

## Outcome

Steps 0, 1 and 2 are done (2026-11-24). Each later step adds to this section when it is done, and Step 5 checks it. Counts and figures are
measured.

### Commits

| Step | Commit |
|---|---|
| 0 | `15b7130` `docs(plan): docs-and-headers-accurate approved as written`: pushed, no CI run started (documentation only); the six issues commented with the plan's URL; `902ea22` links the two new issues |
| 1 | `569895c` `docs: the playbook and two example comments name what the tree holds (plan docs-and-headers-accurate, step 1)`: pushed; the two CI runs it started (`cmake-single-platform.yml`, `clang-tidy.yml`) finished `success`; #53 and #62 closed with their comments |
| 2 | the commit that holds this record |

### Step 0

Issues opened: [#71](https://github.com/tomlin256/cryptocogs/issues/71) *README: sections for the Coinbase and Crypto.com adapters*
(`documentation`, milestone *Docs and headers are accurate*) and [#72](https://github.com/tomlin256/cryptocogs/issues/72) *Public
headers: unused includes that `clang-include-cleaner` reports* (`enhancement`, no milestone: it concerns hygiene and not accuracy). The
repository has kind labels only, so neither has an area. Comments with the plan's URL: #31, #35, #38, #53, #62 and #65. Every text
passed `check_public_leaks.py --tree` (exit 0) before it was posted.

What differed from the plan: the sweep of public headers read 33 of the 34 (the tool cannot process `ix_ws_connection.hpp`), and it
also reports 43 includes that a header uses and gets only through another one; both are in #72 and in *What I checked* above.

### Step 1 — #53 and #62

*Start from green.* `debug-macos-arm64`, Apple clang 21.0.0, at `902ea22`: the tree was up to date and built with no warning;
**598 of 598** (27.9 s).

*The change.* `tests/examples/kraken_example.cpp:11` says `exchange/kraken/types.hpp` and `tests/examples/public_ws.cpp:10` says
`exchange/kraken/ws_api.hpp`. In `docs/agent-add-exchange.md`:
- the 7 line citations are gone (`rest.hpp:77,87`, `rest.hpp:32`, `rest.hpp:57`, `rest_api.hpp:55`, `:119`, `:71` and `types.hpp:47`;
  each is the file or the symbol now): `grep -nE '\.(hpp|inl|cpp):[0-9]'` prints nothing;
- §4 no longer mentions a compat shim; the flag bullet says it is added wherever the root `CMakeLists.txt` lists every venue flag;
- `TickPrice` is cited at `exchange/common/tick_price.hpp`;
- the REST test pattern names `make_<name>_test_client(fn)` (`make_binance_test_client`, `make_coinbase_test_client` and
  `make_cryptocom_test_client` take a function from `HttpRequest` to `HttpResponse`; Kraken's `make_test_client` returns the body as a
  string);
- §2 says that `TypedPrivateRequest<R>` mandates no `build` signature, and gives both shapes: `build()` with the client signing
  (Binance, Coinbase), `build(const <Name>Credentials&)` (Kraken, Crypto.com);
- item 8 says what the venue's flag touches: the option, the two "all venues OFF" conditions, the header install rule and the
  library's entry in the install list of `src/CMakeLists.txt`, and says that a benchmark is optional. No test and no workflow reads
  the install rules (`grep` over `tests/`, `cmake/` and `.github/` for `--install`, `install(DIRECTORY`, `install(TARGETS` and
  `CMAKE_INSTALL_PREFIX` finds nothing), so a venue that leaves either rule out builds and passes every test.

*The audit, claim by claim.* 180 machine checks over the playbook after the edit (a script in the scratch directory, not in the
repository), each a path that exists, a symbol or a line that is in the file it names, or a commit that is on `main`: introduction
14, §0 4, §1 (fixture names) 4, §2 REST 27, §2 auth 9, §2 enums 12, §2 WS 34, §3 paths 30, §3 test patterns 8, §4 4, the 13 commits
cited 13, and the 21 claims of the text added here 21. Before the edit the same script, run over the unedited playbook, listed 5 FAILs;
none was a falsehood of the playbook, each was a pattern of mine that was too narrow (Kraken's `RestResponse<T>` and
`parse_rest_response<T>` are in `kraken/types.hpp`, not `rest_api.hpp`; the client keys a request by `std::to_string(id)` after
`req.req_id = id`; the Binance WS factory's arguments are on three lines; `GTest::gtest_main` is linked in
`tests/unit/CMakeLists.txt`). The two checks added for the new text that failed at first had the same cause (`sign` takes
`exchange::rest::HttpRequest&`). With these seven patterns corrected the script prints 180 OK and no FAIL. Each cited commit was also
read with `git show --stat`: all 13 are on `main`, and each touches a header, a test or a CMake file that the playbook cites it for.
The *Reference* column of §3 names commits to read for the pattern, not one commit per file: `src/binance/auth.cpp`, and the
bodies in `src/binance/ws_api.cpp` and `ws_streams.cpp`, came later with plan declarative-headers (`5430030`, `c1a9479`), and the
playbook says so for the WS bodies. That is left as it is.

What the audit cannot check, and so was read and left as it is: the instructions to the reader (the per-item loop, the commit
message form `feat: <exchange> — <what>`, what to ask the user in §1), "run both CLI examples against the live exchange" (no network
here), and §5 item 4's command (`cmake -S . -B build/decoupled-macos-arm64 -DCRYPTOCOGS_BUILD_<NAME>=OFF`: read, not run).

*The sweep.* Over `include/`, `src/`, `tests/examples`, `tests/unit`, `tests/benchmarks`, the README and the two docs, with the
third-party headers left out: 10 names, all of them placeholders in the playbook (7 `.cpp` and 3 `_example_json.hpp` suffixes, as in
`<name>_auth.cpp`), and no other. The two stale comments are gone.

*Checkpoint.* `debug-macos-arm64`: 2 objects built (the two examples), no warning; **598 of 598**, `test_kraken_example`,
`test_plan_docs` and `test_plan_docs_fixtures` among them. The three edited files and the text of the new issue passed
`check_public_leaks.py --tree` (exit 0).

*What differed from the plan.*
- The sweep prints 10 names, not 7: the plan counted the `.cpp` placeholders and left out the three `_example_json.hpp` suffixes, which
  are the same kind.
- The install rule has two halves, not one: the playbook said neither. Both are in item 8.
- Two statements outside the plan's list were corrected: `RestResponse<T>` is `{ ok, result, errors }` in that order, and the
  `BinancePingRequest` snippet has `override`, as the header does.
- No scratch copy was made for the hand-made change of each edit. The edits are the change itself, so there is nothing to revert; the
  Edit tool refuses an old text that does not occur exactly once, and each new text was grepped for once (the `grep` of the line
  citations and the 21 checks of the new text above).
- The audit found a header comment that says what the playbook said: `common/rest.hpp` lists `build(const Auth&)` for a private
  request, a few lines above the comment that says the base mandates no signature. It is a public header and not in this plan's list:
  [#73](https://github.com/tomlin256/cryptocogs/issues/73) (`documentation`, in this milestone).

### Step 2 — #31 and #35

*Start from green.* Not measured again at the start of this step: `569895c` is the Step 1 checkpoint (598 of 598, no warning) and the
two CI runs it started finished `success` (45360953746 and 45360953853); the working tree held only this step's files.

*The gap, shown.* `check_readme_dependencies.cmake` was written first and run over the unedited README: exit 1 and five problems,
"the README's row for IXWebSocket says v11.4.6, and CMakeLists.txt fetches ixwebsocket at v12.1.0", and no row for toml++, Google
Benchmark, backward-cpp or CLI11. Over the edited README it exits 0 and says it held the table to 8 `FetchContent_Declare` calls.

*The change.*
- Quick start: `zlib1g-dev` in the `apt` line; the sentence after it names no library and links the table.
- *Dependencies*: `Library | Version | Needed by`, one row for each of the 8 fetched libraries (nlohmann/json v3.12.0, toml++ v3.4.0,
  IXWebSocket v12.1.0, Google Test v1.18.0, spdlog v1.17.0, CLI11 v2.4.2, backward-cpp v1.6, Google Benchmark v1.9.5) and for the
  system's OpenSSL, libcurl ("libcurl 7.80.0 or later" is kept: `test_curl_floor` reads it) and zlib. A sentence above the table says
  where the pins are and that `test_readme_dependencies` holds the table to them.
- *Running the tests*: no count and no binary table (the old text said 312 tests in twelve executables, and 147 and 176 for the two
  single-venue trees). It says what runs, that `ctest --preset <tree> -N` lists it, and that each venue flag drops only its own unit tests.
  `grep -nE '[0-9]+ (unit |gtest )?tests?'` over the README prints nothing.
- D2: the intro names the four venues and their four flags; the option table has the `CRYPTOCOGS_BUILD_COINBASE` and
  `CRYPTOCOGS_BUILD_CRYPTOCOM` rows; "three peer static libraries" is "up to six" (it left out `exchange_http`); the flags paragraph says
  that each flag drops only its own adapter, tests and examples; both link snippets name the four adapters; "installs the four static
  libraries" is "the static libraries (six, with every exchange flag on)".
- D3: `tests/doc_checks/check_readme_dependencies.cmake` and `check_readme_dependencies_fixtures.cmake`, registered in
  `tests/CMakeLists.txt` as `test_readme_dependencies` and `test_readme_dependencies_fixtures` (top level only, as `test_curl_floor` is),
  and one clause in `CLAUDE.md` (19643 to 19717 bytes of 21000).

*The check.* It reads the `CMakeLists.txt`, `*.cmake` and `*.cmake.in` of the checkout (the files that `test_ci_cache_key` hashes), takes
their `FetchContent_Declare` calls (any case; what follows a `#` is not read), and holds the README's `## Dependencies` table to them:
each declaration has a row whose first cell links its repository (a trailing `.git` is not part of the address), a row's version is its
declaration's `GIT_TAG`, and a row that is not a system library links a repository that something fetches. A declaration without a
`GIT_REPOSITORY`, without a literal `GIT_TAG`, or whose repository another declaration names is refused, and so is a checkout that
declares nothing. One run lists every problem.

*The fixtures.* 21 small checkouts made at run time, 3 accepted and 18 refused, each refusal by a message that names the row or the
declaration at fault. Accepted: a table that agrees with declarations in two files (system rows, a `;` in a cell, a section after the
table); declarations that are only in comments; a commit hash as the tag. Refused: a stale pin in either file; a bumped pin; a call in
lower case; a row with no version, with one cell, with no link, or with no library behind it; a library with no row; no table, or the
heading renamed, or the table in another section; a declaration with no tag, no repository, a variable for a tag, or a repository
another declaration names; a checkout with no declaration; and one run that names four problems.

*Hand-made changes.* The six of the plan, each in a scratch copy of the whole repository (`rsync` without `build*`; each old text
asserted to occur exactly once; the copy diffed against the repository to show that only that change is in it), and each turned the
check red with the message that names what was changed; the unchanged copy exits 0:

| Change | The message |
|---|---|
| `ixwebsocket` pinned at v12.2.0 in `CMakeLists.txt` | the README's row for IXWebSocket says v12.1.0, and CMakeLists.txt fetches ixwebsocket at v12.2.0 |
| `spdlog` pinned at v1.18.0 in `tests/CMakeLists.txt` | the README's row for spdlog says v1.17.0, and tests/CMakeLists.txt fetches spdlog at v1.18.0 |
| the CLI11 row deleted | the README has no row for https://github.com/CLIUtils/CLI11, which tests/CMakeLists.txt fetches as CLI11 at v2.4.2 |
| a row for fmt added | the README's row for fmt links https://github.com/fmtlib/fmt, which no FetchContent_Declare fetches |
| Google Test's version edited to v1.17.0 in the README | the README's row for Google Test says v1.17.0, and CMakeLists.txt fetches googletest at v1.18.0 |
| the heading renamed `## Requirements` | README.md has no "## Dependencies" section, so nothing holds the libraries that CMake fetches to it |

The fixtures are a check of the check, so 18 more changes were made, each to a scratch copy of `check_readme_dependencies.cmake`, and
each turned `test_readme_dependencies_fixtures` red on the case that holds it: the version not compared; a declaration with no row not
reported; a row with no declaration not reported; comments read as code; system rows held to a declaration; a missing section not an
error; a variable accepted as a tag; a repository declared twice accepted; the call name read in one case; the section not ending at the
next heading; a checkout that declares nothing passing; the header row read as a library; a row with no link skipped; a row with no
version accepted; a row of one cell skipped; a declaration with no tag and one with no repository accepted; a trailing `.git` kept in
the address. One of the 18 first failed to apply: its old text had the wrong indentation, the once-only assertion refused it, and the
corrected text turned the fixtures red.

*What I checked in the README's new statements,* each against the file that holds it: the four `option(CRYPTOCOGS_BUILD_<VENUE> … ON)`
lines and the "nothing will be built" warning (root `CMakeLists.txt`); `cryptocogs::common`, `::http` and the four venue aliases, and the
install list (`exchange_common exchange_http`, then one entry for each flag on) in `src/CMakeLists.txt`; `exchange_http` links
`CURL::libcurl` PUBLIC and `exchange_common` links no libcurl; toml++ is linked PRIVATE and `BUILD_INTERFACE` only, nlohmann/json is
included by the public headers; ixwebsocket, googletest, spdlog, CLI11 and backward-cpp are fetched only inside `CRYPTOCOGS_BUILD_TESTS`
(root file and `tests/`), and are used as the table says (`grep` finds spdlog in 14 files and CLI11 in 15 under `tests/examples`, and
`backward_init.cpp`; ixwebsocket in 7 files under `tests/examples` and 4 under `tests/unit`; gtest in none of the examples); Google Benchmark is fetched only under `CRYPTOCOGS_BUILD_BENCHMARKS`; the eight
`tests/*_checks/` directories exist; the clang-tidy paragraph the new list points up to is at line 662; and "mock HTTP performer" is the
playbook's wording for `make_<name>_test_client`. One statement was found false outside the README: the description of the
`CRYPTOCOGS_BUILD_BENCHMARKS` option says Kraken only, though `tests/benchmarks/CMakeLists.txt` makes four executables, one for each
adapter: [#74](https://github.com/tomlin256/cryptocogs/issues/74) (`documentation`, in this milestone).

*Checkpoint.* `debug-macos-arm64`: 0 objects built (the change is CMake scripts and prose), no warning; **600 of 600** (7.8 s with `-j8`),
the two new tests at 0.02 s and 0.20 s; `test_cmake_floor`, `test_curl_floor`, `test_ci_cache_key`, `test_ci_paths`,
`test_claude_md_budget` and `test_plan_docs` among them. `ubuntu-dev` (CMake 4.4.5): `debug-linux-arm64` reconfigured, 600 tests listed,
and the 11 that read the README, `CLAUDE.md`, the workflows and the plans passed in 0.67 s. The README, `CLAUDE.md`, the two scripts,
`tests/CMakeLists.txt`, the text of #74 and the plan passed `check_public_leaks.py --tree` (exit 0).

*What differed from the plan.*
- *Start from green* was the Step 1 checkpoint and CI's verdict on it, not a new run.
- 21 fixtures, not 7, and 18 hand-made changes to the check script on top of the six of the plan: six changes to the repository
  exercise the check's main rules, and a fixture that passes whatever the check does is what the fixtures exist to rule out.
- #74 was opened: a false statement outside the README that the step's reading found.
- The new comments in the check scripts and in `tests/CMakeLists.txt` name the plan and no issue, because the issue closes with the step.
