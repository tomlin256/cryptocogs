// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#pragma once

// exchange/cryptocom/auth.hpp
// Crypto.com Exchange v1 authentication: CryptoComCredentials, the request
// signer, and the supporting HMAC-SHA256 / hex / params-serialisation
// primitives. All bodies are defined in src/cryptocom/auth.cpp.
//
// Crypto.com signs every private request — REST *and* the WebSocket user channel
// — with a `sig` field placed INSIDE the JSON request envelope (not an HTTP
// header):
//   digest = method + id + api_key + params_string + nonce
//   sig    = hex( HMAC-SHA256( api_secret, digest ) )
// where api_secret is used as the HMAC key VERBATIM (it is NOT base64-decoded,
// unlike Kraken/Coinbase), and params_string is a recursive, key-sorted
// serialisation of the params object (see detail::params_to_str; MAX_LEVEL = 3).
//
// Because the signature lives in the body and depends on the params, signing is
// done inside each request's build(const CryptoComCredentials&) (Kraken-style),
// NOT via a header-injecting IRestAuth.
//
// Convention: the adapter builds params with every value as a JSON *string*
// (numbers via TickPrice::str(), bools as "true"/"false"). That keeps
// params_to_str's per-value stringification deterministic and identical to the
// server's, sidestepping cross-language str() ambiguity (e.g. Python str(True)).
//
// Namespace: exchange::cryptocom::rest

#include "exchange/common/cxx_standard.hpp"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace exchange::cryptocom::rest {

using json = nlohmann::json;

// ── Crypto + serialisation utilities (defined in src/cryptocom/auth.cpp) ─────

namespace detail {

// Maximum recursion depth of params_to_str (per the Crypto.com v1 reference
// algorithm): values nested deeper than this serialise as a whole.
constexpr int MAX_LEVEL = 3;

// HMAC-SHA256 of `data` using `key`, returning raw bytes. Key material is the
// raw UTF-8 secret bytes (Crypto.com does NOT base64-decode the secret).
std::string hmac_sha256(const std::string& key, const std::string& data);

// Encode raw bytes as a lowercase hex string.
std::string to_hex(const std::string& bytes);

// Crypto.com's request-parameter serialisation, ported from the reference
// algorithm in the v1 docs:
//   - object keys are emitted in ascending order, each as key + value
//     (no separators)
//   - a null value serialises as the literal "null"
//   - an array value iterates its elements, recursing at level + 1
//   - any other (scalar) value serialises via its string form
//   - recursion stops at MAX_LEVEL: a value at that depth serialises whole
// The adapter builds params with string-valued fields, so in practice this only
// ever serialises strings, nulls, and arrays of (string-valued) objects.
std::string params_to_str(const json& params, int level = 0);

} // namespace detail

// Millisecond nonce: milliseconds since the Unix epoch (the unit Crypto.com
// expects). Non-decreasing across calls.
int64_t make_nonce();

// ── Credential bundle ────────────────────────────────────────────────────────
//
// A plain struct — set both fields directly, or load them from a TOML file with
// from_file (plan 021). Note there is NO passphrase (unlike Coinbase).
struct CryptoComCredentials {
    std::string api_key;
    std::string api_secret;  // used as the HMAC key verbatim (not base64-decoded)

    // Load api_key + api_secret from a TOML file (plan 021) with top-level
    // string keys:
    //   api_key    = "..."
    //   api_secret = "..."
    // Path: location.empty() ? $HOME/.cryptocom/<name> : <location>/<name>.
    // Throws std::runtime_error if the file is missing/malformed or a key is
    // absent, non-string, or empty.
    static CryptoComCredentials from_file(const std::string& name,
                                          const std::string& location = "");

    // Sign a request/auth frame and return the lowercase-hex `sig` value.
    // `params` may be an empty object — the WS public/auth frame signs only
    // method + id + api_key + nonce (its params_string is empty).
    std::string sign(const std::string& method,
                     int64_t            id,
                     const json&        params,
                     int64_t            nonce) const;
};

} // namespace exchange::cryptocom::rest
