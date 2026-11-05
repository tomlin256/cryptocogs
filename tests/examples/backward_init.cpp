// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include <backward.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {

// What std::terminate runs: the backtrace of the throw site (an unhandled exception is not unwound before terminate),
// then abort.
void print_backtrace_and_abort() {
    fprintf(stderr, "[terminate] unhandled exception — backtrace:\n");
    backward::StackTrace st;
    st.load_here(32);
    backward::Printer p;
    p.print(st, stderr);
    std::abort();
}

// The two installers run before main, in this order, from the initialisers of the variables below. An exception out of
// the initialiser of a namespace-scope variable cannot be caught and ends the process in std::terminate; an exception
// out of a noexcept function does the same, and says so where a static analysis can see it. SignalHandling has to
// outlive main, hence the static.
bool install_signal_handling() noexcept {
    static backward::SignalHandling sh;
    return sh.loaded();
}

bool install_terminate_handler() noexcept {
    std::set_terminate(print_backtrace_and_abort);
    return true;
}

[[maybe_unused]] const bool signal_handling_installed = install_signal_handling();
[[maybe_unused]] const bool terminate_handler_installed = install_terminate_handler();

} // namespace
