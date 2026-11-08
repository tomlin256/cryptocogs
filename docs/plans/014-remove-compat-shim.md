# Remove the deprecated `kraken_*.hpp` / `kraken::` compatibility shim

**Status:** Done — branch `chore/remove-shim-rebased`, released as **v0.1.1**.

## Goal

Delete the pre-refactor Kraken compatibility shim — **both layers** — and cut a
clean release:

- the **header shim** ([plan step-2b-compat-shim](002-step-2b-compat-shim.md)): `kraken_compat.hpp`,
  the seven old-path `kraken_*.hpp` forwarders, and `ws_reconnect_session.{hpp,inl}`,
  which reopened the legacy `kraken::` namespace; and
- the **source shim** ([plan source-compat-shim](013-source-compat-shim.md), merged to `main` via
  PR #19): the `src/kraken_{ws,rest}_client.cpp` unity TUs for consumers that
  compiled those legacy source paths directly.

Both existed only for consumers that had not yet moved to the `exchange::kraken::*`
surface; the ones known have since migrated fully onto it. With no consumer left,
the entire shim is dead weight.

## What changed

**Removed (14 files):**

- Header shim (11): `include/kraken_compat.hpp`; the forwarders
  `include/kraken_{types,rest_api,rest_client,ws_api,ws_client}.hpp` +
  `include/kraken_ws_client.inl` + `include/kraken_ix_ws_connection.hpp`;
  `include/ws_reconnect_session.{hpp,inl}`; and `tests/unit/test_compat_shim.cpp`.
- Source shim (3): `src/kraken_ws_client.cpp`, `src/kraken_rest_client.cpp`, and
  `tests/unit/test_compat_source_shim.cpp`.

**Removed (build wiring):**

- `CMakeLists.txt`: the `CRYPTOCOGS_BUILD_COMPAT_SHIM` option, its
  `CRYPTOCOGS_BUILD_KRAKEN` dependency guard, and the shim-header `install(FILES …)`
  block.
- `tests/unit/CMakeLists.txt`: the `test_compat_shim` target (header shim) and the
  `compat_ws_shim` / `compat_rest_shim` / `test_compat_source_shim` targets
  (source shim), plus their `CRYPTOCOGS_SUPPRESS_DEPRECATION` defines.

**Migrated to `exchange::kraken::*` (the two examples that still used the shim):**

- `tests/examples/rest_client_example.cpp` and `tests/examples/ws_client_example.cpp`.
  The URL-based `kraken::ws::make_ws_client(url)` — which the shim provided but the
  new surface does not — becomes the generic
  `exchange::ws::make_exchange_ws_client(url, exchange::kraken::ws::kraken_frame_descriptor)`,
  matching the Binance stream example; the connection form becomes
  `exchange::kraken::ws::make_kraken_ws_client(conn)`.

**Docs:** `README.md` and `CLAUDE.md` swept — every example migrated off `kraken::`,
the `CRYPTOCOGS_BUILD_COMPAT_SHIM` rows and shim file/test entries removed, the
"upgrade" note reworded to "removed in v0.1.1", and the test counts corrected
(below). `docs/plans.md` flags plans step-2b-compat-shim and source-compat-shim as "removed in remove-compat-shim". Project
version bumped `0.1.0` → `0.1.1`.

## Test

No new tests — this is a removal. The suite still builds and passes with the shim
gone, and the two migrated examples compile against the supported surface. The
build was re-verified against **ixwebsocket v12.0.0** (bumped from v11.4.6 on
`main` alongside the PR #19 merge).

## Result

Clean build; **312 / 312** ctest pass (the 4 `test_compat_shim` and 2
`test_compat_source_shim` cases are gone). Per-tree: `-DCRYPTOCOGS_BUILD_KRAKEN=OFF`
→ 147, `-DCRYPTOCOGS_BUILD_BINANCE=OFF` → 176. No `compat` test remains registered.

## Self-review

- **Risk: an external consumer still on the `kraken::` surface (or the legacy
  source paths) breaks.** Accepted and intended — the shim was always advertised as
  deprecated and slated for removal at the next version; the known consumers are
  already off it. The [migration guide](001-appendix-migration-guide.md) remains for
  anyone else.
- **Risk: stale docs.** Mitigated — README + CLAUDE.md were swept for `kraken::`,
  old-path includes, `COMPAT_SHIM`, and stale counts (all verified absent except
  the deliberate "removed in v0.1.1" notes).
- **Semver:** removing a (deprecated) public surface is normally a minor/major
  bump, but pre-1.0 and with every known consumer already migrated it ships as the
  patch **v0.1.1** as directed.
- **History preserved:** plans step-2b-compat-shim / source-compat-shim and the shipped-shim appendix stay in the
  tree as historical records; both are flagged "removed in remove-compat-shim" in the index.
