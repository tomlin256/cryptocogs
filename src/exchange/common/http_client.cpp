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

#include <stdexcept>
#include <string>

namespace exchange::rest {

// ---------------------------------------------------------------------------
// write_cb — curl write callback; appends received data to a std::string.
// ---------------------------------------------------------------------------
size_t CurlHttpClient::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

CurlHttpClient::CurlHttpClient(std::string base_url)
    : base_url_(std::move(base_url))
    , curl_(curl_easy_init(), &curl_easy_cleanup)
{
    if (!curl_)
        throw std::runtime_error("CurlHttpClient: curl_easy_init() failed");
}

HttpResponse CurlHttpClient::perform(const HttpRequest& http) {
    CURL* curl = curl_.get();

    // The handle is reused, and libcurl's options stay set until they are changed. HTTPHEADER and POSTFIELDS are pointers, to a list that
    // was freed and to a body that belonged to the previous call, so put every option back to its default before this request sets its
    // own (#51). Live connections and the DNS and session caches are kept, so connections are still reused. This comes first so that no
    // way out of the previous call, a throw included, leaves anything behind. An option that should hold for every request is set below,
    // on each call.
    curl_easy_reset(curl);

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

    // Response buffer.
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlHttpClient::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&response));

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
        throw std::runtime_error(
            std::string("curl_easy_perform failed: ") + curl_easy_strerror(rc));

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    return HttpResponse{static_cast<int>(http_code), std::move(response)};
}

} // namespace exchange::rest
