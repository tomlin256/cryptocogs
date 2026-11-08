// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/binance/rest_client.hpp"

#include <memory>
#include <utility>

namespace exchange::binance::rest {

// Production constructor — wires perform_ to the shared CurlHttpClient transport.
// (The libcurl handling itself lives in exchange::rest::CurlHttpClient — plan shared-curl-http-transport.)
// `timeouts` go to the transport, which applies them to every call (plan curl-timeouts, #56).
BinanceRestClient::BinanceRestClient(std::string base_url, const HttpTimeouts timeouts)
    : transport_(std::make_shared<CurlHttpClient>(std::move(base_url), timeouts))
    , perform_([t = transport_](const HttpRequest& h) { return t->perform(h); })
{}

// Test constructor — uses an injected performer; no transport is created.
BinanceRestClient::BinanceRestClient(
    std::function<HttpResponse(const HttpRequest&)> performer)
    : perform_(std::move(performer))
{}

// Factory used by unit tests to inject a mock HTTP performer.
BinanceRestClient
make_binance_test_client(std::function<HttpResponse(const HttpRequest&)> fn) {
    return BinanceRestClient(std::move(fn));
}

} // namespace exchange::binance::rest
