// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/coinbase/auth.hpp
// Coinbase Exchange REST authentication: CoinbaseCredentials, CoinbaseAuth
// (IRestAuth implementor), and supporting base64 / HMAC-SHA256 primitives.
// All bodies are defined in src/coinbase/auth.cpp.
//
// Coinbase signs every private request with four headers:
//   CB-ACCESS-KEY        — the API key
//   CB-ACCESS-SIGN       — base64(HMAC-SHA256(base64_decode(secret), prehash))
//   CB-ACCESS-TIMESTAMP  — Unix epoch SECONDS
//   CB-ACCESS-PASSPHRASE — the key's passphrase
// where prehash = timestamp + method + requestPath + body, and requestPath
// includes the query string (e.g. "/orders?status=open").
//
// Namespace: exchange::coinbase::rest

#include "exchange/common/rest.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace exchange::coinbase::rest {

// ── Crypto utilities (defined in src/coinbase/auth.cpp) ──────────────────────

namespace detail {

std::string base64_encode(const unsigned char* data, size_t len);
std::string base64_decode(const std::string& in);

// HMAC-SHA256 of `data` using `key`, returning raw bytes.
std::string hmac_sha256(const std::string& key, const std::string& data);

// Coinbase request/login signature:
//   base64( HMAC-SHA256( base64_decode(api_secret),
//                        timestamp + method + request_path + body ) )
// request_path must already include the query string when present. Reused by
// both REST signing and the WebSocket user-channel subscribe (which signs
// timestamp + "GET" + "/users/self/verify" with an empty body).
std::string coinbase_sign(const std::string& api_secret,
                          const std::string& timestamp,
                          const std::string& method,
                          const std::string& request_path,
                          const std::string& body);

} // namespace detail

// ── Credential bundle ────────────────────────────────────────────────────────
//
// A plain struct — set the three fields directly, or load them from a TOML file
// with from_file (plan credentials-from-file).

struct CoinbaseCredentials {
    std::string api_key;
    std::string api_secret;  // base64-encoded
    std::string passphrase;

    // Load all three fields from a TOML file (plan credentials-from-file) with top-level string
    // keys:
    //   api_key    = "..."
    //   api_secret = "..."
    //   passphrase = "..."
    // Path: location.empty() ? $HOME/.coinbase/<name> : <location>/<name>.
    // Throws std::runtime_error if the file is missing/malformed or a key is
    // absent, non-string, or empty.
    static CoinbaseCredentials from_file(const std::string& name,
                                         const std::string& location = "");
};

// ── IRestAuth implementor ─────────────────────────────────────────────────────
//
// CoinbaseAuth wraps CoinbaseCredentials and implements IRestAuth::sign().
//
// sign() reconstructs requestPath = path + ("?" + query when non-empty),
// assembles the prehash, computes CB-ACCESS-SIGN, and injects all four
// CB-ACCESS-* headers. POST bodies are JSON, so Content-Type: application/json
// is set when the caller has not already chosen one.
//
// The ClockFn parameter makes the timestamp (Unix epoch SECONDS) injectable for
// deterministic unit tests.
struct CoinbaseAuth : exchange::rest::IRestAuth {
    using ClockFn = std::function<int64_t()>;  // returns Unix epoch SECONDS

    explicit CoinbaseAuth(CoinbaseCredentials creds, ClockFn clock = {});

    void sign(exchange::rest::HttpRequest& req) const override;

private:
    CoinbaseCredentials creds_;
    ClockFn             clock_;
};

} // namespace exchange::coinbase::rest
