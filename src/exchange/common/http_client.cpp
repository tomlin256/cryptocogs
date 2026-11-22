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

// What a caller is told when a request may have reached the server and no reply says what became of it.
constexpr std::string_view OUTCOME_UNKNOWN =
    "The request may have been received and acted on: its outcome is unknown, so check the exchange before sending it again.";

// How many times libcurl has been about to write the request of one call to perform.
struct SendOnce {
    int writes{0};
};

// libcurl's pre-request callback (CURLOPT_PREREQFUNCTION). libcurl calls it each time it is about to write the request, when the
// connection is ready and nothing of the request has gone out, on a new connection and on a reused one alike. `clientp` is the SendOnce
// of the call. The first write goes ahead; a second ends the call with CURLE_ABORTED_BY_CALLBACK.
int allow_one_send(void* clientp, char* /*conn_primary_ip*/, char* /*conn_local_ip*/, int /*conn_primary_port*/,
                   int /*conn_local_port*/) {
    auto* once = static_cast<SendOnce*>(clientp);
    ++once->writes;
    return once->writes > 1 ? CURL_PREREQFUNC_ABORT : CURL_PREREQFUNC_OK;
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
        text.append(OUTCOME_UNKNOWN);
    else
        text.append("The request was not sent.");
    return text;
}

void require_option(const CURLcode result, const std::string_view option) {
    if (result == CURLE_OK)
        return;
    std::string text("CurlHttpClient: libcurl refused ");
    text.append(option).append(" (").append(curl_easy_strerror(result)).append("). The libcurl in use is ");
    text.append(curl_version_info(CURLVERSION_NOW)->version).append(", and this library was built with ").append(LIBCURL_VERSION);
    text.append(". Without that option a POST or a DELETE cannot be held to one write, so the request was not sent.");
    throw std::runtime_error(text);
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

    // A POST or a DELETE is written at most once. libcurl writes a request again, on another connection, when the first attempt gets no
    // reply: a reused connection that closes or is reset under it, an HTTP/2 stream that the server refuses. It cannot tell a request that
    // the server never read from one that it read and acted on, so an order would reach the exchange twice, and no option turns that off.
    // libcurl does call this callback each time it is about to write the request, on a new connection or a reused one: the callback lets
    // the first write through and ends the call at a second. A GET is safe to repeat and gets no callback. The reset above clears both
    // options, so they are set on every call; `once` lives until curl_easy_perform returns, and the next call resets the handle first.
    // A libcurl that refuses either option would leave the request with nothing to hold it to one write, so the call throws here,
    // before anything is sent.
    SendOnce once;
    if (http.method != M::GET) {
        detail::require_option(curl_easy_setopt(curl, CURLOPT_PREREQFUNCTION, &allow_one_send), "CURLOPT_PREREQFUNCTION");
        detail::require_option(curl_easy_setopt(curl, CURLOPT_PREREQDATA, static_cast<void*>(&once)), "CURLOPT_PREREQDATA");
    }

    // Response buffer.
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlHttpClient::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&response));

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_ABORTED_BY_CALLBACK) {
        // allow_one_send stopped a second write: the first attempt was let through, and no reply came to it. It is the only callback of
        // this call that can end it, since write_cb takes everything it is given. One added to perform that can end the call has to be
        // told apart from it here, or its failure would be reported in these words.
        std::string text("curl_easy_perform failed: no reply came to the first attempt, and the request was not sent a second time. ");
        text.append(OUTCOME_UNKNOWN);
        throw std::runtime_error(text);
    }
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
