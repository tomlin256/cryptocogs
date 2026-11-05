// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// exchange/common/ws_client.inl — inline and template method implementations
// for ExchangeWsClient.  Included at the bottom of ws_client.hpp; do not
// include directly.

#pragma once

namespace exchange::ws {

// Note: the non-template bodies for RateLimitedWsErrorHandler and
// SubscriptionHandle live in src/exchange/common/ws.cpp — this .inl is
// template-only.

// ─────────────────────────────────────────────────────────────────────────────
// detail::make_ws_response<T>
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

template<typename T>
WsResponse<T> make_ws_response(T r) {
    WsResponse<T> ws;
    if constexpr (std::is_base_of_v<BaseWsResponse, T>) {
        ws.ok    = r.success;
        ws.error = r.error;
    } else {
        ws.ok = true;
    }
    ws.result = std::move(r);
    return ws;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// ExchangeWsClient template methods
// ─────────────────────────────────────────────────────────────────────────────

template<typename Req>
std::future<WsResponse<typename Req::response_type>>
ExchangeWsClient::execute_async(Req req) {
    using Resp = typename Req::response_type;

    auto id    = gen_req_id();
    req.req_id = id;
    auto key   = std::to_string(id);

    auto prom = std::make_shared<std::promise<WsResponse<Resp>>>();
    auto fut  = prom->get_future();

    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_[key] = [prom](const json& j) {
            // Resp::from_json may throw on an unexpected frame shape; resolve the
            // future to a clean ok=false rather than leaving a broken promise
            // (and the on_raw_message guard keeps the receive thread alive).
            try {
                prom->set_value(detail::make_ws_response(Resp::from_json(j)));
            } catch (const std::exception& e) {
                WsResponse<Resp> err;
                err.ok    = false;
                err.error = std::string("response parse failed: ") + e.what();
                prom->set_value(std::move(err));
            }
        };
    }

    enqueue_or_send(req.to_json().dump());
    return fut;
}

template<typename Req>
WsResponse<typename Req::response_type>
ExchangeWsClient::execute(Req req, std::chrono::milliseconds timeout) {
    auto fut = execute_async(std::move(req));
    if (fut.wait_for(timeout) == std::future_status::timeout) {
        WsResponse<typename Req::response_type> err;
        err.ok    = false;
        err.error = "request timed out";
        return err;
    }
    return fut.get();
}

template<typename Req>
std::future<std::pair<WsResponse<typename Req::response_type>, SubscriptionHandle>>
ExchangeWsClient::subscribe_async(Req req,
                                   std::function<void(typename Req::push_type)> callback) {
    using AckType = typename Req::response_type;
    using PushMsg = typename Req::push_type;

    auto id    = gen_req_id();
    req.req_id = id;
    auto key   = std::to_string(id);

    // Not const, unlike the other locals: the ack handler below takes it by move,
    // and a const member would make that closure's implicit move constructor
    // copy it, which can throw (bugprone-exception-escape).
    std::string rk       = req.route_key();
    std::string unsub_js = req.unsubscribe_json().dump();

    // `callback` is a sink: the by-value parameter is moved into the closure.
    auto erased_push = std::make_shared<std::function<void(const json&)>>(
        [callback = std::move(callback)](const json& j) { callback(PushMsg::from_json(j)); }
    );

    auto active = std::make_shared<std::atomic<bool>>(false);

    auto prom = std::make_shared<
        std::promise<std::pair<WsResponse<AckType>, SubscriptionHandle>>>();
    auto fut = prom->get_future();

    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_[key] = [this, prom, rk = std::move(rk), erased_push, active,
                         unsub_js = std::move(unsub_js)](const json& j) mutable
        {
            // A malformed ack frame must not throw into the receive thread or
            // leave the subscribe future broken — resolve it to ok=false (no
            // subscription installed, inactive handle).
            WsResponse<AckType> ws;
            try {
                ws = detail::make_ws_response(AckType::from_json(j));
            } catch (const std::exception& e) {
                ws.ok    = false;
                ws.error = std::string("subscribe ack parse failed: ") + e.what();
            }

            SubscriptionHandle handle;
            if (ws.ok) {
                active->store(true);
                {
                    std::lock_guard<std::mutex> slk(subs_mu_);
                    subscriptions_[rk] = *erased_push;
                }
                handle = SubscriptionHandle(
                    active,
                    std::weak_ptr<ExchangeWsClient>(shared_from_this()),
                    rk,
                    std::move(unsub_js)
                );
            }
            prom->set_value({std::move(ws), std::move(handle)});
        };
    }

    enqueue_or_send(req.to_json().dump());
    return fut;
}

template<typename Req>
std::pair<WsResponse<typename Req::response_type>, SubscriptionHandle>
ExchangeWsClient::subscribe(Req req,
                             std::function<void(typename Req::push_type)> callback,
                             std::chrono::milliseconds timeout) {
    auto fut = subscribe_async(std::move(req), std::move(callback));
    if (fut.wait_for(timeout) == std::future_status::timeout) {
        WsResponse<typename Req::response_type> err;
        err.ok    = false;
        err.error = "subscribe timed out";
        return {std::move(err), SubscriptionHandle{}};
    }
    return fut.get();
}

} // namespace exchange::ws
