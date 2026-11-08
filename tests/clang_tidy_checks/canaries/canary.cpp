// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// canary.cpp — a unit with one defect of its own, which clang-tidy must report (plan clang-tidy-opt-in-test-step, #34).
//
// The clang_tidy_canary_* tests run clang-tidy on this file, with the real .clang-tidy, after configure has copied it and
// the two headers under the build tree. It is in no compile database and no target, so the gate over the project's own
// sources never sees it, and cryptocogs_clang_tidy_select leaves this directory out. It proves the gate bites:
//
//   - here: a string read after it was moved, which bugprone-use-after-move reports as an error;
//   - canary_own.hpp, copied to .../include/exchange/: the same unit includes a header whose path the header filter admits
//     as this project's, with a defect of its own;
//   - canary_dep.hpp, copied to .../_deps/canary-src/tests/: a header with that defect at a path the filter must refuse,
//     included by -I as a dependency's is. Its path matches the filter's inclusion (/tests/), so only the exclusion of
//     /_deps/ keeps it out.

#include "canary_dep.hpp"
#include "exchange/canary_own.hpp"

#include <string>
#include <utility>

int main() {
    std::string s = "x";
    std::string t = std::move(s);
    return static_cast<int>(s.size() + t.size() + canary_own_size("e") + canary_dep_size("d"));
}
