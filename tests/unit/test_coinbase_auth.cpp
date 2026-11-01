// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies exchange::coinbase::rest::CoinbaseAuth and its base64 / HMAC-SHA256
// primitives.
//
// Coinbase signing scheme (per docs.cdp.coinbase.com/exchange):
//   prehash   = timestamp + method + requestPath + body  (requestPath includes
//               the query string)
//   signature = base64( HMAC-SHA256( base64_decode(secret), prehash ) )
//   headers   = CB-ACCESS-KEY / -SIGN / -TIMESTAMP / -PASSPHRASE
//
// No official Coinbase signature vector is published, so the HMAC-SHA256
// primitive is proven against the authoritative RFC 4231 test vector and base64
// against known vectors; sign() is then verified by pinning its prehash
// assembly against an independently-assembled coinbase_sign() call.

#include "exchange/coinbase/auth.hpp"
#include "exchange/common/rest.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <unistd.h>  // getpid

using namespace exchange::coinbase::rest;
using exchange::rest::HttpRequest;

namespace {

// Lowercase-hex helper, local to the test (the adapter has no to_hex — Coinbase
// signatures are base64, not hex). Used only to compare against RFC vectors.
std::string to_hex(const std::string& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : bytes)
        oss << std::setw(2) << static_cast<unsigned int>(c);
    return oss.str();
}

// A valid base64 secret (decodes to "testsecretkey"); used as an opaque
// Coinbase api_secret throughout.
const char* TEST_KEY        = "test-api-key";
const char* TEST_SECRET     = "dGVzdHNlY3JldGtleQ==";
const char* TEST_PASSPHRASE = "test-passphrase";

constexpr int64_t FIXED_TS = 1700000000;  // Unix epoch SECONDS

} // namespace

