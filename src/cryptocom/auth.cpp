// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/cryptocom/auth.hpp"

#include "exchange/common/credentials_file.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// OpenSSL headers — required for HMAC-SHA256.
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace exchange::cryptocom::rest {

// ── Crypto + serialisation utilities ─────────────────────────────────────────

namespace detail {

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

std::string to_hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0F]);
    }
    return out;
}

namespace {

// Scalar string form of a JSON value — the reference algorithm's str(value).
// The adapter sends every param as a string, so the string branch is the live
// path; the others are defensive and chosen to be language-neutral.
std::string scalar_to_str(const json& v) {
    if (v.is_string())          return v.get<std::string>();
    if (v.is_null())            return "null";
    if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer())  return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss << v.get<double>();
        return oss.str();
    }
    return v.dump();  // object/array serialised whole (recursion-depth cap)
}

} // namespace

std::string params_to_str(const json& params, int level) {
    // Beyond the depth cap, or for any non-object input, serialise whole.
    if (level >= MAX_LEVEL || !params.is_object())
        return scalar_to_str(params);

    // Emit keys in ascending order. nlohmann::json's default object is already
    // key-sorted, but sort explicitly so correctness never depends on that.
    std::vector<std::string> keys;
    keys.reserve(params.size());
    for (auto it = params.begin(); it != params.end(); ++it)
        keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());

    std::string out;
    for (const auto& key : keys) {
        out += key;
        const json& v = params.at(key);
        if (v.is_null()) {
            out += "null";
        } else if (v.is_array()) {
            for (const auto& elem : v)
                out += params_to_str(elem, level + 1);
        } else {
            out += scalar_to_str(v);
        }
    }
    return out;
}

} // namespace detail

int64_t make_nonce() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

CryptoComCredentials CryptoComCredentials::from_file(const std::string& name,
                                                     const std::string& location) {
    // TOML file with top-level string keys api_key / api_secret (plan credentials-from-file).
    const auto v = exchange::rest::read_toml_credentials(
        name, ".cryptocom", location, {"api_key", "api_secret"});
    return CryptoComCredentials{v[0], v[1]};
}

std::string CryptoComCredentials::sign(const std::string& method,
                                       int64_t            id,
                                       const json&        params,
                                       int64_t            nonce) const {
    const std::string params_str = detail::params_to_str(params, 0);
    const std::string digest =
        method + std::to_string(id) + api_key + params_str + std::to_string(nonce);
    return detail::to_hex(detail::hmac_sha256(api_secret, digest));
}

} // namespace exchange::cryptocom::rest
