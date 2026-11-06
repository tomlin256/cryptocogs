// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/http_client.hpp"

#include "exchange/common/curl_header_list.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>

namespace exchange::rest {

namespace {

// A limit that is not positive would reach libcurl as 0, which it reads as "no limit" (total) or "300 s" (connect): the hang that the
// limits exist to prevent (#56). Refused before the handle is made, with the limit named.
void require_positive(const std::chrono::milliseconds limit, const std::string_view name) {
    if (limit.count() > 0)
        return;
    std::string message("CurlHttpClient: the ");
    message.append(name).append(" timeout must be positive, got ").append(std::to_string(limit.count())).append(" ms");
    throw std::invalid_argument(message);
}

HttpTimeouts checked(const HttpTimeouts& timeouts) {
    require_positive(timeouts.connect, "connect");
    require_positive(timeouts.total, "total");
    return timeouts;
}

} // namespace

// ---------------------------------------------------------------------------
// write_cb — curl write callback; appends received data to a std::string.
// ---------------------------------------------------------------------------
size_t CurlHttpClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

CurlHttpClient::CurlHttpClient(std::string base_url, const HttpTimeouts timeouts)
    : base_url_(std::move(base_url))
    , timeouts_(checked(timeouts))
    , curl_(curl_easy_init(), &curl_easy_cleanup)
{
    if (!curl_)
        throw std::runtime_error("CurlHttpClient: curl_easy_init() failed");
}

namespace detail {

std::string timeout_message(const bool request_may_have_been_sent, const HttpTimeouts& limits) {
    std::string text("curl_easy_perform failed: ");
    text.append(curl_easy_strerror(CURLE_OPERATION_TIMEDOUT));
    text.append(" (connect limit ").append(std::to_string(limits.connect.count()));
    text.append(" ms, total limit ").append(std::to_string(limits.total.count())).append(" ms). ");
    if (request_may_have_been_sent)
        text.append("The request may have been received and acted on: its outcome is unknown, so check the exchange before sending it again.");
    else
        text.append("The request was not sent.");
    return text;
}

} // namespace detail

HttpResponse CurlHttpClient::perform(const HttpRequest& http) {
    CURL* curl = curl_.get();

    // The handle is reused, and libcurl's options stay set until they are changed. HTTPHEADER and POSTFIELDS are pointers, to a list that
    // was freed and to a body that belonged to the previous call, so put every option back to its default before this request sets its
    // own (#51). Live connections and the DNS and session caches are kept, so connections are still reused. This comes first so that no
    // way out of the previous call, a throw included, leaves anything behind. An option that should hold for every request is set below,
    // on each call.
    curl_easy_reset(curl);

    // The options that hold for every request are set here, on each call: the reset above clears them, so set once (in the constructor, or
    // on the first call) they would be gone (#56). The two limits bound the call, a server that stops answering included. NOSIGNAL is
    // libcurl's advice for a program with threads that uses timeouts: a build with the synchronous resolver enforces a name-lookup timeout
    // with SIGALRM, which a threaded program cannot take. With the threaded resolver, and with the numeric addresses that the tests use,
    // it changes nothing that a test can see.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeouts_.connect.count()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeouts_.total.count()));

    // Build full URL.
    std::string url = base_url_ + http.path;
    if (!http.query.empty())
        url += '?' + http.query;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Build the header list from HttpRequest::headers. If libcurl cannot allocate it this throws before anything is sent (#48).
    const auto header_list = detail::make_curl_header_list(http.headers);
    if (header_list)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list.get());

    // Method-specific options. The reset above left no CUSTOMREQUEST, body or POST from an earlier call to undo.
    using M = HttpRequest::Method;
    if (http.method == M::GET) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (http.method == M::DELETE) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        if (!http.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, http.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(http.body.size()));
        }
    } else {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, http.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(http.body.size()));
    }

    // A POST or a DELETE is never sent on a connection that an earlier request used (#58). When a reused connection is closed by the server
    // without a reply, libcurl cannot tell a request that the server never read from one that it read and acted on, so it writes the
    // request again on a new connection: an order would reach the exchange twice, and the caller would see only the second attempt's
    // outcome. libcurl has no option that turns this retry off, and it happens only on a reused connection, so such a request gets a
    // connection of its own. A GET is safe to repeat and keeps reusing connections (#51). The reset above clears this option, so it is set
    // on every call.
    if (http.method != M::GET)
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);

    // Response buffer.
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlHttpClient::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&response));

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        // How far did the call get? The pre-transfer time is 0 until libcurl is about to send the request, so 0 proves that nothing was sent,
        // and anything above 0 means that it may have been. If libcurl cannot say, assume that it went out: the other claim is the dangerous
        // one, because it would let a caller send an order again.
        curl_off_t began_us = 0;
        const bool may_have_been_sent =
            curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME_T, &began_us) != CURLE_OK || began_us > 0;
        throw std::runtime_error(detail::timeout_message(may_have_been_sent, timeouts_));
    }
    if (rc != CURLE_OK)
        throw std::runtime_error(
            std::string("curl_easy_perform failed: ") + curl_easy_strerror(rc));

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    return HttpResponse{static_cast<int>(http_code), std::move(response)};
}

} // namespace exchange::rest
