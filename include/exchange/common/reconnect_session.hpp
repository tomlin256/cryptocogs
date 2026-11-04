// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/reconnect_session.hpp
// WsReconnectSession — transport-level WebSocket reconnect machinery.
//
// Encapsulates the background thread, mutex/cv, exponential backoff, and stop
// signal that every reconnecting WebSocket consumer needs.  Callers supply two
// callbacks and never implement the thread machinery themselves:
//
//   ConnectFn    — called to open (or re-open) the connection; throws on failure.
//   DisconnectFn — called to tear down the current connection before reconnecting,
//                  and once more on stop(). Must not throw.
//
// Typical usage:
//   WsReconnectSession session(
//       [&]{ conn.connect(); conn.subscribe(); },
//       [&]{ conn.disconnect(); }
//   );
//   session.start();
//   conn.set_on_close([&](std::string reason) {
//       session.scheduleReconnect(std::move(reason));
//   });
//   // On shutdown:
//   session.stop();
//
// Backoff: initial = minBackoff (default 1 s), doubles per consecutive failure,
// caps at 60 s.  Resets to minBackoff after a successful connect.

#include "exchange/common/cxx_standard.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace exchange::ws {

class WsReconnectSession {
public:
    using ConnectFn    = std::function<void()>;
    using DisconnectFn = std::function<void()>;

    static constexpr std::chrono::milliseconds kMaxBackoff{60000};

    WsReconnectSession(ConnectFn    connect,
                       DisconnectFn disconnect,
                       std::chrono::milliseconds minBackoff = std::chrono::seconds{1});

    ~WsReconnectSession();

    // Establish the initial connection and start the background reconnect thread.
    // Calls connect_fn_() on the calling thread; throws if the initial connect fails.
    void start();

    // Signal the reconnect thread to stop, wait for it to exit, then call
    // disconnect_fn_() to tear down the live connection.
    void stop();

    // Schedule a reconnect attempt.  Safe to call from any thread.
    void scheduleReconnect(std::string reason = "");

private:
    ConnectFn                 connect_fn_;
    DisconnectFn              disconnect_fn_;
    std::chrono::milliseconds min_backoff_;

    std::atomic<bool>       stop_requested_{false};
    std::atomic<bool>       reconnect_requested_{false};
    std::mutex              mu_;
    std::condition_variable cv_;
    std::thread             worker_;

    void workerLoop();
};

} // namespace exchange::ws

#include "exchange/common/reconnect_session.inl"
