// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/auth.hpp"

#include "exchange/common/credentials_file.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// OpenSSL headers — required for sign().
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace exchange::kraken::rest {

// ── Crypto utilities ─────────────────────────────────────────────────────────

namespace detail {

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

// SHA-256 of a byte string, returning raw bytes.
std::string sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH);
}

// HMAC-SHA512 of `data` using `key`, returning raw bytes.
std::string hmac_sha512(const std::string& key, const std::string& data) {
    unsigned char result[64];
    unsigned int  len = 64;
    if (HMAC(EVP_sha512(),
             reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             result, &len) == nullptr)        // review L4: surface OpenSSL failure
        throw std::runtime_error("HMAC-SHA512 computation failed");
    return std::string(reinterpret_cast<char*>(result), len);
}

// URL-encode a string (application/x-www-form-urlencoded).
std::string url_encode(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            oss << c;
        else
            oss << '%' << std::uppercase << std::hex << std::setw(2)
                << std::setfill('0') << static_cast<unsigned int>(c);
    }
    return oss.str();
}

// Build url-encoded POST body from a flat key/value map.
std::string build_form_body(const std::map<std::string, std::string>& params) {
    std::string body;
    for (const auto& [k, v] : params) {
        if (!body.empty()) body += '&';
        body += url_encode(k) + '=' + url_encode(v);
    }
    return body;
}

} // namespace detail

// ── Nonce helper ─────────────────────────────────────────────────────────────

uint64_t make_nonce() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
}

// ── KrakenCredentials ──────────────────────────────────────────────────────────────

KrakenCredentials KrakenCredentials::from_file(const std::string& name,
                                   const std::string& location) {
    // TOML file with top-level string keys api_key / api_secret (plan credentials-from-file).
    const auto v = exchange::rest::read_toml_credentials(
        name, ".kraken", location, {"api_key", "api_secret"});
    return KrakenCredentials{v[0], v[1]};
}

std::string KrakenCredentials::sign(const std::string& uri_path,
                              const std::string& nonce,
                              const std::string& post_body) const {
    using namespace detail;
    std::string decoded_secret = base64_decode(api_secret);
    std::string sha256_input   = nonce + post_body;
    std::string hashed         = sha256(sha256_input);
    std::string message        = uri_path + hashed;
    std::string mac            = hmac_sha512(decoded_secret, message);
    return base64_encode(reinterpret_cast<const unsigned char*>(mac.data()), mac.size());
}

// ── KrakenAuth ───────────────────────────────────────────────────────────────

KrakenAuth::KrakenAuth(KrakenCredentials creds) : creds_(std::move(creds)) {}

void KrakenAuth::sign(exchange::rest::HttpRequest& req) const {
    std::string nonce_str;
    auto ct = req.headers.find("Content-Type");
    if (ct != req.headers.end() && ct->second == "application/json") {
        nonce_str = nlohmann::json::parse(req.body).at("nonce").get<std::string>();
    } else {
        const std::string prefix = "nonce=";
        auto pos = req.body.find(prefix);
        if (pos != std::string::npos) {
            pos += prefix.size();
            auto end = req.body.find('&', pos);
            nonce_str = req.body.substr(
                pos, end == std::string::npos ? std::string::npos : end - pos);
        }
    }
    req.headers["API-Key"]  = creds_.api_key;
    req.headers["API-Sign"] = creds_.sign(req.path, nonce_str, req.body);
}

} // namespace exchange::kraken::rest
