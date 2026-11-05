// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/http_client.hpp"

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

    // Build full URL.
    std::string url = base_url_ + http.path;
    if (!http.query.empty())
        url += '?' + http.query;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Build header list from HttpRequest::headers.
    auto chunk_deleter = [](curl_slist* p) { if (p) curl_slist_free_all(p); };
    std::unique_ptr<curl_slist, decltype(chunk_deleter)> chunk(nullptr, chunk_deleter);
    for (const auto& [key, val] : http.headers) {
        // "Key: value", appended onto one string instead of chained with operator+
        // (performance-inefficient-string-concatenation). curl_slist_append copies it.
        std::string line;
        line.reserve(key.size() + 2 + val.size());
        line += key;
        line += ": ";
        line += val;
        chunk.reset(curl_slist_append(chunk.release(), line.c_str()));
    }
    if (chunk)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk.get());

    // Method-specific options. The handle is reused across calls, so clear any
    // sticky CUSTOMREQUEST a prior DELETE may have left before choosing a method
    // (otherwise a DELETE-then-GET on the same client would still send DELETE).
    using M = HttpRequest::Method;
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, nullptr);
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
