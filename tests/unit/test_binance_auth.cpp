// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies exchange::binance::rest::BinanceAuth and the supporting HMAC-SHA256
// / hex-encoding primitives.
//
// Signing differences from Kraken that these tests explicitly cover:
//   Digest         : HMAC-SHA256  (Kraken: SHA512)
//   Signed payload : query_string + body (Kraken: path + SHA256(nonce + body))
//   Output encoding: lowercase hex  (Kraken: base64)
//   Anti-replay    : timestamp (ms) injected by sign()  (Kraken: nonce in build())
//   Key material   : raw UTF-8 secret bytes  (Kraken: base64-decoded)

#include "exchange/binance/auth.hpp"
#include "exchange/common/rest.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>  // getpid

using namespace exchange::binance::rest;
using exchange::rest::HttpRequest;

// ── Binance published test constants ─────────────────────────────────────────
//
// Source: Binance SPOT REST API documentation key/secret pair, with parameters
// from the "SIGNED endpoint examples" section.  The expected signature is
// independently verified with:
//   echo -n "<params>" | openssl dgst -sha256 -hmac "<secret>"
//
// API key   : vmPUZE6mv9SD5VNHk4HlWFsOr6aKE2zvsw0MuIgwCIPy6utIco14y7Ju91duEh8A
// Secret key: NhqRioT9q50mvzEPbXFD6M5sJ4XCBCrWXa58N4FhLQlmMBWCWPKm3zNv2V0SZEF2
// Total params (queryString + requestBody, already URL-encoded):
//   symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1
//   &recvWindow=5000&timestamp=1499827319559
// Expected HMAC-SHA256 hex (openssl confirmed):
//   20a0e317afdfe97df3b45ffa590995a34e89d12c11e7d02cb26737624018f020

static const char* BINANCE_TEST_API_KEY =
    "vmPUZE6mv9SD5VNHk4HlWFsOr6aKE2zvsw0MuIgwCIPy6utIco14y7Ju91duEh8A";
static const char* BINANCE_TEST_SECRET =
    "NhqRioT9q50mvzEPbXFD6M5sJ4XCBCrWXa58N4FhLQlmMBWCWPKm3zNv2V0SZEF2";
static const char* BINANCE_TEST_TOTAL_PARAMS =
    "symbol=LTCBTC&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=0.1"
    "&recvWindow=5000&timestamp=1499827319559";
static const char* BINANCE_TEST_EXPECTED_SIG =
    "20a0e317afdfe97df3b45ffa590995a34e89d12c11e7d02cb26737624018f020";

static const int64_t FIXED_TS = 1499827319559LL;

// ── Crypto primitive tests ────────────────────────────────────────────────────

// Validates HMAC-SHA256 + hex encoding against Binance's authoritative test vector.
// This is the central correctness proof for the signing implementation.
TEST(BinanceCrypto, HmacSha256HexMatchesPublishedVector) {
    using namespace exchange::binance::rest::detail;
    EXPECT_EQ(to_hex(hmac_sha256(BINANCE_TEST_SECRET, BINANCE_TEST_TOTAL_PARAMS)),
              BINANCE_TEST_EXPECTED_SIG);
}

// Verifies the to_hex output is lowercase (Binance docs specify lowercase hex).
TEST(BinanceCrypto, ToHex_IsLowercase) {
    using namespace exchange::binance::rest::detail;
    std::string hex = to_hex(hmac_sha256(BINANCE_TEST_SECRET, BINANCE_TEST_TOTAL_PARAMS));
    for (char c : hex) {
        EXPECT_FALSE(c >= 'A' && c <= 'F') << "uppercase hex char in: " << hex;
    }
}

// Empty input edge case — hmac_sha256 over empty message should not crash.
TEST(BinanceCrypto, HmacSha256_EmptyData_DoesNotCrash) {
    using namespace exchange::binance::rest::detail;
    std::string result = to_hex(hmac_sha256("key", ""));
    EXPECT_EQ(result.size(), 64u);  // 32 bytes → 64 hex chars
}

// ── BinanceAuth::sign() — GET request ────────────────────────────────────────

// sign() appends timestamp + recvWindow to the query string of a GET request,
// then appends &signature=<hex>.
TEST(BinanceAuth, GetRequest_TimestampAndSignatureAppendedToQuery) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET,
                             BinanceSignAlgorithm::HmacSha256, 5000};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/api/v3/account";
    // Empty query — sign() injects everything.

    auth.sign(req);

    // Compute the expected signature over the injected params.
    std::string expected_payload = "timestamp=1499827319559&recvWindow=5000";
    std::string expected_sig =
        detail::to_hex(detail::hmac_sha256(BINANCE_TEST_SECRET, expected_payload));

    EXPECT_EQ(req.query,
              "timestamp=1499827319559&recvWindow=5000&signature=" + expected_sig);
    EXPECT_TRUE(req.body.empty());
}

