// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// canary_asan.cpp — a deliberate heap-use-after-free (plan 032, #30).
//
// Built and run only in a -DCRYPTOCOGS_SANITIZE=asan tree, through tests/sanitize_checks/run_canary.cmake, which
// requires AddressSanitizer to kill it and say so. The program itself returns 0: if the sanitizer's options never
// reached the compiler it exits cleanly, run_canary.cmake sees no report, and the test goes red. That is the point.
// A sanitizer tree whose options silently did not apply is green on every real test and tells you nothing.
//
// noinline and volatile keep the optimiser from proving the access dead and deleting it. The block is freed behind
// a call, so the compiler cannot pair the new with the delete and drop both.

namespace {

__attribute__((noinline)) void release(int* const block) {
    delete[] block;
}

__attribute__((noinline)) int read_first(const volatile int* const block) {
    return block[0];
}

}  // namespace

int main() {
    int* const block = new int[8]{};
    release(block);
    // Heap-use-after-free: ASan aborts here. Uninstrumented, this reads freed memory and carries on.
    const int stale = read_first(block);
    static_cast<void>(stale);
    return 0;
}
