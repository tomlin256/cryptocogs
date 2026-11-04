// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// MockWsConnection — shared test double for ExchangeWsClient suites (Kraken,
// Binance, Coinbase and Crypto.com). Avoids any real network I/O; tests can:
//   - Inspect outbound messages (conn->sent(), a copy taken under a lock)
//   - Wait for an outbound message from another thread (conn->wait_for_sent(n))
//   - Inject inbound server frames (conn->inject_message(...))
//   - Simulate connection open/close (conn->fire_open() / fire_close())
//
// The outbound log is safe to use from a second thread (plan 033, #27): send()
// appends under a lock and wakes the waiters, and sent() copies under the same
// lock. The log is private for that reason: the unlocked read from another
// thread that ThreadSanitizer reported in Execute.BlocksUntilResponse can no
// longer be written. The rest (the connected flag, the callbacks) is for the
// test thread, as before; inject_message() may be called from a second thread
// once the callbacks are set.

#pragma once

#include "exchange/common/ws.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class MockWsConnection : public exchange::ws::IWsConnection {
public:
    // connect() marks the connection as internally ready but does NOT fire
    // on_open automatically – tests call fire_open() for full control.
    void connect()    override { connected_ = true; }
    void disconnect() override { connected_ = false; }
    bool is_connected() const override { return connected_; }

    // Appends to the outbound log under the lock, and wakes every wait_for_sent().
    void send(const std::string& msg) override {
        {
            const std::lock_guard<std::mutex> lock(mu_);
            sent_.push_back(msg);
        }
        sent_cv_.notify_all();
    }

    // A copy of everything sent so far, in order, taken under the lock. A copy, so the caller can look at it while the log
    // goes on growing; the temporary lives to the end of the full expression, so conn->sent()[0] is fine.
    std::vector<std::string> sent() const {
        const std::lock_guard<std::mutex> lock(mu_);
        return sent_;
    }

    // Blocks until at least `count` messages have been sent, and returns true; or until release_waiters() has been called,
    // and returns whether `count` messages had been sent by then. No timeout and no polling: it waits on a condition
    // variable for its predicate, so it returns the moment the predicate is true, whichever thread gets there first. A
    // test that waits for a message that will never come is freed by release_waiters(), from the thread that knows so,
    // and by the test preset's timeout otherwise.
    bool wait_for_sent(const std::size_t count) {
        std::unique_lock<std::mutex> lock(mu_);
        sent_cv_.wait(lock, [this, count] { return sent_.size() >= count || released_; });
        return sent_.size() >= count;
    }

    // Wakes every thread in wait_for_sent(), now and later.
    void release_waiters() {
        {
            const std::lock_guard<std::mutex> lock(mu_);
            released_ = true;
        }
        sent_cv_.notify_all();
    }

    void set_on_message(MessageCb cb) override { msg_cb_  = std::move(cb); }
    void set_on_open(OpenCb cb)       override { open_cb_ = std::move(cb); }
    void set_on_close(CloseCb cb)     override { close_cb_= std::move(cb); }
    void set_on_error(ErrorCb cb)     override { error_cb_= std::move(cb); }

    // Test helpers
    void inject_message(const std::string& raw)  { if (msg_cb_)   msg_cb_(raw);    }
    void fire_open()                              { if (open_cb_)  open_cb_();      }
    void fire_close(std::string reason = "")      { if (close_cb_) close_cb_(std::move(reason)); }
    void fire_error(const std::string& reason)    { if (error_cb_) error_cb_(reason); }

private:
    bool      connected_{false};
    MessageCb msg_cb_;
    OpenCb    open_cb_;
    CloseCb   close_cb_;
    ErrorCb   error_cb_;

    // The outbound log and what guards it. mu_ is mutable because sent() is logically const but has to take the lock.
    mutable std::mutex       mu_;
    std::condition_variable  sent_cv_;
    std::vector<std::string> sent_;
    bool                     released_{false};
};
