// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/cryptocom/rest_client.hpp
// Type-safe HTTP executor for the Crypto.com Exchange v1 REST API. The libcurl
// transport is the shared exchange::rest::CurlHttpClient (plan shared-curl-http-transport); this class
// binds the {code,result} envelope/parse and, for private requests, the
// in-body signing.
//
// Signing is Kraken-style: a private request signs ITSELF in build(creds) (the
// `sig` lives in the JSON body and depends on params), so there is no IRestAuth.
// execute(req, creds) copies the request, assigns a unique correlation `id`, and
// calls build(creds). Crypto.com keys errors off the envelope `code` AND the HTTP
// status, so the parse reads both.
//
// Not thread-safe (one client per thread, like CurlHttpClient).
//
// Namespace: exchange::cryptocom::rest

#include "exchange/cryptocom/auth.hpp"
#include "exchange/cryptocom/rest_api.hpp"
#include "exchange/common/http_client.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

namespace exchange::cryptocom::rest {

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpResponse;
using exchange::rest::HttpTimeouts;

class CryptoComRestClient {
public:
    // Production constructor — uses the shared libcurl transport. `timeouts` bound every call (HttpTimeouts, #56): a call that exceeds
    // them comes back with ok == false and "request failed: curl_easy_perform failed: Timeout was reached …" in errors, and that text
    // says whether the request was sent. Throws std::invalid_argument if a limit is not positive.
    explicit CryptoComRestClient(std::string base_url = "https://api.crypto.com", HttpTimeouts timeouts = {});

    CryptoComRestClient(const CryptoComRestClient&)            = delete;
    CryptoComRestClient& operator=(const CryptoComRestClient&) = delete;

    // Execute a public request (no credentials). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PublicRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req);

    // Execute a private request — the request signs its own envelope in
    // build(creds). Defined in rest_client.inl.
    template<typename Req,
             typename = std::enable_if_t<std::is_base_of_v<PrivateRequest, Req>>>
    exchange::rest::RestResponse<typename Req::response_type>
    execute(const Req& req, const CryptoComCredentials& creds);

private:
    // Test constructor — injects a custom performer so unit tests run without curl.
    explicit CryptoComRestClient(
        std::function<HttpResponse(const HttpRequest&)> performer);

    friend CryptoComRestClient make_cryptocom_test_client(
        std::function<HttpResponse(const HttpRequest&)>);

    // Shared perform + parse, folding transport/parse failures into the envelope.
    template<typename R>
    exchange::rest::RestResponse<R> dispatch(const HttpRequest& http);

    std::shared_ptr<CurlHttpClient>                 transport_;  // null in tests
    std::function<HttpResponse(const HttpRequest&)> perform_;
    int64_t                                         next_id_{1};  // correlation id (single-thread)
};

// Factory used by unit tests to inject a mock HTTP performer.
// Defined in src/cryptocom/rest_client.cpp.
CryptoComRestClient
make_cryptocom_test_client(std::function<HttpResponse(const HttpRequest&)> fn);

} // namespace exchange::cryptocom::rest

#include "exchange/cryptocom/rest_client.inl"
