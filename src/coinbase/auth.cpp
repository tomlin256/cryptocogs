// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/coinbase/auth.hpp"

#include "exchange/common/credentials_file.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

// OpenSSL headers — required for HMAC-SHA256.
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace exchange::coinbase::rest {

// ── Crypto utilities ─────────────────────────────────────────────────────────

namespace detail {

std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        // Each byte is widened to unsigned before it is shifted: a shift of the
        // promoted (signed) int is what bugprone-signed-bitwise objects to.
        unsigned int val = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) val |= static_cast<unsigned int>(data[i+1]) << 8;
        if (i + 2 < len) val |= static_cast<unsigned int>(data[i+2]);
        out.push_back(chars[(val >> 18) & 63]);
        out.push_back(chars[(val >> 12) & 63]);
        out.push_back(i + 1 < len ? chars[(val >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? chars[val & 63] : '=');
    }
    return out;
}

std::string base64_decode(const std::string& in) {
    std::string out;
    // val takes every 6-bit group and is never reduced, so after the fifth
    // character it has shifted out of 31 bits. A signed int is undefined
    // behaviour there (issue #23); unsigned wraps, and only the low 12 bits are
    // ever read below (bits is 0, 2 or 4 when a byte is emitted).
    unsigned int val = 0;
    int bits = -8;
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (unsigned char c : in) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<unsigned int>(pos);
        bits += 6;
        if (bits >= 0) {
            // bits is signed only because it starts below zero; it is 0, 2 or 4 here.
            out.push_back(static_cast<char>((val >> static_cast<unsigned int>(bits)) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char result[32];
    unsigned int  len = 32;
    if (HMAC(EVP_sha256(),
             reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             result, &len) == nullptr)
        throw std::runtime_error("HMAC-SHA256 computation failed");
    return std::string(reinterpret_cast<char*>(result), len);
}

std::string coinbase_sign(const std::string& api_secret,
                          const std::string& timestamp,
                          const std::string& method,
                          const std::string& request_path,
                          const std::string& body) {
    const std::string decoded_secret = base64_decode(api_secret);
    const std::string prehash        = timestamp + method + request_path + body;
    const std::string mac            = hmac_sha256(decoded_secret, prehash);
    return base64_encode(reinterpret_cast<const unsigned char*>(mac.data()), mac.size());
}

} // namespace detail

namespace {

const char* method_str(exchange::rest::HttpRequest::Method m) {
    using M = exchange::rest::HttpRequest::Method;
    switch (m) {
        case M::GET:    return "GET";
        case M::POST:   return "POST";
        case M::DELETE: return "DELETE";
    }
    return "GET";  // unreachable; keeps the compiler quiet
}

} // namespace

// ── CoinbaseCredentials ──────────────────────────────────────────────────────

CoinbaseCredentials CoinbaseCredentials::from_file(const std::string& name,
                                                   const std::string& location) {
    // TOML file with top-level string keys api_key / api_secret / passphrase
    // (plan 021).
    const auto v = exchange::rest::read_toml_credentials(
        name, ".coinbase", location, {"api_key", "api_secret", "passphrase"});
    return CoinbaseCredentials{v[0], v[1], v[2]};
}

// ── CoinbaseAuth ──────────────────────────────────────────────────────────────

CoinbaseAuth::CoinbaseAuth(CoinbaseCredentials creds, ClockFn clock)
    : creds_(std::move(creds))
    , clock_(clock ? std::move(clock) : []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
      })
{}

void CoinbaseAuth::sign(exchange::rest::HttpRequest& req) const {
    using M = exchange::rest::HttpRequest::Method;

    // requestPath includes the query string when present — Coinbase signs the
    // exact path the server receives (e.g. "/orders?status=open").
    std::string request_path = req.path;
    if (!req.query.empty())
        request_path += "?" + req.query;

    const std::string timestamp = std::to_string(clock_());
    const std::string sig       = detail::coinbase_sign(
        creds_.api_secret, timestamp, method_str(req.method), request_path, req.body);

    req.headers["CB-ACCESS-KEY"]        = creds_.api_key;
    req.headers["CB-ACCESS-SIGN"]       = sig;
    req.headers["CB-ACCESS-TIMESTAMP"]  = timestamp;
    req.headers["CB-ACCESS-PASSPHRASE"] = creds_.passphrase;

    // Coinbase POST/DELETE bodies are JSON; set the type when the caller hasn't.
    if (req.method == M::POST && !req.body.empty() &&
        req.headers.find("Content-Type") == req.headers.end())
        req.headers["Content-Type"] = "application/json";
}

} // namespace exchange::coinbase::rest
