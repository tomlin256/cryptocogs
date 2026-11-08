// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/binance/rest_client.hpp
// Type-safe HTTP executor for the Binance Spot REST API. The libcurl transport
// is the shared exchange::rest::CurlHttpClient (plan shared-curl-http-transport); this class only binds
// the Binance envelope/parse and auth.
//
// Namespace: exchange::binance::rest

#include "exchange/binance/rest_api.hpp"
#include "exchange/binance/auth.hpp"
#include "exchange/common/http_client.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace exchange::binance::rest {

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpResponse;
using exchange::rest::HttpTimeouts;

class BinanceRestClient {
public:
    // Production constructor — uses the shared libcurl transport. `timeouts` bound every call (HttpTimeouts, #56): a call that exceeds
    // them comes back with ok == false and "request failed: curl_easy_perform failed: Timeout was reached …" in errors, and that text
    // says whether the request was sent. Throws std::invalid_argument if a limit is not positive.
    explicit BinanceRestClient(std::string base_url = "https://api.binance.com", HttpTimeouts timeouts = {});

    BinanceRestClient(const BinanceRestClient&) = delete;
    BinanceRestClient& operator=(const BinanceRestClient&) = delete;

    // Execute a public request (no credentials required). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PublicRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req);

    // Execute a private request (BinanceAuth injects timestamp + signature).
    // Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PrivateRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req, const BinanceAuth& auth);

private:
    // Test constructor — injects a custom performer so unit tests run without curl.
    explicit BinanceRestClient(
        std::function<HttpResponse(const HttpRequest&)> performer);

    friend BinanceRestClient make_binance_test_client(
        std::function<HttpResponse(const HttpRequest&)>);

    std::shared_ptr<CurlHttpClient>                 transport_;  // null in tests
    std::function<HttpResponse(const HttpRequest&)> perform_;
};

// Factory used by unit tests to inject a mock HTTP performer.
// Defined in src/binance/rest_client.cpp.
BinanceRestClient
make_binance_test_client(std::function<HttpResponse(const HttpRequest&)> fn);

} // namespace exchange::binance::rest

#include "exchange/binance/rest_client.inl"
