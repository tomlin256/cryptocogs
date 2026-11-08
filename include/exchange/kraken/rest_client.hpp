// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/kraken/rest_client.hpp
// Type-safe HTTP executor for the Kraken REST API. The libcurl transport is the
// shared exchange::rest::CurlHttpClient (plan shared-curl-http-transport); this class only binds the
// Kraken envelope/parse. Kraken reads errors from the JSON envelope, so it
// ignores the HTTP status the transport returns.
//
// Namespace: exchange::kraken::rest

#include "exchange/kraken/rest_api.hpp"
#include "exchange/common/http_client.hpp"

#include <functional>
#include <memory>
#include <type_traits>

namespace exchange::kraken::rest {

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpResponse;
using exchange::rest::HttpTimeouts;

class KrakenRestClient {
public:
    // Production constructor — uses the shared libcurl transport. `timeouts` bound every call (HttpTimeouts, #56): a call that exceeds
    // them comes back with ok == false and "request failed: curl_easy_perform failed: Timeout was reached …" in errors, and that text
    // says whether the request was sent. Throws std::invalid_argument if a limit is not positive.
    explicit KrakenRestClient(std::string base_url = "https://api.kraken.com", HttpTimeouts timeouts = {});

    KrakenRestClient(const KrakenRestClient&) = delete;
    KrakenRestClient& operator=(const KrakenRestClient&) = delete;

    // Execute a public request (no credentials required). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PublicRequest, Req>>>
    exchange::kraken::RestResponse<typename Req::response_type>
    execute(const Req& req);

    // Execute a private request (credentials required). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PrivateRequest, Req>>>
    exchange::kraken::RestResponse<typename Req::response_type>
    execute(const Req& req, const KrakenCredentials& creds);

private:
    // Test constructor — injects a body-only performer (Kraken keys errors off
    // the envelope, not the HTTP status, so tests only ever supply a body).
    explicit KrakenRestClient(std::function<std::string(const HttpRequest&)> performer);

    friend KrakenRestClient make_test_client(std::function<std::string(const HttpRequest&)>);

    std::shared_ptr<CurlHttpClient>                 transport_;  // null in tests
    std::function<HttpResponse(const HttpRequest&)> perform_;
};

// Factory used by unit tests to inject a mock HTTP performer (body only).
// Defined in src/kraken/rest_client.cpp.
KrakenRestClient make_test_client(std::function<std::string(const HttpRequest&)> fn);

} // namespace exchange::kraken::rest

#include "exchange/kraken/rest_client.inl"
