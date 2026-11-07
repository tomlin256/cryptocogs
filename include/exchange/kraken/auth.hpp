// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/kraken/auth.hpp
// Kraken REST authentication: KrakenCredentials, KrakenAuth (IRestAuth implementor),
// nonce generator, and supporting crypto utilities. All bodies are defined in
// src/kraken/auth.cpp.
//
// Namespace: exchange::kraken::rest

#include "exchange/common/rest.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace exchange::kraken::rest {

// ── Crypto utilities (defined in src/kraken/auth.cpp) ────────────────────────

namespace detail {

std::string base64_decode(const std::string& in);
std::string base64_encode(const unsigned char* data, size_t len);
std::string sha256(const std::string& data);                         // raw bytes
std::string hmac_sha512(const std::string& key, const std::string& data);
std::string url_encode(const std::string& s);
std::string build_form_body(const std::map<std::string, std::string>& params);

} // namespace detail

// ── Nonce helper ─────────────────────────────────────────────────────────────

// Returns a monotonically increasing nonce based on the system clock.
// Uses microseconds to match the KAPI nonce scale (16-digit numbers).
uint64_t make_nonce();

// ── Auth credential bundle ───────────────────────────────────────────────────

struct KrakenCredentials {
    std::string api_key;     // public key  → API-Key header
    std::string api_secret;  // base64-encoded private key → used for signing

    // Load from a TOML file (plan 021) with top-level string keys:
    //   api_key    = "..."
    //   api_secret = "..."
    // Path: location.empty() ? $HOME/.kraken/<name> : <location>/<name>.
    // Throws std::runtime_error if the file is missing/malformed or a key is
    // absent, non-string, or empty.
    static KrakenCredentials from_file(const std::string& name,
                                 const std::string& location = "");

    // Compute the API-Sign header value:
    //   base64( HMAC-SHA512( base64_decode(api_secret), uri_path + SHA256(nonce + post_body) ) )
    //   uri_path : e.g. "/0/private/AddOrder"
    //   nonce    : the nonce string included in post_body
    //   post_body: raw url-encoded POST body (must already include nonce=...)
    std::string sign(const std::string& uri_path,
                     const std::string& nonce,
                     const std::string& post_body) const;
};

// ── IRestAuth implementor ─────────────────────────────────────────────────────
//
// KrakenAuth wraps KrakenCredentials and implements IRestAuth::sign().
//
// Design note (Option 2, Step 3): PrivateRequest::build() constructs the
// complete request body — including the nonce field — before calling sign().
// sign() therefore extracts the nonce from the already-built body rather than
// injecting it. This keeps build() shapes stable and confines all header
// injection to a single chokepoint.
//
// Two body formats are supported:
//   application/x-www-form-urlencoded — nonce= appears as the first key-value pair
//   application/json                  — nonce is a top-level string field
struct KrakenAuth : exchange::rest::IRestAuth {
    explicit KrakenAuth(KrakenCredentials creds);

    void sign(exchange::rest::HttpRequest& req) const override;

private:
    KrakenCredentials creds_;
};

} // namespace exchange::kraken::rest
