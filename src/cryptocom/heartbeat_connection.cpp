// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/cryptocom/heartbeat_connection.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace exchange::cryptocom::ws {

using json = nlohmann::json;

namespace {

// True for a heartbeat: a JSON object whose "method" is the string "public/heartbeat". Anything else is not one, an object whose
// "method" is missing or is not a string included. This runs on the transport's I/O thread, where nothing catches an exception, so
// it must not throw: json::value() does, type_error.302, for a "method" that is not a string (#44).
bool is_heartbeat(const json& j) {
    if (!j.is_object())
        return false;
    const auto method = j.find("method");
    return method != j.end() && method->is_string() && method->get_ref<const std::string&>() == "public/heartbeat";
}

} // namespace

// What the callback installed on the inner connection owns. That callback lives inside the connection and can outlive this
// responder (the factories give the client the only reference to it, while the caller keeps the connection), so it must not
// refer to the responder: it carries its own copy of the forward callback and a weak reference to the connection it answers
// on. Weak, because the callback is a member of that connection, and a strong reference would be a cycle nothing could break
// (plan transport-callback-lifetimes, #43).
struct HeartbeatResponder::Relay {
    std::weak_ptr<IWsConnection> inner;
    MessageCb                    forward;

    void on_message(const std::string& raw) const;
};

void HeartbeatResponder::Relay::on_message(const std::string& raw) const {
    // Answer + swallow heartbeats; forward everything else (including frames that
    // fail to parse, and frames whose "method" is missing or is not a string — the
    // client's own error handling deals with those).
    const json j = json::parse(raw, nullptr, /*allow_exceptions=*/false);
    if (is_heartbeat(j)) {
        json reply;
        reply["id"]     = j.contains("id") ? j.at("id") : json(nullptr);
        reply["method"] = "public/respond-heartbeat";
        if (const auto conn = inner.lock())
            conn->send(reply.dump());
        return;
    }
    if (forward)
        forward(raw);
}

HeartbeatResponder::HeartbeatResponder(std::shared_ptr<IWsConnection> inner)
    : inner_(std::move(inner))
{}

void HeartbeatResponder::connect()              { inner_->connect(); }
void HeartbeatResponder::disconnect()           { inner_->disconnect(); }
bool HeartbeatResponder::is_connected() const   { return inner_->is_connected(); }
void HeartbeatResponder::send(const std::string& msg) { inner_->send(msg); }

void HeartbeatResponder::set_on_message(MessageCb cb) {
    const auto relay = std::make_shared<const Relay>(Relay{inner_, std::move(cb)});
    inner_->set_on_message([relay](const std::string& raw) { relay->on_message(raw); });
}

void HeartbeatResponder::set_on_open(OpenCb cb)   { inner_->set_on_open(std::move(cb)); }
void HeartbeatResponder::set_on_close(CloseCb cb) { inner_->set_on_close(std::move(cb)); }
void HeartbeatResponder::set_on_error(ErrorCb cb) { inner_->set_on_error(std::move(cb)); }

} // namespace exchange::cryptocom::ws
