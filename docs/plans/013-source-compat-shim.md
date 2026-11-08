# Source-compatibility shim for legacy `src/kraken_{ws,rest}_client.cpp` paths

**Status:** Done — `0f047b5`

## Goal

Complete the deprecation story started in [plan step-2b-compat-shim](002-step-2b-compat-shim.md).
Plan step-2b-compat-shim shipped the **header** compat shim (`kraken_compat.hpp` + the seven
`kraken_*.hpp` / `ws_reconnect_session.hpp` forwarders), so pre-refactor callers
keep compiling against the `kraken::` namespace. But it deleted/relocated the old
**source** files (`src/kraken_ws_client.cpp`, `src/kraken_rest_client.cpp`,
`src/kraken_types.cpp`) — which breaks a class of consumer the header shim cannot
help: those that do **not** link cryptocogs's CMake targets, and instead build
their own static libraries by compiling specific cryptocogs source paths directly.

A build of that kind looks like this:

```cmake
add_library(ws_lib   STATIC ${CRYPTOCOGS_DIR}/src/kraken_ws_client.cpp)
add_library(rest_lib STATIC ${CRYPTOCOGS_DIR}/src/kraken_rest_client.cpp)
```

After the refactor these paths don't exist, so such a CMake fails at generate
time.

## What changed

Re-add the two legacy source paths as thin **unity translation units** that
`#include` the relocated implementation source(s), mapping each old path onto its
new TUs:

| Legacy path (re-added) | Pulls in |
|---|---|
| `src/kraken_ws_client.cpp` | `exchange/common/ws_client.cpp` |
| `src/kraken_rest_client.cpp` | `kraken/rest_client.cpp` + `exchange/common/http_client.cpp` + `exchange/common/tick_price.cpp` |

### Load-bearing decisions

- **Not added to `src/CMakeLists.txt`.** cryptocogs's own libraries
  (`exchange_common`, `exchange_http`, `cryptocogs`) already compile those TUs
  directly; compiling these copies in the same build would duplicate symbols.
  The shim files exist purely for *external* builds that hardcode the old paths.
- **`tick_price.cpp` goes in the REST shim only.** Its only out-of-line symbol is
  `exchange::TickPrice::from_json`, which the WS path never references (WS uses
  `TickPrice` solely as outbound `limit_price`/`post_only_price`, serialized
  inline). Putting it in both shims would give a consumer that links both libs a
  duplicate-symbol error.
- **`src/kraken_types.cpp` not re-added** — no known consumer compiles that path.
  Trivial to add later by the same pattern if one appears.

## Test

`tests/unit/test_compat_source_shim.cpp` (+ `compat_ws_shim` / `compat_rest_shim`
targets), gated `CRYPTOCOGS_BUILD_KRAKEN AND CRYPTOCOGS_BUILD_COMPAT_SHIM`, built
with `CRYPTOCOGS_SUPPRESS_DEPRECATION`. It **reproduces a consumer's build**: two
static libs compiled from the legacy paths (with the same direct dependencies a
consumer uses), linked together into one executable that deliberately does **not**
link `cryptocogs`/`exchange_common`/`exchange_http`. This guards both regressions a
header-only proof cannot: a TU relocating again (compile/link break) and
`tick_price.cpp` reaching both libs (duplicate-symbol error). Two behavioural
smoke tests exercise the WS (`make_ws_client(conn)` → ping) and REST
(`make_test_client` → `GetServerTimeRequest`) surfaces through the deprecated API.

## Result

Clean cryptocogs build; **318/318** ctest pass (incl. the 2 new tests). Verified
against an existing external build that compiles the legacy paths: it builds all
of its targets and passes its own tests against this branch, with no change to its
sources or CMake.

## Self-review

- **`#include`-of-`.cpp` is a deliberate unity-TU technique**, not an accident —
  it lets one hardcoded source path yield several relocated TUs' object code while
  delegating to the real sources (cannot drift). Each is marked
  `NOLINT(bugprone-suspicious-include)` and heavily commented. The new test fails
  loudly if a future refactor re-splits a TU without updating the shim.
- **Risk:** someone later adds these files to `src/CMakeLists.txt` (→ duplicate
  symbols in cryptocogs's own build). Mitigated by an explicit comment in each
  file and the fact that the source lists are hand-maintained (no globbing).
- **Deprecation:** these are deprecated like the rest of the shim; they go away
  with the `kraken_*.hpp` forwarders at the next major. No `#pragma message` is
  added (a `.cpp` warning at compile time of a consumer's lib would be noise; the
  header forwarders already announce deprecation when those consumers include the
  matching `kraken_*.hpp`).
