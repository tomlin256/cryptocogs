// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/ws_client.hpp"

#include <optional>
#include <stdexcept>

namespace exchange::ws {

// SubscriptionHandle and RateLimitedWsErrorHandler are implemented in ws.cpp.

// ─────────────────────────────────────────────────────────────────────────────
// ExchangeWsClient
// ─────────────────────────────────────────────────────────────────────────────

void ExchangeWsClient::init() {
    if (!error_handler_)
        error_handler_ = std::make_shared<RateLimitedWsErrorHandler>();

    if (conn_->is_connected())
        connected_.store(true);

    auto weak_self = std::weak_ptr<ExchangeWsClient>(shared_from_this());

    conn_->set_on_message([weak_self](const std::string& raw) {
        if (auto self = weak_self.lock()) self->on_raw_message(raw);
    });
    conn_->set_on_open([weak_self]() {
        if (auto self = weak_self.lock()) self->on_open_handler();
    });
    conn_->set_on_close([weak_self](std::string reason) {
        if (auto self = weak_self.lock()) {
            self->connected_.store(false);
            if (self->disconnect_cb_) self->disconnect_cb_(std::move(reason));
        }
    });
    conn_->set_on_error([weak_self](const std::string& reason) {
        if (auto self = weak_self.lock())
            self->error_handler_->on_connection_error(reason);
    });
}

void ExchangeWsClient::set_on_disconnect(std::function<void(std::string)> cb) {
    disconnect_cb_ = std::move(cb);
}

int64_t ExchangeWsClient::gen_req_id() { return next_req_id_.fetch_add(1); }

void ExchangeWsClient::cancel_subscription(const std::string& route_key,
                                            const std::string& unsub_json) {
    {
        std::lock_guard<std::mutex> lk(subs_mu_);
        subscriptions_.erase(route_key);
    }
    enqueue_or_send(unsub_json);
}

void ExchangeWsClient::enqueue_or_send(const std::string& msg) {
    std::lock_guard<std::mutex> lk(queue_mu_);
    if (connected_.load()) {
        conn_->send(msg);
    } else {
        send_queue_.push_back(msg);
    }
}

void ExchangeWsClient::on_open_handler() {
    // Flush the backlog while holding queue_mu_ (review L1): a concurrent
    // enqueue_or_send that sees connected_==true must block here and send AFTER
    // the backlog, not race ahead of it. enqueue_or_send already sends under the
    // same lock, so this introduces no new lock-ordering risk.
    std::lock_guard<std::mutex> lk(queue_mu_);
    connected_.store(true);
    for (const auto& msg : send_queue_)
        conn_->send(msg);
    send_queue_.clear();
}

void ExchangeWsClient::on_raw_message(const std::string& raw) {
    json j;
    try { j = json::parse(raw); }
    catch (const std::exception& e) {
        error_handler_->on_malformed_frame(raw, e);
        return;
    }

    // The identifier is supplied by the adapter and runs on this thread like the handlers below, so a throw from it ends the
    // process the same way and is contained the same way: report the frame, drop it and go on (plan 034).
    const std::optional<FrameDescriptor> classified = [&]() -> std::optional<FrameDescriptor> {
        try {
            return identifier_(j);
        } catch (const std::exception& e) {
            error_handler_->on_malformed_frame(raw, e);
        } catch (...) {
            const std::runtime_error e("non-std::exception thrown by the MessageIdentifier");
            error_handler_->on_malformed_frame(raw, e);
        }
        return std::nullopt;
    }();
    if (!classified)
        return;
    const FrameDescriptor& desc = *classified;

    // A dispatched handler/callback runs user-supplied from_json + callbacks,
    // any of which can throw (unexpected field type, std::stod on a non-number,
    // a throwing user lambda). This runs on the transport's receive thread, so
    // an escaped exception would std::terminate the process. Isolate it: report
    // via the error handler and keep the dispatch loop alive. The typed futures
    // additionally resolve themselves to ok=false (see ws_client.inl) so callers
    // see a clean error rather than a broken promise.
    auto safe_invoke = [&](const std::function<void(const json&)>& fn) {
        try {
            fn(j);
        } catch (const std::exception& e) {
            error_handler_->on_malformed_frame(raw, e);
        } catch (...) {
            const std::runtime_error e("non-std::exception thrown by WS dispatch handler");
            error_handler_->on_malformed_frame(raw, e);
        }
    };

    switch (desc.kind) {
        case FrameKind::MethodResponse: {
            if (!desc.correlation_id.has_value()) break;
            const auto& cid = *desc.correlation_id;
            std::function<void(const json&)> handler;
            {
                std::lock_guard<std::mutex> lk(pending_mu_);
                auto it = pending_.find(cid);
                if (it != pending_.end()) {
                    handler = std::move(it->second);
                    pending_.erase(it);
                }
            }
            if (handler) safe_invoke(handler);
            break;
        }
        case FrameKind::PushMessage: {
            const auto& rk = desc.route_key;
            std::function<void(const json&)> cb;
            {
                std::lock_guard<std::mutex> lk(subs_mu_);
                auto it = subscriptions_.find(rk);
                if (it != subscriptions_.end()) cb = it->second;
            }
            if (cb) safe_invoke(cb);
            break;
        }
        case FrameKind::Unknown:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory (conn-based)
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<ExchangeWsClient>
make_exchange_ws_client(std::shared_ptr<IWsConnection>   conn,
                        MessageIdentifier                identifier,
                        std::shared_ptr<IWsErrorHandler> error_handler) {
    auto client = std::make_shared<ExchangeWsClient>(
        std::move(conn), std::move(identifier), std::move(error_handler));
    client->init();
    return client;
}

} // namespace exchange::ws
