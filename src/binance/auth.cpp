// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/binance/auth.hpp"

#include "exchange/common/credentials_file.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

// OpenSSL headers — required for HMAC-SHA256.
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace exchange::binance::rest {

// ── Crypto utilities ─────────────────────────────────────────────────────────

namespace detail {

// HMAC-SHA256 of `data` using `key`, returning raw bytes.
// Key material is the raw UTF-8 secret bytes (unlike Kraken, which base64-decodes first).
std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char result[32];
    unsigned int  len = 32;
    if (HMAC(EVP_sha256(),
             reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             result, &len) == nullptr)        // review L4: surface OpenSSL failure
        throw std::runtime_error("HMAC-SHA256 computation failed");
    return std::string(reinterpret_cast<char*>(result), len);
}

// Encode raw bytes as a lowercase hex string.
std::string to_hex(const std::string& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : bytes)
        oss << std::setw(2) << static_cast<unsigned int>(c);
    return oss.str();
}

} // namespace detail

// ── BinanceCredentials ──────────────────────────────────────────────────────

BinanceCredentials BinanceCredentials::from_file(const std::string& name,
                                                 const std::string& location) {
    // TOML file with top-level string keys api_key / api_secret (plan credentials-from-file);
    // api_secret maps to secret_key. algorithm / recv_window_ms keep defaults.
    const auto v = exchange::rest::read_toml_credentials(
        name, ".binance", location, {"api_key", "api_secret"});
    return BinanceCredentials{v[0], v[1]};
}

// ── BinanceAuth ───────────────────────────────────────────────────────────────

BinanceAuth::BinanceAuth(BinanceCredentials creds, ClockFn clock)
    : creds_(std::move(creds))
    , clock_(clock ? std::move(clock) : []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
      })
{}

void BinanceAuth::sign(exchange::rest::HttpRequest& req) const {
    // Only HMAC-SHA256 is implemented. Reject other algorithms loudly rather
    // than silently HMAC-signing with the wrong scheme (would produce a
    // signature the server rejects, with no clue why).
    if (creds_.algorithm != BinanceSignAlgorithm::HmacSha256)
        throw std::invalid_argument(
            "BinanceAuth: only HMAC-SHA256 signing is implemented "
            "(RSA / Ed25519 are not supported)");

    using M = exchange::rest::HttpRequest::Method;

    const bool is_body_request = (req.method == M::POST);

    // Build the timestamp + recvWindow suffix.
    std::string ts_params = "timestamp=" + std::to_string(clock_());
    if (creds_.recv_window_ms > 0)
        ts_params += "&recvWindow=" + std::to_string(creds_.recv_window_ms);

    // Append to the appropriate field (query for GET/DELETE, body for POST).
    if (is_body_request) {
        if (!req.body.empty()) req.body += '&';
        req.body += ts_params;
        if (req.headers.find("Content-Type") == req.headers.end())
            req.headers["Content-Type"] = "application/x-www-form-urlencoded";
    } else {
        if (!req.query.empty()) req.query += '&';
        req.query += ts_params;
    }

    // Signature covers the full "queryString + requestBody" concatenation.
    std::string payload = req.query + req.body;
    std::string sig = detail::to_hex(detail::hmac_sha256(creds_.secret_key, payload));

    if (is_body_request)
        req.body += "&signature=" + sig;
    else
        req.query += "&signature=" + sig;

    req.headers["X-MBX-APIKEY"] = creds_.api_key;
}

} // namespace exchange::binance::rest
