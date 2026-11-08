// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/coinbase/rest_client.hpp
// Type-safe HTTP executor for the Coinbase Exchange REST API. The libcurl
// transport is the shared exchange::rest::CurlHttpClient (plan shared-curl-http-transport); this class
// only binds the Coinbase envelope/parse and auth. Coinbase rejects a request
// with no User-Agent ("User-Agent header is required"), so execute() sets one
// unless the request already carries it (rest_client.inl).
//
// Namespace: exchange::coinbase::rest

#include "exchange/coinbase/rest_api.hpp"
#include "exchange/coinbase/auth.hpp"
#include "exchange/common/http_client.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace exchange::coinbase::rest {

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpResponse;
using exchange::rest::HttpTimeouts;

class CoinbaseRestClient {
public:
    // Production constructor — uses the shared libcurl transport. `timeouts` bound every call (HttpTimeouts, #56): a call that exceeds
    // them comes back with ok == false and "request failed: curl_easy_perform failed: Timeout was reached …" in errors, and that text
    // says whether the request was sent. Throws std::invalid_argument if a limit is not positive.
    explicit CoinbaseRestClient(
        std::string base_url = "https://api.exchange.coinbase.com", HttpTimeouts timeouts = {});

    CoinbaseRestClient(const CoinbaseRestClient&)            = delete;
    CoinbaseRestClient& operator=(const CoinbaseRestClient&) = delete;

    // Execute a public request (no credentials). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PublicRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req);

    // Execute a private request — CoinbaseAuth injects the CB-ACCESS-* headers.
    // Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PrivateRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req, const CoinbaseCredentials& creds);

private:
    // Test constructor — injects a custom performer so unit tests run without curl.
    explicit CoinbaseRestClient(
        std::function<HttpResponse(const HttpRequest&)> performer);

    friend CoinbaseRestClient make_coinbase_test_client(
        std::function<HttpResponse(const HttpRequest&)>);

    std::shared_ptr<CurlHttpClient>                 transport_;  // null in tests
    std::function<HttpResponse(const HttpRequest&)> perform_;
};

// Factory used by unit tests to inject a mock HTTP performer.
// Defined in src/coinbase/rest_client.cpp.
CoinbaseRestClient
make_coinbase_test_client(std::function<HttpResponse(const HttpRequest&)> fn);

} // namespace exchange::coinbase::rest

#include "exchange/coinbase/rest_client.inl"
