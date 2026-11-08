// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// CallbackProbe — finds a callback that is called after it was destroyed (plan transport-callback-lifetimes, #39 and #43).
//
// A destroyed std::function is still callable in practice: libc++ and libstdc++ do not clear it, so `if (cb)` is true and the
// call lands in a destroyed closure. A transport that destroys its callbacks before its I/O thread has stopped does exactly
// that, and neither the type system nor a sanitizer says so (the access that matters can sit in an uninstrumented library).
// The probe makes it visible. Every callback it is used with owns a token whose destructor counts one destruction, and every
// call records whether a destruction had already happened; a call that sees one is a call after a destruction.
//
//   conn.set_on_close([token = CallbackProbe::token()](std::string) {
//       CallbackProbe::record_call();            // first, and the body reads nothing it captured
//       ...
//   });
//   ...
//   EXPECT_EQ(probe.calls_after_a_destruction(), 0);
//
// The bodies must not read their captures: on the code this finds they run after the closure was destroyed. They reach the
// test's state through a namespace-scope pointer instead. For the same reason the counters are static, so a test makes one
// probe at a time; gtest runs the tests of a binary one after another, and the constructor starts a fresh count.

#pragma once

#include <atomic>
#include <memory>

class CallbackProbe {
public:
    CallbackProbe() noexcept { reset(); }
    CallbackProbe(const CallbackProbe&)            = delete;
    CallbackProbe& operator=(const CallbackProbe&) = delete;

    // Starts a fresh count. Call it only while no callback of an earlier count can still be called.
    void reset() noexcept {
        calls_.store(0);
        destroyed_.store(0);
        calls_after_a_destruction_.store(0);
    }

    // A token for a callback to capture by value. The callback keeps it alive; its destruction counts one.
    static std::shared_ptr<void> token() { return std::make_shared<Token>(); }

    // The first statement of every callback body.
    static void record_call() noexcept {
        calls_.fetch_add(1);
        if (destroyed_.load() > 0)
            calls_after_a_destruction_.fetch_add(1);
    }

    int calls() const noexcept                    { return calls_.load(); }
    int destroyed() const noexcept                { return destroyed_.load(); }
    int calls_after_a_destruction() const noexcept { return calls_after_a_destruction_.load(); }

private:
    struct Token {
        ~Token() { destroyed_.fetch_add(1); }
    };

    inline static std::atomic<int> calls_{0};
    inline static std::atomic<int> destroyed_{0};
    inline static std::atomic<int> calls_after_a_destruction_{0};
};
