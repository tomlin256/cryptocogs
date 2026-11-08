// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies exchange::cryptocom::rest auth: the HMAC-SHA256 / hex primitives, the
// bespoke params_to_str request serialisation, and CryptoComCredentials::sign().
//
// Crypto.com signing scheme (per exchange-docs.crypto.com/exchange/v1):
//   digest = method + id + api_key + params_string + nonce
//   sig    = hex( HMAC-SHA256( api_secret, digest ) )      // secret used verbatim
//   params_string: keys ascending, key+value; null -> "null"; arrays iterate +
//                  recurse; recursion capped at MAX_LEVEL (3).
//
// No official Crypto.com signature vector is published, so HMAC-SHA256 is proven
// against the authoritative RFC 4231 vector and sign() is pinned by recomputing
// its documented digest assembly independently.

#include "exchange/cryptocom/auth.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>  // getpid

using namespace exchange::cryptocom::rest;
using json = nlohmann::json;

namespace {

const char* TEST_KEY    = "test-api-key";
const char* TEST_SECRET = "test-secret";  // used verbatim as the HMAC key

} // namespace

// ── HMAC-SHA256 + to_hex — RFC 4231 Test Case 2 ──────────────────────────────
//
// key  = "Jefe", data = "what do ya want for nothing?"
// HMAC-SHA256 = 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
// Proves hmac_sha256 and to_hex together (sign() emits hex, so this is the live
// composition).
TEST(CryptoComCrypto, HmacSha256Hex_MatchesRfc4231Vector) {
    const std::string mac = detail::hmac_sha256("Jefe", "what do ya want for nothing?");
    EXPECT_EQ(detail::to_hex(mac),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(CryptoComCrypto, ToHex_KnownBytes) {
    const std::string bytes("\x00\x01\xff\xab", 4);
    EXPECT_EQ(detail::to_hex(bytes), "0001ffab");
}

TEST(CryptoComCrypto, HmacSha256_EmptyData_DoesNotCrash) {
    const std::string mac = detail::hmac_sha256("key", "");
    EXPECT_EQ(mac.size(), 32u);  // SHA-256 digest is always 32 bytes
}

// ── params_to_str — the bespoke serialisation (Risk R1) ──────────────────────

TEST(CryptoComParamsToStr, EmptyObject_IsEmptyString) {
    EXPECT_EQ(detail::params_to_str(json::object()), "");
}

TEST(CryptoComParamsToStr, FlatObject_KeysSortedAscending_AsKeyValue) {
    // Insertion order deliberately unsorted; output must be a<1> b<2> c<3>.
    const json params = {{"c", "3"}, {"a", "1"}, {"b", "2"}};
    EXPECT_EQ(detail::params_to_str(params), "a1b2c3");
}

TEST(CryptoComParamsToStr, StringValues_PassThroughVerbatim) {
    const json params = {{"instrument_name", "BTC_USD"}, {"side", "BUY"}};
    // "instrument_name" < "side", so it is emitted first.
    EXPECT_EQ(detail::params_to_str(params), "instrument_nameBTC_USDsideBUY");
}

TEST(CryptoComParamsToStr, NullValue_SerialisesAsLiteralNull) {
    const json params = {{"x", nullptr}};
    EXPECT_EQ(detail::params_to_str(params), "xnull");
}

TEST(CryptoComParamsToStr, ArrayOfObjects_IteratesAndRecurses) {
    const json params = {{"order_list", json::array({{{"a", "1"}}, {{"b", "2"}}})}};
    // "order_list" + str({a:1}) + str({b:2}) = "order_list" + "a1" + "b2".
    EXPECT_EQ(detail::params_to_str(params), "order_lista1b2");
}

// The exec_inst wire shape (plan cryptocom-create-order-exec-inst): a bare string element recurses at
// level+1, sees a non-object, and falls straight to scalar_to_str — so an
// array of strings serialises with no separators, same as an array of objects.
TEST(CryptoComParamsToStr, ArrayOfStrings_IteratesAndRecursesElementwise) {
    const json params = {{"exec_inst", json::array({"POST_ONLY"})}};
    EXPECT_EQ(detail::params_to_str(params), "exec_instPOST_ONLY");
}

TEST(CryptoComParamsToStr, ArrayAndScalarKeys_SortedTogether) {
    const json params = {{"z", "9"}, {"arr", json::array({{{"a", "1"}}})}};
    // sorted keys: "arr" then "z" → "arr" + "a1" + "z" + "9".
    EXPECT_EQ(detail::params_to_str(params), "arra1z9");
}

TEST(CryptoComParamsToStr, IntegerValue_DefensiveStringForm) {
    const json params = {{"n", 42}};
    EXPECT_EQ(detail::params_to_str(params), "n42");
}

// The recursion cap: at MAX_LEVEL the value is serialised whole rather than
// having its keys expanded. (Out of reach for in-scope requests, which never
// nest arrays this deep — exercised here mechanically.)
TEST(CryptoComParamsToStr, RecursionCap_SerialisesWhole) {
    const json obj = {{"a", "1"}, {"b", "2"}};
    EXPECT_EQ(detail::params_to_str(obj, detail::MAX_LEVEL), obj.dump());
    EXPECT_NE(detail::params_to_str(obj, detail::MAX_LEVEL), "a1b2");
}

// ── CryptoComCredentials::sign — digest composition ──────────────────────────

TEST(CryptoComSign, ComposesHexHmacOfDocumentedDigest) {
    const CryptoComCredentials creds{TEST_KEY, TEST_SECRET};
    const json    params = {{"instrument_name", "BTC_USD"}};
    const int64_t id     = 11;
    const int64_t nonce  = 1587846358253;

    const std::string sig = creds.sign("private/get-order-detail", id, params, nonce);

    const std::string digest = std::string("private/get-order-detail") + std::to_string(id) +
                               TEST_KEY + detail::params_to_str(params) + std::to_string(nonce);
    EXPECT_EQ(sig, detail::to_hex(detail::hmac_sha256(TEST_SECRET, digest)));
}

// Same construction-pinning approach as the case above, extended to a params
// object carrying exec_inst (plan cryptocom-create-order-exec-inst) — the array-recursion path in
// params_to_str must not change how sign() composes its digest.
TEST(CryptoComSign, ComposesHexHmacOfDocumentedDigest_WithExecInst) {
    const CryptoComCredentials creds{TEST_KEY, TEST_SECRET};
    const json    params = {{"instrument_name", "BTC_USD"},
                            {"exec_inst", json::array({"POST_ONLY"})}};
    const int64_t id     = 11;
    const int64_t nonce  = 1587846358253;

    const std::string sig = creds.sign("private/create-order", id, params, nonce);

    const std::string digest = std::string("private/create-order") + std::to_string(id) +
                               TEST_KEY + detail::params_to_str(params) + std::to_string(nonce);
    EXPECT_EQ(sig, detail::to_hex(detail::hmac_sha256(TEST_SECRET, digest)));
}

TEST(CryptoComSign, EmptyParams_AuthFrameStyle) {
    // The WS public/auth frame signs method + id + api_key + nonce only.
    const CryptoComCredentials creds{TEST_KEY, TEST_SECRET};
    const int64_t id    = 1;
    const int64_t nonce = 1610905028000;

    const std::string sig = creds.sign("public/auth", id, json::object(), nonce);

    const std::string digest =
        std::string("public/auth") + std::to_string(id) + TEST_KEY + "" + std::to_string(nonce);
    EXPECT_EQ(sig, detail::to_hex(detail::hmac_sha256(TEST_SECRET, digest)));
}

TEST(CryptoComSign, DifferentParams_ProduceDifferentSignatures) {
    const CryptoComCredentials creds{TEST_KEY, TEST_SECRET};
    const std::string a = creds.sign("private/create-order", 1, {{"quantity", "1"}}, 100);
    const std::string b = creds.sign("private/create-order", 1, {{"quantity", "2"}}, 100);
    EXPECT_NE(a, b);
}

// Key order in the params object must not affect the signature (server re-sorts).
TEST(CryptoComSign, ParamKeyOrder_DoesNotAffectSignature) {
    const CryptoComCredentials creds{TEST_KEY, TEST_SECRET};
    const json p1 = {{"a", "1"}, {"b", "2"}};
    const json p2 = {{"b", "2"}, {"a", "1"}};
    EXPECT_EQ(creds.sign("m", 1, p1, 100), creds.sign("m", 1, p2, 100));
}

// ── make_nonce ────────────────────────────────────────────────────────────────

TEST(CryptoComNonce, IsMillisecondScaleAndNonDecreasing) {
    const int64_t a = make_nonce();
    const int64_t b = make_nonce();
    EXPECT_GT(a, 1600000000000LL);  // after 2020-09 in ms — confirms ms, not s
    EXPECT_GE(b, a);
}

// ── CryptoComCredentials::from_file — TOML loader (plan credentials-from-file) ───

TEST(CryptoComCredentialsFromFile, LoadsBothFields) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_cryptocom_creds_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key    = \"CC_KEY\"\n"
             "api_secret = \"CC_SECRET\"\n";
    }

    const auto creds = CryptoComCredentials::from_file("default", dir.string());
    EXPECT_EQ(creds.api_key, "CC_KEY");
    EXPECT_EQ(creds.api_secret, "CC_SECRET");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(CryptoComCredentialsFromFile, ThrowsWhenSecretMissing) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_cryptocom_creds_missing_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key = \"CC_KEY\"\n";  // no api_secret
    }

    EXPECT_THROW(CryptoComCredentials::from_file("default", dir.string()),
                 std::runtime_error);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
