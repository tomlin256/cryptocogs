// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// canary_tsan.cpp — a deliberate data race (plan sanitizer-option-and-presets, #30).
//
// Built and run only in a -DCRYPTOCOGS_SANITIZE=tsan tree, through tests/sanitize_checks/run_canary.cmake, which
// requires a non-zero exit AND ThreadSanitizer's "data race" report. The program returns 0, so a build whose
// sanitizer options never applied exits cleanly and the test goes red.
//
// Two threads write one plain int with nothing ordering them. No sleep is needed to make them overlap:
// ThreadSanitizer finds races by happens-before analysis, not by catching two accesses in the act. Neither thread
// is joined before the other starts, so the second write has no ordering after the first however the scheduler runs
// them, and the report is deterministic.
//
// volatile keeps the optimiser from deleting the writes. Nothing here reads the variable and it has internal
// linkage, so without it both stores are dead and are removed: no race is compiled, none is reported, and the
// canary exits 0 under a live ThreadSanitizer.
// ThreadSanitizer treats a volatile access as an ordinary one, so the race is still a race.

#include <thread>

namespace {

volatile int shared_value = 0;  // neither atomic nor guarded: the race

}  // namespace

int main() {
    std::thread first([] { shared_value = 1; });
    std::thread second([] { shared_value = 2; });
    first.join();
    second.join();
    return 0;
}
