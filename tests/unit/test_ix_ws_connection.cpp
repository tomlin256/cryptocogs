// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// IxWsConnection against a loopback ix::WebSocketServer (plan transport-callback-lifetimes, #39).
//
// What the destructor promises about the callbacks and the I/O thread. IxWsConnection declares its socket before its four
// callbacks, so on its own the callbacks are destroyed first, and the I/O thread then delivers the final Close (and any
// frame in flight) to a destroyed std::function. These tests drop a connection that is still open and ask the CallbackProbe
// whether any callback was called after one was destroyed.
//
// Real sockets on 127.0.0.1, no network. No sleeps and no timeouts: every wait is on a gate that the I/O thread opens, and a
// wait that never ends is stopped by the test preset's 120 s timeout.
//
// ixwebsocket does not always deliver the final Close. stop() sets a flag and then closes; the I/O thread leaves its loop at
// once if it sees the flag before the socket is closing, and then no handshake runs and no Close is delivered. That happens
// when stop() lands while the thread is still on its way back from a callback, which is where a test that has just waited on
// a gate opened by that callback puts it: about 4 in 100 runs on the Mac (measured, plan transport-callback-lifetimes). So no
// assertion here depends on a Close having been delivered, and the one test that needs one (it is what makes a late call
// possible) repeats the attempt until an attempt has had one.

#include "exchange/common/ix_ws_connection.hpp"
#include "callback_probe.hpp"

#include <gtest/gtest.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <unistd.h>

#include <atomic>
#include <future>
#include <memory>
#include <string>

using exchange::ws::IWsConnection;
using exchange::ws::IxWsConnection;

namespace {

// Opens once; wait() blocks until it has. Many waiters are fine, and so are many open() calls.
class Gate {
public:
    void open() {
        if (!opened_.exchange(true))
            promise_.set_value();
    }
    void wait() const { future_.wait(); }

private:
    std::atomic<bool>        opened_{false};
    std::promise<void>       promise_;
    std::shared_future<void> future_{promise_.get_future().share()};
};

// What the callbacks tell the test. They reach it through g_signals and not through a capture, because on the code these
// tests hunt, the callbacks run after they were destroyed (see callback_probe.hpp).
struct Signals {
    Gate              opened;
    Gate              got_message;
    std::atomic<bool> message_seen{false};
    std::string       message;          // written once, before got_message opens
    std::atomic<int>  close_calls{0};
};

Signals* g_signals = nullptr;

IWsConnection::MessageCb message_cb() {
    return [token = CallbackProbe::token()](const std::string& msg) {
        CallbackProbe::record_call();
        if (!g_signals->message_seen.exchange(true)) {
            g_signals->message = msg;
            g_signals->got_message.open();
        }
    };
}

IWsConnection::OpenCb open_cb() {
    return [token = CallbackProbe::token()]() {
        CallbackProbe::record_call();
        g_signals->opened.open();
    };
}

IWsConnection::CloseCb close_cb() {
    return [token = CallbackProbe::token()](const std::string&) {
        CallbackProbe::record_call();
        g_signals->close_calls.fetch_add(1);
    };
}

IWsConnection::ErrorCb error_cb() {
    return [token = CallbackProbe::token()](const std::string&) {
        CallbackProbe::record_call();
    };
}

void install_callbacks(IxWsConnection& conn) {
    conn.set_on_message(message_cb());
    conn.set_on_open(open_cb());
    conn.set_on_close(close_cb());
    conn.set_on_error(error_cb());
}

// A WebSocket server on 127.0.0.1 that sends "hello" to every client that connects. ixwebsocket cannot report the port it
// bound when asked for port 0, so this scans upward from a port derived from the process id until one binds; two test
// processes under ctest -j then settle on different ports without any randomness.
class LoopbackServer {
public:
    LoopbackServer() {
        const int first_port = 49152 + static_cast<int>(::getpid() % 16000);
        for (int attempt = 0; attempt < 200 && !server_; ++attempt) {
            const int port = first_port + attempt;
            auto candidate = std::make_unique<ix::WebSocketServer>(port, "127.0.0.1");
            candidate->setOnClientMessageCallback(
                [](const std::shared_ptr<ix::ConnectionState>&, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
                    if (msg->type == ix::WebSocketMessageType::Open)
                        ws.send("hello");
                });
            if (candidate->listenAndStart()) {
                port_   = port;
                server_ = std::move(candidate);
            }
        }
    }

