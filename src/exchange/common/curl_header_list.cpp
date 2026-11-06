// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/common/curl_header_list.hpp"

#include <stdexcept>
#include <string>
#include <tuple>

namespace exchange::rest::detail {

CurlSlist make_curl_header_list(const Headers& headers, const SlistAppend& append) {
    CurlSlist list(nullptr, &curl_slist_free_all);
    for (const auto& [key, val] : headers) {
        // "Key: value", appended onto one string instead of chained with operator+
        // (performance-inefficient-string-concatenation). curl_slist_append copies it.
        std::string line;
        line.reserve(key.size() + 2 + val.size());
        line += key;
        line += ": ";
        line += val;

        // libcurl returns NULL on failure and leaves the list it was given alone, so `list` keeps ownership until the call has
        // worked. Releasing it first, as this loop once did, leaked every node built so far, emptied the list (the lines before the
        // failure were not sent) and let the lines after it start a new one.
        curl_slist* const grown = append(list.get(), line.c_str());
        if (grown == nullptr)
            throw std::runtime_error("CurlHttpClient: curl_slist_append() failed");
        if (grown != list.get()) {         // the first line always changes the head
            std::ignore = list.release();  // `grown` heads the same nodes: hand them over, do not free them
            list.reset(grown);
        }
    }
    return list;
}

} // namespace exchange::rest::detail
