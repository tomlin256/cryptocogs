// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/rest_client.hpp"

#include <memory>
#include <utility>

namespace exchange::kraken::rest {

// Production constructor — wires perform_ to the shared CurlHttpClient transport.
// (The libcurl handling itself lives in exchange::rest::CurlHttpClient — plan 010.)
// `timeouts` go to the transport, which applies them to every call (plan 042, #56).
KrakenRestClient::KrakenRestClient(std::string base_url, const HttpTimeouts timeouts)
    : transport_(std::make_shared<CurlHttpClient>(std::move(base_url), timeouts))
    , perform_([t = transport_](const HttpRequest& h) { return t->perform(h); })
{}

// Test constructor — adapts a body-only performer to the {status, body} shape
// the client uses internally (status is irrelevant to Kraken; fixed at 200).
KrakenRestClient::KrakenRestClient(std::function<std::string(const HttpRequest&)> performer)
    : perform_([fn = std::move(performer)](const HttpRequest& h) {
          return HttpResponse{200, fn(h)};
      })
{}

// Factory used by unit tests to inject a mock HTTP performer (body only).
KrakenRestClient make_test_client(std::function<std::string(const HttpRequest&)> fn) {
    return KrakenRestClient(std::move(fn));
}

} // namespace exchange::kraken::rest