// ── HMAC-SHA256 primitive — RFC 4231 Test Case 2 ─────────────────────────────
//
// key  = "Jefe"
// data = "what do ya want for nothing?"
// HMAC-SHA256 = 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
TEST(CoinbaseCrypto, HmacSha256_MatchesRfc4231Vector) {
    const std::string mac = detail::hmac_sha256("Jefe", "what do ya want for nothing?");
    EXPECT_EQ(to_hex(mac),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(CoinbaseCrypto, HmacSha256_EmptyData_DoesNotCrash) {
    const std::string mac = detail::hmac_sha256("key", "");
    EXPECT_EQ(mac.size(), 32u);  // SHA-256 digest is always 32 bytes
}

// ── base64 — known vectors + round-trip ──────────────────────────────────────

TEST(CoinbaseCrypto, Base64Encode_KnownVectors) {
    auto enc = [](const std::string& s) {
        return detail::base64_encode(
            reinterpret_cast<const unsigned char*>(s.data()), s.size());
    };
    EXPECT_EQ(enc("Man"), "TWFu");
    EXPECT_EQ(enc("Ma"),  "TWE=");
    EXPECT_EQ(enc("M"),   "TQ==");
    EXPECT_EQ(enc(""),    "");
}

TEST(CoinbaseCrypto, Base64Decode_KnownVectorAndRoundTrip) {
    EXPECT_EQ(detail::base64_decode("TWFu"), "Man");

    const std::string original = "the quick brown fox \x01\x02\xff";
    const std::string encoded  = detail::base64_encode(
        reinterpret_cast<const unsigned char*>(original.data()), original.size());
    EXPECT_EQ(detail::base64_decode(encoded), original);
}

// A 64-byte, 88-character input (the shape of a real API secret) decodes to
// exactly the bytes it was encoded from. The vector is Python's base64.b64encode
// over bytes((i * 7 + 3) % 256 for i in range(64)), so it does not lean on this
// library's own encoder.
//
// Any input over five characters carries the decoder's accumulator past 31 bits.
// This pins the bytes, which is all an assertion can see: the shift itself is
// undefined behaviour that every compiler in use wraps, and only a UBSan build of
// this test reports it (issue #23).
TEST(CoinbaseCrypto, Base64Decode_LongInputDecodesToExactBytes) {
    std::string expected;
    for (int i = 0; i < 64; ++i)
        expected.push_back(static_cast<char>((i * 7 + 3) % 256));

    EXPECT_EQ(detail::base64_decode(
                  "AwoRGB8mLTQ7QklQV15lbHN6gYiPlp2kq7K5wMfO1dzj6vH4/wYNFBsiKTA3PkVM"
                  "U1phaG92fYSLkpmgp661vA=="),
              expected);
}

// ── coinbase_sign — composition ──────────────────────────────────────────────

// coinbase_sign must equal base64(HMAC-SHA256(base64_decode(secret), prehash)).
TEST(CoinbaseCrypto, CoinbaseSign_ComposesBase64HmacOfPrehash) {
    const std::string ts   = "1700000000";
    const std::string sig  = detail::coinbase_sign(TEST_SECRET, ts, "GET", "/accounts", "");

    const std::string prehash = ts + "GET" + "/accounts" + "";
    const std::string mac     = detail::hmac_sha256(detail::base64_decode(TEST_SECRET), prehash);
    const std::string expect  = detail::base64_encode(
        reinterpret_cast<const unsigned char*>(mac.data()), mac.size());

    EXPECT_EQ(sig, expect);
}

// ── CoinbaseAuth::sign() — header injection + prehash assembly ───────────────

TEST(CoinbaseAuth, GetRequest_SetsAllFourHeaders) {
    CoinbaseAuth auth{{TEST_KEY, TEST_SECRET, TEST_PASSPHRASE}, [] { return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/accounts";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("CB-ACCESS-KEY"),        TEST_KEY);
    EXPECT_EQ(req.headers.at("CB-ACCESS-PASSPHRASE"), TEST_PASSPHRASE);
    EXPECT_EQ(req.headers.at("CB-ACCESS-TIMESTAMP"),  std::to_string(FIXED_TS));
    // CB-ACCESS-SIGN matches the documented prehash (ts + METHOD + path + body).
    EXPECT_EQ(req.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(TEST_SECRET, std::to_string(FIXED_TS),
                                    "GET", "/accounts", ""));
}

// The query string is part of the signed requestPath.
TEST(CoinbaseAuth, GetRequest_WithQuery_SignsRequestPathIncludingQuery) {
    CoinbaseAuth auth{{TEST_KEY, TEST_SECRET, TEST_PASSPHRASE}, [] { return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/orders";
    req.query  = "status=open&product_id=BTC-USD";

    auth.sign(req);

    const std::string ts = std::to_string(FIXED_TS);
    EXPECT_EQ(req.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(TEST_SECRET, ts, "GET",
                                    "/orders?status=open&product_id=BTC-USD", ""));
    // Including the query must actually change the signature.
    EXPECT_NE(req.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(TEST_SECRET, ts, "GET", "/orders", ""));
}

// POST signs the JSON body and sets Content-Type when the caller did not.
TEST(CoinbaseAuth, PostRequest_SignsBodyAndSetsJsonContentType) {
    CoinbaseAuth auth{{TEST_KEY, TEST_SECRET, TEST_PASSPHRASE}, [] { return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/orders";
    req.body   = R"({"type":"limit","side":"buy","product_id":"BTC-USD"})";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("Content-Type"), "application/json");
    EXPECT_EQ(req.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(TEST_SECRET, std::to_string(FIXED_TS),
                                    "POST", "/orders", req.body));
}

TEST(CoinbaseAuth, PostRequest_PreservesExistingContentType) {
    CoinbaseAuth auth{{TEST_KEY, TEST_SECRET, TEST_PASSPHRASE}, [] { return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/orders";
    req.body   = "{}";
    req.headers["Content-Type"] = "application/json; charset=utf-8";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("Content-Type"), "application/json; charset=utf-8");
}

// DELETE (order cancel) signs with the DELETE method token.
TEST(CoinbaseAuth, DeleteRequest_SignsWithDeleteMethod) {
    CoinbaseAuth auth{{TEST_KEY, TEST_SECRET, TEST_PASSPHRASE}, [] { return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::DELETE;
    req.path   = "/orders/abc-123";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(TEST_SECRET, std::to_string(FIXED_TS),
                                    "DELETE", "/orders/abc-123", ""));
}

// ── IRestAuth conformance ─────────────────────────────────────────────────────

TEST(CoinbaseAuth, ImplementsIRestAuth) {
    static_assert(std::is_base_of_v<exchange::rest::IRestAuth, CoinbaseAuth>,
                  "CoinbaseAuth must derive exchange::rest::IRestAuth");
}

// ── CoinbaseCredentials::from_file — TOML loader (plan 021) ──────────────────

TEST(CoinbaseCredentialsFromFile, LoadsAllThreeFields) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_coinbase_creds_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key    = \"CB_KEY\"\n"
             "api_secret = \"CB_SECRET\"\n"
             "passphrase = \"CB_PASS\"\n";
    }

    const auto creds = CoinbaseCredentials::from_file("default", dir.string());
    EXPECT_EQ(creds.api_key, "CB_KEY");
    EXPECT_EQ(creds.api_secret, "CB_SECRET");
    EXPECT_EQ(creds.passphrase, "CB_PASS");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(CoinbaseCredentialsFromFile, ThrowsWhenPassphraseMissing) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_coinbase_creds_missing_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key    = \"CB_KEY\"\n"
             "api_secret = \"CB_SECRET\"\n";  // no passphrase
    }

    EXPECT_THROW(CoinbaseCredentials::from_file("default", dir.string()),
                 std::runtime_error);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
