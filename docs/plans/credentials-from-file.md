# TOML `from_file` credential loaders for all four adapters

## Motivation

Only Kraken's `Credentials` has a `from_file` loader, and it parses a bespoke
two-line text file. The Binance, Coinbase, and Crypto.com credential structs have
no loader at all, so client/example code that reads keys off disk is messy and
inconsistent. This plan gives **all four** adapters a uniform `from_file` that
parses a **TOML** credentials file via one shared helper, and **upgrades Kraken's
existing loader to TOML** in the process.

This is a **breaking change** (existing line-based `~/.kraken/<name>` files stop
working and must be rewritten as TOML) — accepted by the user. Higher blast
radius is acceptable in exchange for one consistent, robust mechanism.

## Scope

- **In scope:** a shared `exchange::rest::read_toml_credentials(...)` helper in the
  common scaffold; a `static <Creds> from_file(name, location="")` on all four
  credential structs delegating to it; toml++ as a new (build-only) dependency;
  unit tests; docs; a breaking-change version bump.
- **Out of scope:** reading optional non-secret config (e.g. Binance
  `recv_window_ms`/`algorithm`) from the file — defaults are preserved, caller
  overrides after load (noted as a possible follow-up); any signing/transport
  change; the legacy `tests/examples/kapi.cpp` reference wrapper (its own private
  loader is untouched).

## Design decisions (confirm at approval)

- **TOML library: toml++ (`marzer/tomlplusplus`), pinned `v3.4.0`**, header-only,
  C++17. Fetched with `FetchContent` like `nlohmann_json`. It is used **only**
  inside `src/exchange/common/credentials_file.cpp` and never appears in a public
  header, so it is a pure build-time implementation detail.
- **Uniform file format — TOML with uniform keys across exchanges:**

  | Exchange | Default dir | Required TOML keys |
  |---|---|---|
  | Kraken | `~/.kraken` | `api_key`, `api_secret` |
  | Binance | `~/.binance` | `api_key`, `api_secret` (→ struct `secret_key`) |
  | Coinbase | `~/.coinbase` | `api_key`, `api_secret`, `passphrase` |
  | Crypto.com | `~/.cryptocom` | `api_key`, `api_secret` |

  Example `~/.kraken/default`:
  ```toml
  api_key    = "PUBLICKEY..."
  api_secret = "base64secret..."
  ```
  Note Binance's on-disk key is `api_secret` (uniform) even though its struct
  field is `secret_key` — the on-disk format is deliberately identical across
  exchanges.
- **Path resolution unchanged from Kraken:** `location` empty ⇒ `$HOME/<dir>/<name>`;
  non-empty ⇒ `<location>/<name>` (no `~` expansion, no forced `.toml` extension —
  the profile `name` is the filename, so existing `from_file("default")` call
  sites keep working).
- **Shared helper** (in `exchange::rest`, declared in
  `include/exchange/common/credentials_file.hpp`, body in
  `src/exchange/common/credentials_file.cpp`, compiled into **`exchange_common`** —
  which stays curl/ssl-free; toml++ is header-only with no link cost):
  ```cpp
  namespace exchange::rest {
  // Resolve <dir>/<name> (dir defaults to $HOME/<default_dir> when location is
  // empty), parse it as TOML, and return the values of `keys` in order.
  // Throws std::runtime_error on: HOME unset (default-location branch), open or
  // TOML-parse failure, or any key missing / not a string / empty.
  std::vector<std::string> read_toml_credentials(
      const std::string& name,
      const std::string& default_dir,            // e.g. ".kraken"
      const std::string& location,               // "" => $HOME/<default_dir>
      const std::vector<std::string>& keys);
  }
  ```
  Each adapter's `from_file` is then a 2-liner mapping the returned values to its
  fields, e.g. `auto v = read_toml_credentials(name, ".coinbase", location,
  {"api_key","api_secret","passphrase"}); return {v[0], v[1], v[2]};`.
- **Errors** remain `std::runtime_error` (matching Kraken today), so the existing
  `test_client.cpp` `CredentialsFromFile` error tests stay valid.
- **Aggregate init unaffected:** adding a `static` member function keeps all four
  structs C++17 aggregates, so `BinanceCredentials{k,s}` etc. keep compiling — a
  step build proves it.
- **CMake export safety:** toml++ is linked
  `PRIVATE $<BUILD_INTERFACE:tomlplusplus::tomlplusplus>` to `exchange_common`,
  mirroring the `$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>` guard, so it is
  **not** recorded in `cryptocogsTargets` and needs no vendoring (no public header
  references it). Step 6 verifies the install prefix has zero toml++ artifacts.

## Steps

Each coding step ends with a checkpoint: full `cmake --build build` **and**
`ctest --output-on-failure` from `build/`, both green, then a commit.

### Step 1 — toml++ dependency + shared helper (fully tested in isolation)
- Top-level `CMakeLists.txt`: `FetchContent_Declare/MakeAvailable` toml++ `v3.4.0`,
  placed with `nlohmann_json` (always available — `exchange_common` needs it
  unconditionally, not just under `CRYPTOCOGS_BUILD_TESTS`).
- New `include/exchange/common/credentials_file.hpp` (declaration, std types only,
  banner) + `src/exchange/common/credentials_file.cpp` (toml++ impl, banner).
