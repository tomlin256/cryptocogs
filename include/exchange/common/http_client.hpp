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

#include <chrono>
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

// The two limits on one request (#56). libcurl's own defaults are no limit on a transfer and 300 s on a connection, so a server that
// stops answering would block the calling thread forever, and in a trading loop that thread is the one that cancels orders. Both limits
// are set on every call, and both must be positive: 0 would reach libcurl as "no limit" (total) or "300 s" (connect), the hang these
// exist to prevent. The total includes the connect, so a total below the connect limit leaves the connect limit unreachable.
struct HttpTimeouts {
    std::chrono::milliseconds connect{10'000};  // name resolution, TCP connect and TLS: until the request can be sent
    std::chrono::milliseconds total{30'000};    // the whole call, the connect included
};

class CurlHttpClient {
public:
    // `timeouts` bound every call to perform. Throws std::invalid_argument if either limit is not positive.
    explicit CurlHttpClient(std::string base_url, HttpTimeouts timeouts = {});

    // Executes one request against base_url + path (+ ?query), returning the
    // status and body. Throws std::runtime_error on a libcurl failure: a
    // transport error, a timeout (HttpTimeouts), or libcurl unable to allocate
    // the request's header list (nothing is sent then, rather than a request
    // with headers missing).
    //
    // A timeout says in its text whether the request was sent (#56). "was not
    // sent" means libcurl never began to send it, so sending it again is safe.
    // Otherwise it may have been received and acted on and its outcome is
    // unknown: an order has to be looked for at the exchange before it is sent
    // again.
    //
    // Each call stands alone: nothing set for an earlier request (its headers,
    // body or method) reaches the next, one that threw included, while
    // connections stay open between calls (#51).
    HttpResponse perform(const HttpRequest& http);

    // Move-only (owns a curl handle); copy is implicitly deleted by the
    // unique_ptr member — no double-cleanup possible.

private:
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);

    using Handle = std::unique_ptr<CURL, void (*)(CURL*)>;

    std::string  base_url_;
    HttpTimeouts timeouts_;
    Handle       curl_;
};

namespace detail {

// The text of the error for a call that libcurl stopped at one of its limits: which limits were set, and either that the request was
// not sent or that it may have been and its outcome is unknown. Apart from perform so that both texts can be tested without a timer
// (a plain-HTTP timeout at a few milliseconds is a race between the connect and the clock). Not part of the supported surface.
std::string timeout_message(bool request_may_have_been_sent, const HttpTimeouts& limits);

} // namespace detail

} // namespace exchange::rest
