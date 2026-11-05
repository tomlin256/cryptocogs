// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// exchange/common/reconnect_session.inl — WsReconnectSession method implementations.
// Included at the bottom of reconnect_session.hpp; never include directly.

#pragma once

namespace exchange::ws {

inline WsReconnectSession::WsReconnectSession(ConnectFn    connect,
                                               DisconnectFn disconnect,
                                               std::chrono::milliseconds minBackoff)
    : connect_fn_(std::move(connect))
    , disconnect_fn_(std::move(disconnect))
    , min_backoff_(minBackoff)
{}

inline WsReconnectSession::~WsReconnectSession() {
    stop();
}

inline void WsReconnectSession::start() {
    connect_fn_();
    worker_ = std::thread([this]{ workerLoop(); });
}

inline void WsReconnectSession::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }
    cv_.notify_all();

    if (worker_.joinable())
        worker_.join();

    // DisconnectFn must not throw (reconnect_session.hpp). This catch defends that
    // contract and is not an error path: stop() runs from the destructor, which
    // must not throw, and the session has no error channel to report to.
    try { disconnect_fn_(); } catch (...) {}  // NOLINT(bugprone-empty-catch)
}

inline void WsReconnectSession::scheduleReconnect(const std::string& /*reason*/) {
    if (stop_requested_.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        reconnect_requested_.store(true, std::memory_order_relaxed);
    }
    cv_.notify_all();
}

inline void WsReconnectSession::workerLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return stop_requested_.load(std::memory_order_relaxed)
                    || reconnect_requested_.load(std::memory_order_relaxed);
            });
            if (stop_requested_.load(std::memory_order_relaxed)) return;
            reconnect_requested_.store(false, std::memory_order_relaxed);
        }

        auto backoff = min_backoff_;

        while (true) {
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, backoff, [this] {
                    return stop_requested_.load(std::memory_order_relaxed);
                });
                if (stop_requested_.load(std::memory_order_relaxed)) return;
            }

            // As in stop(): DisconnectFn must not throw, so this catch defends that
            // contract. A throw that escaped would end this thread in std::terminate,
            // and there is no error channel here, so the reconnect goes on.
            try { disconnect_fn_(); } catch (...) {}  // NOLINT(bugprone-empty-catch)

            try {
                connect_fn_();
                break;
            } catch (...) {
                backoff = std::min(backoff * 2, kMaxBackoff);
            }
        }
    }
}

} // namespace exchange::ws
