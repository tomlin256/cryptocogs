// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/common/rest.hpp
// Exchange-agnostic REST layer types.
//
// TypedPublicRequest<R>  — base for unauthenticated REST requests; links
//                          request type to its response type at compile time.
// TypedPrivateRequest<R> — base for authenticated REST requests.
// RestResponse<T>        — uniform response envelope (ok, result, errors).
// HttpRequest            — prepared HTTP request passed to the transport layer.
// IRestAuth              — authentication strategy interface; implementations
//                          inject credentials/signatures into HttpRequest.

#include "exchange/common/cxx_standard.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace exchange::rest {

// ── Prepared HTTP request ────────────────────────────────────────────────────

struct HttpRequest {
    enum class Method : std::uint8_t { GET, POST, DELETE };
    Method      method{Method::GET};
    std::string path;
    std::string query;   // URL-encoded query string (GET requests)
    std::string body;    // URL-encoded or JSON body (POST/DELETE requests)
    std::map<std::string, std::string> headers;
};

// ── Authentication strategy ──────────────────────────────────────────────────

struct IRestAuth {
    virtual ~IRestAuth() = default;

    // Called once per request before it is dispatched. Injects auth headers
    // (API key, signature) into req. Whether the nonce/timestamp is injected
    // here or embedded in the body by build() is exchange-specific: Kraken
    // embeds the nonce during build() and signs here; Binance (Step 4) injects
    // timestamp here.
    virtual void sign(HttpRequest& req) const = 0;
};

// ── Response envelope ────────────────────────────────────────────────────────

template<typename T>
struct RestResponse {
    bool                     ok{false};
    std::optional<T>         result;
    std::vector<std::string> errors;

    bool               has_error() const;    // defined in rest.inl
    const std::string& first_error() const;  // defined in rest.inl
};

// ── Typed request bases ──────────────────────────────────────────────────────
//
// Each exchange-specific request struct inherits one of these and defines:
//   using response_type = SomeResult;
//   HttpRequest build() const;              // public
//   HttpRequest build(const Auth&) const;   // private (Auth type per exchange)

template<typename R>
struct TypedPublicRequest {
    using response_type = R;
    virtual ~TypedPublicRequest() = default;
    virtual HttpRequest build() const = 0;
};

// Private request base does not mandate a build signature — different exchanges
// pass auth in different ways.  The typed link response_type → R is the
// compile-time contract; the client's execute() template enforces it.
template<typename R>
struct TypedPrivateRequest {
    using response_type = R;
    virtual ~TypedPrivateRequest() = default;
};

} // namespace exchange::rest

#include "exchange/common/rest.inl"
