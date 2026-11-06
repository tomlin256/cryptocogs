// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/curl_header_list.hpp
// The libcurl header list (curl_slist) of one request, built from HttpRequest::headers. An implementation detail of
// CurlHttpClient::perform, kept in `detail` and out of the supported surface. It has a header of its own so that a test can replace
// the call that grows the list and make it fail (plan 038, #48).
//
// Namespace: exchange::rest::detail

#include "exchange/common/cxx_standard.hpp"

#include <curl/curl.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace exchange::rest::detail {

using Headers     = std::map<std::string, std::string>;                    // the type of HttpRequest::headers
using CurlSlist   = std::unique_ptr<curl_slist, void (*)(curl_slist*)>;    // owns a header list; null when there is none
using SlistAppend = std::function<curl_slist*(curl_slist*, const char*)>;  // curl_slist_append's signature

// Builds the list of "Key: value" lines, one per header, in key order; null for no headers. Throws std::runtime_error when libcurl
// cannot allocate a node, after freeing the nodes made so far: a request is never sent with some of its headers missing.
//
// `append` grows the list. The default is libcurl's own curl_slist_append, which returns NULL on failure and leaves the list it was
// given alone, and otherwise the head of the grown list. A test passes one that fails.
CurlSlist make_curl_header_list(const Headers& headers, const SlistAppend& append = &curl_slist_append);

} // namespace exchange::rest::detail
