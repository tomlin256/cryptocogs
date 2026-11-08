// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// Tests of MockWsConnection's thread-safe outbound log: sent(), wait_for_sent() and release_waiters() (plan
// tsan-races-in-the-tests, #27).
//
// The mock is shared by the Kraken, Binance, Coinbase and Crypto.com WebSocket suites, and Execute.BlocksUntilResponse
// reads it from a second thread, so what it promises about threads is itself tested. Nothing here sleeps: every wait is
// on a predicate, so it does not matter which thread gets there first. In the tsan tree these tests are also what
// proves that the lock is taken.

#include "mock_ws_connection.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <future>
#include <string>
#include <thread>
#include <vector>

TEST(MockWsConnection, SentReturnsTheMessagesInOrderAsACopy) {
    MockWsConnection conn;
    conn.send("a");
    conn.send("b");
    conn.send("c");

    // Binding a reference to the returned temporary keeps that copy alive. Were sent() to return a reference to the log
    // itself, this would follow the log, and the size below would be 4.
    const auto& snapshot = conn.sent();
    EXPECT_EQ(snapshot, (std::vector<std::string>{"a", "b", "c"}));

    conn.send("d");
    EXPECT_EQ(snapshot.size(), 3u) << "a copy taken earlier must not follow the log";
    EXPECT_EQ(conn.sent().size(), 4u);
}

TEST(MockWsConnection, WaitForSentReturnsAtOnceWhenAlreadySatisfied) {
    MockWsConnection conn;
    conn.send("a");
    conn.send("b");

    EXPECT_TRUE(conn.wait_for_sent(2));
    EXPECT_TRUE(conn.wait_for_sent(1));
    EXPECT_TRUE(conn.wait_for_sent(0));
}

TEST(MockWsConnection, WaitForSentWakesWhenAnotherThreadSends) {
    // Many rounds, because the order that a missing notification breaks (the waiter is already waiting when the message
    // arrives) cannot be forced without a sleep. The waiter says it is about to wait and the sender goes once it has heard
    // that, which makes that order the usual one. A correct mock passes every round in either order.
    for (int round = 0; round < 50; ++round) {
        MockWsConnection conn;
        std::promise<void> about_to_wait;
        const std::future<void> heard = about_to_wait.get_future();
        bool woke = false;

        std::thread waiter([&] {
            about_to_wait.set_value();
            woke = conn.wait_for_sent(1);
        });
        heard.wait();
        conn.send("x");
        waiter.join();

        EXPECT_TRUE(woke) << "round " << round;
    }
}

TEST(MockWsConnection, ReleaseWaitersWakesWithFalse) {
    for (int round = 0; round < 50; ++round) {
        MockWsConnection conn;
        std::promise<void> about_to_wait;
        const std::future<void> heard = about_to_wait.get_future();
        bool reached = true;

        std::thread waiter([&] {
            about_to_wait.set_value();
            reached = conn.wait_for_sent(1);     // nothing is ever sent
        });
        heard.wait();
        conn.release_waiters();
        waiter.join();

        EXPECT_FALSE(reached) << "round " << round;
    }

    // A waiter that comes after the release does not block either: true if its count was already reached, false if not.
    MockWsConnection conn;
    conn.send("a");
    conn.release_waiters();
    EXPECT_TRUE(conn.wait_for_sent(1));
    EXPECT_FALSE(conn.wait_for_sent(2));
}

TEST(MockWsConnection, ConcurrentSendsLoseNothingAndKeepEachThreadsOrder) {
    constexpr int threads = 4;
    constexpr int per_thread = 100;
    MockWsConnection conn;

    std::vector<std::thread> senders;
    senders.reserve(static_cast<std::size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        senders.emplace_back([&conn, t] {
            for (int k = 0; k < per_thread; ++k) {
                conn.send(std::to_string(t) + ":" + std::to_string(k));
            }
        });
    }
    for (std::thread& sender : senders) {
        sender.join();
    }

    const std::vector<std::string> all = conn.sent();
    ASSERT_EQ(all.size(), static_cast<std::size_t>(threads * per_thread));

    // Each thread's messages appear in the order that thread sent them.
    std::vector<int> next(threads, 0);
    for (const std::string& message : all) {
        const std::size_t colon = message.find(':');
        const int t = std::stoi(message.substr(0, colon));
        const int k = std::stoi(message.substr(colon + 1));
        EXPECT_EQ(k, next[static_cast<std::size_t>(t)]) << "thread " << t;
        ++next[static_cast<std::size_t>(t)];
    }
    for (int t = 0; t < threads; ++t) {
        EXPECT_EQ(next[static_cast<std::size_t>(t)], per_thread) << "thread " << t;
    }
}
