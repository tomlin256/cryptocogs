// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/binance/auth.hpp
// Binance REST authentication: BinanceCredentials, BinanceAuth (IRestAuth implementor),
// and supporting HMAC-SHA256 / hex-encoding utilities.
//
// Namespace: exchange::binance::rest

#include "exchange/common/rest.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace exchange::binance::rest {

// ── Crypto utilities (defined in src/binance/auth.cpp) ───────────────────────

namespace detail {

// HMAC-SHA256 of `data` using `key`, returning raw bytes.
// Key material is the raw UTF-8 secret bytes (unlike Kraken, which base64-decodes first).
std::string hmac_sha256(const std::string& key, const std::string& data);

// Encode raw bytes as a lowercase hex string.
std::string to_hex(const std::string& bytes);

} // namespace detail

// ── Signing algorithm selector ────────────────────────────────────────────────

enum class BinanceSignAlgorithm : std::uint8_t { HmacSha256, Rsa, Ed25519 };

// ── Credential bundle ────────────────────────────────────────────────────────

struct BinanceCredentials {
    std::string          api_key;
    std::string          secret_key;
    BinanceSignAlgorithm algorithm{BinanceSignAlgorithm::HmacSha256};
    // Included in every signed request.  Binance default is 5000 ms; max 60000.
    int                  recv_window_ms{5000};

    // Load api_key + secret_key from a TOML file (plan 021) with top-level
    // string keys (api_secret maps to secret_key):
    //   api_key    = "..."
    //   api_secret = "..."
    // algorithm and recv_window_ms keep their defaults — override after load.
    // Path: location.empty() ? $HOME/.binance/<name> : <location>/<name>.
    // Throws std::runtime_error if the file is missing/malformed or a key is
    // absent, non-string, or empty.
    static BinanceCredentials from_file(const std::string& name,
                                        const std::string& location = "");
};

// ── IRestAuth implementor ─────────────────────────────────────────────────────
//
// BinanceAuth wraps BinanceCredentials and implements IRestAuth::sign().
//
// Signing differences from Kraken (Option 2 pattern — see Step 3 notes in the plan):
//   Digest      : HMAC-SHA256  (Kraken uses HMAC-SHA512)
//   Signed payload: query_string + body (already URL-encoded, raw concatenation)
//   Output encoding: lowercase hex  (Kraken uses base64)
//   Anti-replay : timestamp (ms) + recvWindow injected here  (Kraken embeds nonce in build())
//   Key material: raw UTF-8 secret bytes  (Kraken base64-decodes)
//
// sign() appends "timestamp=<ms>&recvWindow=<ms>" to the query string (GET/DELETE)
// or request body (POST), then computes the signature over the full
// (query + body) concatenation and appends "&signature=<hex>" to the same field.
// The X-MBX-APIKEY header is also injected.
//
// The ClockFn parameter makes the timestamp injectable for unit tests.
struct BinanceAuth : exchange::rest::IRestAuth {
    using ClockFn = std::function<int64_t()>;

    // Defined in src/binance/auth.cpp.
    explicit BinanceAuth(BinanceCredentials creds, ClockFn clock = {});

    void sign(exchange::rest::HttpRequest& req) const override;

private:
    BinanceCredentials creds_;
    ClockFn            clock_;
};

} // namespace exchange::binance::rest