// sign() preserves and appends to an existing non-empty query string.
TEST(BinanceAuth, GetRequest_ExistingQuery_AppendedCorrectly) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET,
                             BinanceSignAlgorithm::HmacSha256, 5000};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/api/v3/order";
    req.query  = "symbol=LTCBTC";

    auth.sign(req);

    // Payload = existing query + injected timestamp params (body is empty).
    std::string expected_payload = "symbol=LTCBTC&timestamp=1499827319559&recvWindow=5000";
    std::string expected_sig =
        detail::to_hex(detail::hmac_sha256(BINANCE_TEST_SECRET, expected_payload));

    EXPECT_EQ(req.query,
              "symbol=LTCBTC&timestamp=1499827319559&recvWindow=5000&signature=" + expected_sig);
}

// ── BinanceAuth::sign() — POST request ───────────────────────────────────────

// sign() appends timestamp + recvWindow to the POST body, computes signature
// over query + body (where query is typically empty for POST), appends &signature
// to the body, and sets Content-Type if absent.
TEST(BinanceAuth, PostRequest_TimestampAndSignatureAppendedToBody) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET,
                             BinanceSignAlgorithm::HmacSha256, 5000};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/api/v3/order";
    req.body   = "symbol=BTCUSDT&side=BUY&type=MARKET&quantity=0.01";

    auth.sign(req);

    std::string expected_body_before_sig =
        "symbol=BTCUSDT&side=BUY&type=MARKET&quantity=0.01"
        "&timestamp=1499827319559&recvWindow=5000";
    // payload = query (empty) + body (after timestamp injection)
    std::string expected_sig =
        detail::to_hex(detail::hmac_sha256(BINANCE_TEST_SECRET, expected_body_before_sig));

    EXPECT_EQ(req.body, expected_body_before_sig + "&signature=" + expected_sig);
    EXPECT_TRUE(req.query.empty());
}

// sign() sets Content-Type for POST if the caller did not pre-set it.
TEST(BinanceAuth, PostRequest_SetsContentTypeIfAbsent) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/api/v3/order";
    req.body   = "symbol=BTCUSDT";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("Content-Type"), "application/x-www-form-urlencoded");
}

// sign() does not override a Content-Type the caller already set.
TEST(BinanceAuth, PostRequest_PreservesExistingContentType) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/api/v3/order";
    req.body   = "{}";
    req.headers["Content-Type"] = "application/json";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("Content-Type"), "application/json");
}

// ── BinanceAuth::sign() — header injection ───────────────────────────────────

TEST(BinanceAuth, InjectsXMbxApiKeyHeader) {
    BinanceCredentials creds{BINANCE_TEST_API_KEY, BINANCE_TEST_SECRET};
    BinanceAuth auth{creds, [](){ return FIXED_TS; }};

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/api/v3/ping";

    auth.sign(req);

    EXPECT_EQ(req.headers.at("X-MBX-APIKEY"), std::string(BINANCE_TEST_API_KEY));
}

// ── IRestAuth interface conformance ──────────────────────────────────────────

TEST(BinanceAuth, ImplementsIRestAuth) {
    static_assert(std::is_base_of_v<exchange::rest::IRestAuth, BinanceAuth>,
                  "BinanceAuth must derive exchange::rest::IRestAuth");
}

// ── Review M1: reject non-HMAC algorithms instead of silently HMAC-signing ────

TEST(BinanceAuth, RejectsNonHmacAlgorithm) {
    BinanceCredentials creds;
    creds.api_key    = "k";
    creds.secret_key = "s";

    HttpRequest req;
    req.method = HttpRequest::Method::GET;
    req.path   = "/api/v3/account";

    creds.algorithm = BinanceSignAlgorithm::Rsa;
    EXPECT_THROW(BinanceAuth(creds).sign(req), std::invalid_argument);

    creds.algorithm = BinanceSignAlgorithm::Ed25519;
    EXPECT_THROW(BinanceAuth(creds).sign(req), std::invalid_argument);

    // The default (HMAC-SHA256) still signs without throwing.
    creds.algorithm = BinanceSignAlgorithm::HmacSha256;
    EXPECT_NO_THROW(BinanceAuth(creds).sign(req));
}

// ── BinanceCredentials::from_file — TOML loader (plan credentials-from-file) ───

TEST(BinanceCredentialsFromFile, LoadsTomlAndPreservesDefaults) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_binance_creds_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key    = \"BNB_KEY\"\n"
             "api_secret = \"BNB_SECRET\"\n";
    }

    const auto creds = BinanceCredentials::from_file("default", dir.string());
    EXPECT_EQ(creds.api_key, "BNB_KEY");
    EXPECT_EQ(creds.secret_key, "BNB_SECRET");  // api_secret -> secret_key
    // Non-secret config keeps struct defaults — from_file loads keys only.
    EXPECT_EQ(creds.algorithm, BinanceSignAlgorithm::HmacSha256);
    EXPECT_EQ(creds.recv_window_ms, 5000);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(BinanceCredentialsFromFile, ThrowsWhenSecretMissing) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_binance_creds_missing_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key = \"BNB_KEY\"\n";  // no api_secret
    }

    EXPECT_THROW(BinanceCredentials::from_file("default", dir.string()),
                 std::runtime_error);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
