// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/reconnect_session.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

using namespace exchange::ws;
using namespace std::chrono_literals;

// ── Test helpers ──────────────────────────────────────────────────────────────

// Poll `pred` up to `timeout`, sleeping 1 ms between checks.
// Returns true if pred() became true within the deadline.
template<typename Pred>
static bool waitFor(Pred&& pred, std::chrono::milliseconds timeout = 500ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

// Short backoff used across all tests so the suite runs fast.
static constexpr auto kTestBackoff = 5ms;

// ── start() ──────────────────────────────────────────────────────────────────

TEST(WsReconnectSession, StartCallsConnectOnce) {
    std::atomic<int> connects{0};

    WsReconnectSession session(
        [&]{ ++connects; },
        []{},
        kTestBackoff
    );
    session.start();
    session.stop();

    EXPECT_EQ(connects.load(), 1);
}

TEST(WsReconnectSession, StartPropagatesConnectException) {
    WsReconnectSession session(
        []{ throw std::runtime_error("no network"); },
        []{},
        kTestBackoff
    );
    EXPECT_THROW(session.start(), std::runtime_error);
}

// ── stop() ───────────────────────────────────────────────────────────────────

TEST(WsReconnectSession, StopCallsDisconnect) {
    std::atomic<int> disconnects{0};

    WsReconnectSession session(
        []{},
        [&]{ ++disconnects; },
        kTestBackoff
    );
    session.start();
    session.stop();

    EXPECT_GE(disconnects.load(), 1);
}

TEST(WsReconnectSession, StopBeforeStartIsSafe) {
    WsReconnectSession session([]{}, []{}, kTestBackoff);
    // Should not crash or hang.
    session.stop();
}

TEST(WsReconnectSession, DestructorStopsCleanly) {
    std::atomic<bool> workerStarted{false};

    {
        WsReconnectSession session(
            [&]{ workerStarted.store(true); },
            []{},
            kTestBackoff
        );
        session.start();
        // Session goes out of scope here — destructor must join the thread.
    }

    EXPECT_TRUE(workerStarted.load());
    // If we reach here without deadlock, the destructor worked correctly.
}

// ── scheduleReconnect() ───────────────────────────────────────────────────────

TEST(WsReconnectSession, ScheduleReconnectTriggersFurtherConnect) {
    std::atomic<int> connects{0};

    WsReconnectSession session(
        [&]{ ++connects; },
        []{},
        kTestBackoff
    );
    session.start();

    session.scheduleReconnect("test disconnect");

    // Wait for the second connect (initial + 1 reconnect)
    EXPECT_TRUE(waitFor([&]{ return connects.load() >= 2; }));

    session.stop();
}

TEST(WsReconnectSession, ScheduleReconnectCallsDisconnectBeforeReconnect) {
    std::atomic<int> disconnects{0};
    std::atomic<int> connects{0};

    WsReconnectSession session(
        [&]{ ++connects; },
        [&]{ ++disconnects; },
        kTestBackoff
    );
    session.start();
    int disconnectsAfterStart = disconnects.load();

    session.scheduleReconnect();

    // Wait for reconnect to complete
    EXPECT_TRUE(waitFor([&]{ return connects.load() >= 2; }));

    // disconnect_fn should have been called at least once during the reconnect attempt
    // (in addition to the final call from stop())
    EXPECT_GT(disconnects.load(), disconnectsAfterStart);

    session.stop();
}

TEST(WsReconnectSession, ScheduleReconnectAfterStopIsNoOp) {
    std::atomic<int> connects{0};

    WsReconnectSession session(
        [&]{ ++connects; },
        []{},
        kTestBackoff
    );
    session.start();
    session.stop();

    int connectsAfterStop = connects.load();
    session.scheduleReconnect("too late");

    // Give the worker thread a moment (it is already stopped)
    std::this_thread::sleep_for(20ms);

    EXPECT_EQ(connects.load(), connectsAfterStop);
}

// ── Reconnect on failure / backoff ───────────────────────────────────────────

TEST(WsReconnectSession, FailedConnectIsRetriedWithBackoff) {
    std::atomic<int> connects{0};
    // Fail the first two reconnect attempts, succeed on the third.
    // (The initial connect in start() always succeeds.)

    WsReconnectSession session(
        [&]{
            int n = ++connects;
            if (n == 2 || n == 3) throw std::runtime_error("transient failure");
        },
        []{},
        kTestBackoff
    );
    session.start();

    session.scheduleReconnect("dropped");

    // Wait for the successful reconnect (4th total connect call)
    EXPECT_TRUE(waitFor([&]{ return connects.load() >= 4; }, 2000ms));

    session.stop();
}

TEST(WsReconnectSession, StopDuringBackoffExitsPromptly) {
    // Use a long backoff to make the timing observable.
    static constexpr auto kLongBackoff = 10s;

    std::atomic<int> connects{0};

    WsReconnectSession session(
        [&]{
            int n = ++connects;
            if (n >= 2) throw std::runtime_error("down");
        },
        []{},
        kLongBackoff
    );
    session.start();
    session.scheduleReconnect();

    // Give the worker time to enter the backoff wait
    std::this_thread::sleep_for(20ms);

    auto t0 = std::chrono::steady_clock::now();
    session.stop();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    // stop() must unblock the worker well before the 10 s backoff expires.
    EXPECT_LT(elapsed, 2s);
}

// ── Backoff reset ─────────────────────────────────────────────────────────────

TEST(WsReconnectSession, BackoffResetsAfterSuccessfulReconnect) {
    // Fail twice on first reconnect, then succeed.
    // On second reconnect, the first attempt should succeed immediately
    // (backoff reset to min_backoff), not take doubled time.
    std::atomic<int> connects{0};
    // Decremented by the session's worker thread (the connect callback) and read by this thread in the
    // waitFor predicates below, so it is atomic, like connects (#28).
    std::atomic<int> failsRemaining{2};

    WsReconnectSession session(
        [&]{
            int n = ++connects;
            if (n >= 2 && failsRemaining > 0) {
                --failsRemaining;
                throw std::runtime_error("transient");
            }
        },
        []{},
        kTestBackoff
    );
    session.start();

    // First reconnect: fails twice, then succeeds.
    session.scheduleReconnect("first drop");
    EXPECT_TRUE(waitFor([&]{ return failsRemaining == 0 && connects.load() >= 4; }, 1000ms));

    // Second reconnect: should succeed on the first attempt (backoff reset).
    int connectsBeforeSecond = connects.load();
    session.scheduleReconnect("second drop");
    EXPECT_TRUE(waitFor([&]{ return connects.load() > connectsBeforeSecond; }, 500ms));

    session.stop();
}

// ── A DisconnectFn that throws ────────────────────────────────────────────────
//
// reconnect_session.hpp says DisconnectFn must not throw. The two empty catch blocks around it, in stop() and in the worker
// loop, defend that contract (they carry a NOLINT that says so): stop() runs from the destructor, which must not throw,
// and a throw that escaped the worker thread would end the process. These tests are what keeps those blocks honest. Without
// a catch, the destructor test and the worker test end the process (std::terminate) instead of failing politely.

TEST(WsReconnectSession, StopContainsAThrowFromDisconnect) {
    std::atomic<int> disconnects{0};

    WsReconnectSession session(
        []{},
        [&]{ ++disconnects; throw std::runtime_error("disconnect failed"); },
        kTestBackoff
    );
    session.start();

    EXPECT_NO_THROW(session.stop());
    EXPECT_EQ(disconnects.load(), 1);
}

TEST(WsReconnectSession, DestructorContainsAThrowFromDisconnect) {
    std::atomic<int> disconnects{0};

    {
        WsReconnectSession session(
            []{},
            [&]{ ++disconnects; throw std::runtime_error("disconnect failed"); },
            kTestBackoff
        );
        session.start();
        // The destructor calls stop(), which calls the throwing disconnect once: a throw out of it would end the process.
    }

    EXPECT_EQ(disconnects.load(), 1);
}

// The worker thread calls DisconnectFn before every reconnect attempt. A throw there must neither end the thread nor
// stop the reconnect: the next connect still happens.
TEST(WsReconnectSession, WorkerContainsAThrowFromDisconnectAndStillReconnects) {
    std::atomic<int> connects{0};
    std::atomic<int> disconnects{0};
    std::promise<void> reconnected;

    WsReconnectSession session(
        [&]{ if (++connects == 2) reconnected.set_value(); },   // the initial connect is 1, the reconnect is 2
        [&]{ ++disconnects; throw std::runtime_error("disconnect failed"); },
        kTestBackoff
    );
    session.start();
    session.scheduleReconnect("dropped");

    reconnected.get_future().get();   // returns once the worker has disconnected (throwing) and connected again

    EXPECT_GE(disconnects.load(), 1);
    EXPECT_EQ(connects.load(), 2);
    session.stop();
}