    ~LoopbackServer() {
        if (server_)
            server_->stop();
    }

    LoopbackServer(const LoopbackServer&)            = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    bool        running() const { return server_ != nullptr; }
    std::string url() const     { return "ws://127.0.0.1:" + std::to_string(port_); }

private:
    std::unique_ptr<ix::WebSocketServer> server_{};
    int                                  port_{0};
};

class IxWsConnectionTest : public ::testing::Test {
protected:
    IxWsConnectionTest()           { fresh_attempt(); }
    ~IxWsConnectionTest() override { g_signals = nullptr; }

    // A new set of signals and a new probe count, for the next connection. Only between connections: nothing of the last
    // one may still be able to call a callback.
    void fresh_attempt() {
        signals_ = std::make_unique<Signals>();
        g_signals = signals_.get();
        probe_.reset();
    }

    // Connects a fresh connection and returns once it is open and has received the server's "hello".
    std::shared_ptr<IxWsConnection> open_connection() {
        auto conn = std::make_shared<IxWsConnection>(server_.url());
        install_callbacks(*conn);
        conn->connect();
        signals_->opened.wait();
        signals_->got_message.wait();
        return conn;
    }

    CallbackProbe            probe_;
    std::unique_ptr<Signals> signals_;
    LoopbackServer           server_;
};

} // namespace

// The harness works: the connection opens and the frame reaches the message callback. Without this, the tests below could pass
// because nothing ever connected.
TEST_F(IxWsConnectionTest, DeliversAFrameToTheMessageCallback) {
    ASSERT_TRUE(server_.running());
    const auto conn = open_connection();
    EXPECT_EQ(signals_->message, "hello");
    EXPECT_TRUE(conn->is_connected());
    conn->disconnect();
}

// The regression test for #39. The connection is dropped while it is open and nobody calls disconnect(). The destructor has to
// stop the socket, which delivers the final Close, while the callbacks are still alive. An attempt in which ixwebsocket
// delivered no Close (see the top of the file) could not have shown the bug, so it is repeated; the attempt that did deliver
// one must have seen no call after a destruction.
TEST_F(IxWsConnectionTest, DroppingAnOpenConnectionStopsItBeforeAnyCallbackIsDestroyed) {
    ASSERT_TRUE(server_.running());
    constexpr int max_attempts = 100;
    bool delivered_a_close = false;
    for (int attempt = 1; attempt <= max_attempts && !delivered_a_close; ++attempt) {
        fresh_attempt();
        {
            const auto conn = open_connection();
        }
        EXPECT_EQ(probe_.calls_after_a_destruction(), 0) << "attempt " << attempt;
        EXPECT_EQ(probe_.destroyed(), 4) << "attempt " << attempt;     // the four callbacks, and nothing else
        EXPECT_LE(signals_->close_calls.load(), 1) << "attempt " << attempt;
        delivered_a_close = signals_->close_calls.load() == 1;
    }
    EXPECT_TRUE(delivered_a_close) << "no attempt in " << max_attempts << " delivered a Close, so none could show the bug";
}

// disconnect() first (what the Crypto.com and Coinbase examples do), then the drop: the destructor's stop() is a second one
// and must not deliver a Close of its own or call anything after a destruction.
TEST_F(IxWsConnectionTest, DisconnectThenDropDeliversNoSecondClose) {
    ASSERT_TRUE(server_.running());
    int closes_after_disconnect = -1;
    {
        const auto conn = open_connection();
        conn->disconnect();
        closes_after_disconnect = signals_->close_calls.load();
        EXPECT_LE(closes_after_disconnect, 1);
        EXPECT_FALSE(conn->is_connected());
    }
    EXPECT_EQ(signals_->close_calls.load(), closes_after_disconnect);
    EXPECT_EQ(probe_.calls_after_a_destruction(), 0);
    EXPECT_EQ(probe_.destroyed(), 4);
}

// No connect(), so no thread and nothing to stop: the destructor returns, and no callback runs.
TEST_F(IxWsConnectionTest, DroppingAConnectionThatWasNeverConnectedReturns) {
    {
        IxWsConnection conn("ws://127.0.0.1:1");
        install_callbacks(conn);
    }
    EXPECT_EQ(probe_.calls(), 0);
    EXPECT_EQ(probe_.destroyed(), 4);
    EXPECT_EQ(signals_->close_calls.load(), 0);
}