- `src/CMakeLists.txt`: add the `.cpp` to `exchange_common` and link
  `PRIVATE $<BUILD_INTERFACE:tomlplusplus::tomlplusplus>`.
- New `tests/unit/test_credentials_file.cpp` (+ target in `tests/unit/CMakeLists.txt`,
  linking `exchange_common GTest::gtest_main`; runs in any build, like
  `test_tick_price`). Cover, via a `std::filesystem` temp-dir RAII fixture:
  happy path (explicit location); `$HOME`-default resolution; file-not-found;
  malformed TOML; missing key; non-string value (e.g. integer); empty-string
  value; `HOME` unset + empty location. Deterministic, no sleeps.
- **Done:** build + all tests pass; the helper is correct before any adapter uses it.

### Step 2 — Kraken loader → TOML (breaking)
- Rewrite `Credentials::from_file` in `src/kraken/auth.cpp` to delegate to
  `read_toml_credentials(name, ".kraken", location, {"api_key","api_secret"})`;
  drop the now-unused line-reading code; update the header comment.
- Add a Kraken TOML happy-path test in `tests/unit/test_client.cpp`; confirm the
  existing `ThrowsWhenFileNotFound` / `ThrowsWhenHomeUnset` tests still pass.
- **Done:** build + all tests pass.

### Step 3 — Binance `from_file`
- Declare `static BinanceCredentials from_file(...)` in `auth.hpp`; define in
  `src/binance/auth.cpp` via the helper (`{"api_key","api_secret"}`), leaving
  `algorithm`/`recv_window_ms` at defaults.
- Tests in `tests/unit/test_binance_auth.cpp`: happy-path field mapping + assert
  defaults preserved + one missing-key error (wiring check).
- **Done:** build + all tests pass.

### Step 4 — Coinbase `from_file`
- Declare on `CoinbaseCredentials`; update the "no from_file loader" comment;
  define via the helper (`{"api_key","api_secret","passphrase"}`).
- Tests in `tests/unit/test_coinbase_auth.cpp`: 3-field happy path + a
  missing-`passphrase` error.
- **Done:** build + all tests pass.

### Step 5 — Crypto.com `from_file`
- Declare on `CryptoComCredentials`; update the "no from_file loader" comment;
  define via the helper (`{"api_key","api_secret"}`).
- Tests in `tests/unit/test_cryptocom_auth.cpp`: happy path + a missing-key error.
- **Done:** build + all tests pass.

### Step 6 — Docs, version bump, flag-matrix + install validation
- `CLAUDE.md`: add toml++ to the FetchContent table; rewrite **Credentials file
  format** as TOML for all four (with the key table + example above); update the
  Binance/Coinbase/Crypto.com auth rows (drop "no file loader") and Kraken's
  loader description; add the new header/cpp to the project-structure tree and
  note `exchange_common` now also compiles `credentials_file.cpp`.
- Update the `private_rest.cpp` / `private_ws.cpp` help text/comments to mention
  the TOML format (the `from_file` call itself is unchanged).
- Bump project `VERSION` `0.4.0` → `0.5.0` (breaking change) in `CMakeLists.txt`.
- Validate: a Crypto.com-only configure/build/test (`-DCRYPTOCOGS_BUILD_KRAKEN=OFF
  -DCRYPTOCOGS_BUILD_BINANCE=OFF -DCRYPTOCOGS_BUILD_COINBASE=OFF`) still passes,
  and `cmake --install build --prefix <tmp>` produces **no** toml++ files.
- Mark plan credentials-from-file **Done** in `docs/plans.md`.
- **Done:** full build + test green; install prefix clean.

## Self-review — risks, assumptions, alternatives

- **Breaking change is intentional.** Existing line-based key files (incl. the
  user's own `~/.kraken/default`) must be rewritten as TOML. Documented; version
  bumped to 0.5.0. Unit tests read only temp files they write, so CI is unaffected.
- **toml++ stays out of the public surface and the install export.** Used only in
  one `.cpp`, linked `PRIVATE $<BUILD_INTERFACE:...>` (same trick as nlohmann_json).
  Step 6 asserts the prefix is toml-free. If the `v3.4.0` tag fails to fetch,
  Step 1's first configure surfaces it immediately and the tag is adjusted.
- **`exchange_common` stays curl/ssl-free.** toml++ is header-only (no link
  dependency); the helper does no crypto/HTTP — consistent with the lib's charter.
- **Uniform `api_secret` key** differs from Binance's `secret_key` struct field —
  deliberate, for one identical on-disk format; documented at the mapping site.
- **Alternative rejected (per user):** four independent per-adapter TOML parsers.
  The shared helper removes the parse/validate/path-resolution duplication that
  would otherwise be copied four times — the "better outcome" the user asked for.
- **Optional config in TOML** (Binance `recv_window_ms`/`algorithm`) is out of
  scope; the string-only required-keys helper keeps the interface simple. Easy
  follow-up if wanted.
- **Legacy `kapi.cpp`** keeps its own `~/.kraken` reader; not part of the public
  API and not migrated. Noted so the divergence is intentional, not an oversight.
- **No security regression:** loaders only read a caller-named path; secrets are
  never logged.
