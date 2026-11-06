// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/http_client.hpp
// CurlHttpClient — a generic libcurl-backed HTTP transport shared by every REST
// adapter. Owns one curl easy handle and maps an exchange::rest::HttpRequest to
// an {status, body} HttpResponse (GET / POST / DELETE). Exchange-agnostic: it
// knows nothing about envelopes, auth, or endpoints — adapters layer their
// typed execute()/parse on top.
//
// Not thread-safe: like the libcurl easy API, one client serves one thread.
//
// Namespace: exchange::rest

#include "exchange/common/rest.hpp"

#include <curl/curl.h>

#include <memory>
#include <string>

namespace exchange::rest {

// Raw HTTP reply: the numeric status code plus the response body. Adapters that
// key error handling off the status (e.g. Binance) read both; adapters that read
// errors from the JSON envelope (e.g. Kraken) ignore status.
struct HttpResponse {
    int         status{0};
    std::string body;
};

class CurlHttpClient {
public:
    explicit CurlHttpClient(std::string base_url);

    // Executes one request against base_url + path (+ ?query), returning the
    // status and body. Throws std::runtime_error on a libcurl failure: a
    // transport error, or libcurl unable to allocate the request's header list
    // (nothing is sent then, rather than a request with headers missing).
    HttpResponse perform(const HttpRequest& http);

    // Move-only (owns a curl handle); copy is implicitly deleted by the
    // unique_ptr member — no double-cleanup possible.

private:
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);

    using Handle = std::unique_ptr<CURL, void (*)(CURL*)>;

    std::string base_url_;
    Handle      curl_;
};

} // namespace exchange::rest
