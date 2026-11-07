// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/ix_ws_connection.hpp
// IxWsConnection — ixwebsocket concrete implementation of IWsConnection.
// Also provides the URL-based make_exchange_ws_client() overload.
//
// Include this header (in addition to ws_client.hpp) when you want the real
// ixwebsocket transport.  Unit tests that use a mock connection should include
// only ws_client.hpp to avoid pulling in ixwebsocket.
//
// DECLARATIVE-HEADER CARVE-OUT (plan 016, decision D1): unlike every other
// header, this one keeps its implementation inline. It is the only header that
// #includes <ixwebsocket/...>, and ixwebsocket is (a) fetched only under
// CRYPTOCOGS_BUILD_TESTS and (b) deliberately not installed (plan 012). Moving
// these bodies to a .cpp would force a library that links ixwebsocket and would
// break downstream consumers that use IxWsConnection header-only (linking their
// own ixwebsocket). Keeping it header-only preserves the "core static libs are
// ixwebsocket-free" invariant and the install contract.

#include "exchange/common/ws_client.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketHttpHeaders.h>

#include <map>
#include <string>

namespace exchange::ws {

// ─────────────────────────────────────────────────────────────────────────────
// IxWsConnection  —  ixwebsocket implementation of IWsConnection
// ─────────────────────────────────────────────────────────────────────────────

class IxWsConnection : public IWsConnection {
public:
    // extra_headers lets a caller add or override handshake headers. The only
    // override that currently matters is "Host": ixwebsocket otherwise emits
    // "Host: <host>:443" for wss, which some gateways (Crypto.com) reject with
    // HTTP 400 — supplying "Host" suppresses ixwebsocket's auto value.
    explicit IxWsConnection(std::string url,
                            std::map<std::string, std::string> extra_headers = {})
        : url_(std::move(url)), extra_headers_(std::move(extra_headers)) {}

    // Stops the socket first. ix::WebSocket::stop() joins the I/O thread, and the thread normally ends by delivering a Close
    // message to the callback below, so all four std::functions must still be alive for it, and for any frame still in
    // flight. Members are destroyed in reverse order and ws_ is declared before them: without this destructor ~ix::WebSocket
    // would stop the thread only after the callbacks were gone, and the thread would call destroyed std::functions
    // (plan 034, #39). A final on_close may therefore run on the I/O thread while this destructor runs, exactly as it does in
    // disconnect() (ixwebsocket skips it when stop() lands between two iterations of the thread's loop), and whatever the
    // callbacks capture must outlive the connection. stop() is idempotent, so disconnect() followed by destruction is
    // unchanged. If the I/O thread itself drops the last reference to this object it joins itself: see the ownership warning
    // on the URL factory below.
    ~IxWsConnection() override { ws_.stop(); }

    void connect() override {
        ws_.setUrl(url_);
        if (!extra_headers_.empty())
            ws_.setExtraHeaders(
                ix::WebSocketHttpHeaders(extra_headers_.begin(), extra_headers_.end()));
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            switch (msg->type) {
                case ix::WebSocketMessageType::Open:
                    if (open_cb_)  open_cb_();
                    break;
                case ix::WebSocketMessageType::Close: {
                    if (close_cb_) {
                        std::string reason = "[code " +
                            std::to_string(msg->closeInfo.code) + "]";
                        if (!msg->closeInfo.reason.empty())
                            reason += " " + msg->closeInfo.reason;
                        close_cb_(std::move(reason));
                    }
                    break;
                }
                case ix::WebSocketMessageType::Message:
                    if (msg_cb_)   msg_cb_(msg->str);
                    break;
                case ix::WebSocketMessageType::Error:
                    if (error_cb_) error_cb_(msg->errorInfo.reason);
                    break;
                default:
                    break;
            }
        });
        ws_.start();
    }

    void disconnect() override { ws_.stop(); }

    bool is_connected() const override {
        return ws_.getReadyState() == ix::ReadyState::Open;
    }

    void send(const std::string& msg) override { ws_.send(msg); }

    void set_on_message(MessageCb cb) override { msg_cb_   = std::move(cb); }
    void set_on_open(OpenCb cb)       override { open_cb_  = std::move(cb); }
    void set_on_close(CloseCb cb)     override { close_cb_ = std::move(cb); }
    void set_on_error(ErrorCb cb)     override { error_cb_ = std::move(cb); }

private:
    std::string                        url_;
    std::map<std::string, std::string> extra_headers_;
    ix::WebSocket                      ws_;
    MessageCb     msg_cb_;
    OpenCb        open_cb_;
    CloseCb       close_cb_;
    ErrorCb       error_cb_;
};

// ─────────────────────────────────────────────────────────────────────────────
// URL-based factory
// ─────────────────────────────────────────────────────────────────────────────
//
// Creates a fresh IxWsConnection, calls init() and connect().
// Phase 1 (on_open) fires asynchronously; subscribe/execute calls made before
// it fires are queued internally and flushed atomically on connect.
//
// ⚠ OWNERSHIP WARNING — prefer the conn-based factory for anything long-lived.
//
// This overload keeps the IxWsConnection alive solely through the returned
// client: the local `conn` below dies at return, so ExchangeWsClient::conn_ is
// the *only* owner of the connection, and therefore of its ix::WebSocket.
//
// That matters because ~ix::WebSocket calls stop(), which JOINS the socket's own
// I/O thread — so destroying it *from* that thread is a self-join (EDEADLK,
// "thread::join failed: Resource deadlock avoided"). And every inbound callback
// registered by ExchangeWsClient::init takes a temporary strong reference to the
// client on exactly that thread (`if (auto self = weak_self.lock())`). So if a
// consumer releases its client while a callback is in flight, the I/O thread is
// left holding the last reference, releases it at callback scope exit, and
// destroys the socket on itself. The exception escapes the thread function and
// terminates the process.
//
// Any consumer whose connection can drop or reconnect while it is running should
// instead:
//
//   1. build the IxWsConnection itself and keep a shared_ptr to it,
//   2. wrap it with make_exchange_ws_client(conn, identifier, error_handler),
//   3. register callbacks, then call conn->connect(),
//   4. on teardown: call conn->disconnect() first — it joins the I/O thread on
//      the *caller's* thread, so no callback can run, or hold a reference, once
//      it returns — and only then release the client and conn, in either order.
//
// After disconnect() returns, the socket is guaranteed to be destroyed on the
// caller's thread.
//
// Dropping conn without disconnect() is safe as well, as long as the drop happens
// on a thread of your own: ~IxWsConnection() stops the socket before it destroys
// the callbacks (plan 034, #39). What it cannot do is stop the socket from the I/O
// thread itself, which is the self-join described above.
//
// This overload remains fine for short-lived, single-shot request/response use
// where no callback can be in flight at teardown.

inline std::shared_ptr<ExchangeWsClient>
make_exchange_ws_client(const std::string&               url,
                        MessageIdentifier                 identifier,
                        std::shared_ptr<IWsErrorHandler>  error_handler = nullptr) {
    auto conn   = std::make_shared<IxWsConnection>(url);
    auto client = std::make_shared<ExchangeWsClient>(
        conn, std::move(identifier), std::move(error_handler));
    client->init();
    conn->connect();
    return client;
}

} // namespace exchange::ws
