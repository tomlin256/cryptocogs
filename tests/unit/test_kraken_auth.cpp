// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Proves that KrakenAuth::sign() (exchange/kraken/auth.hpp) produces byte-identical
// API-Sign and API-Key headers to the pre-refactor direct KrakenCredentials::sign() path
// for the same path/body inputs — both form-encoded and JSON bodies.
//
// This is the "new path matches the trusted reference" regression net described
// in Step 3 of docs/plans/001-multi-exchange-abstraction.md.

#include "exchange/kraken/auth.hpp"
#include "exchange/common/rest.hpp"

#include <gtest/gtest.h>
#include <string>

using namespace exchange::kraken::rest;
using exchange::rest::HttpRequest;

static const char* TEST_KEY    = "my-api-key";
static const char* TEST_SECRET =
    "kQH5HW/8p1uGOVjbgWA7FunAmGO8lsSUXNsu3eow76sz84Q18fWxnyRzBHCd3pd5"
    "nE9qa99HAZtuZuj6F1huXg==";

static KrakenCredentials make_creds() {
    return KrakenCredentials{TEST_KEY, TEST_SECRET};
}

// Build a form-encoded HttpRequest with the given path/body, call KrakenAuth::sign,
// and return the injected API-Sign value so tests can compare it to the direct path.
static std::string kraken_auth_sign_form(const std::string& path,
                                          const std::string& body) {
    KrakenCredentials creds = make_creds();
    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = path;
    req.body   = body;
    req.headers["Content-Type"] = "application/x-www-form-urlencoded";
    KrakenAuth{creds}.sign(req);
    return req.headers.at("API-Sign");
}

static std::string kraken_auth_sign_json(const std::string& path,
                                          const std::string& body) {
    KrakenCredentials creds = make_creds();
    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = path;
    req.body   = body;
    req.headers["Content-Type"] = "application/json";
    KrakenAuth{creds}.sign(req);
    return req.headers.at("API-Sign");
}

// ── Form-encoded body tests ───────────────────────────────────────────────────

TEST(KrakenAuth, FormEncoded_Balance_MatchesDirectSign) {
    KrakenCredentials creds = make_creds();
    const std::string path  = "/0/private/Balance";
    const std::string nonce = "1616492376594";
    const std::string body  = "nonce=" + nonce;

    EXPECT_EQ(kraken_auth_sign_form(path, body),
              creds.sign(path, nonce, body));
}

TEST(KrakenAuth, FormEncoded_WithExtraParams_MatchesDirectSign) {
    KrakenCredentials creds = make_creds();
    const std::string path  = "/0/private/AddOrder";
    const std::string nonce = "1616492376594";
    const std::string body  = "nonce=" + nonce
                            + "&ordertype=limit&pair=XBTUSD&price=30000&type=buy&volume=1.25";

    EXPECT_EQ(kraken_auth_sign_form(path, body),
              creds.sign(path, nonce, body));
}

TEST(KrakenAuth, FormEncoded_GetWebSocketsToken_MatchesDirectSign) {
    KrakenCredentials creds = make_creds();
    const std::string path  = "/0/private/GetWebSocketsToken";
    const std::string nonce = "9999999999999";
    const std::string body  = "nonce=" + nonce;

    EXPECT_EQ(kraken_auth_sign_form(path, body),
              creds.sign(path, nonce, body));
}

// ── JSON body tests ───────────────────────────────────────────────────────────

TEST(KrakenAuth, JsonBody_MatchesDirectSign) {
    KrakenCredentials creds = make_creds();
    const std::string path  = "/0/private/AddOrderBatch";
    const std::string nonce = "1616492376594";
    // Body as produced by nlohmann/json with std::map (keys sorted alphabetically).
    const std::string body  = R"({"nonce":"1616492376594","orders":[],"pair":"XBTUSD"})";

    EXPECT_EQ(kraken_auth_sign_json(path, body),
              creds.sign(path, nonce, body));
}

TEST(KrakenAuth, JsonBody_CancelBatch_MatchesDirectSign) {
    KrakenCredentials creds = make_creds();
    const std::string path  = "/0/private/CancelOrderBatch";
    const std::string nonce = "9876543210000";
    const std::string body  = R"({"nonce":"9876543210000","orders":["TXN1","TXN2"]})";

    EXPECT_EQ(kraken_auth_sign_json(path, body),
              creds.sign(path, nonce, body));
}

// ── Header injection tests ────────────────────────────────────────────────────

TEST(KrakenAuth, InjectsApiKeyHeader) {
    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/0/private/Balance";
    req.body   = "nonce=1234567890000";
    req.headers["Content-Type"] = "application/x-www-form-urlencoded";
    KrakenAuth{make_creds()}.sign(req);
    EXPECT_EQ(req.headers.at("API-Key"), std::string(TEST_KEY));
}

TEST(KrakenAuth, InjectsNonEmptyApiSignHeader) {
    HttpRequest req;
    req.method = HttpRequest::Method::POST;
    req.path   = "/0/private/Balance";
    req.body   = "nonce=1234567890000";
    req.headers["Content-Type"] = "application/x-www-form-urlencoded";
    KrakenAuth{make_creds()}.sign(req);
    EXPECT_FALSE(req.headers.at("API-Sign").empty());
}

// ── IRestAuth interface conformance ──────────────────────────────────────────

TEST(KrakenAuth, ImplementsIRestAuth) {
    static_assert(std::is_base_of_v<exchange::rest::IRestAuth, KrakenAuth>,
                  "KrakenAuth must derive exchange::rest::IRestAuth");
}

// ── base64_decode ────────────────────────────────────────────────────────────

// A 64-byte, 88-character input (the shape of a real API secret) decodes to
// exactly the bytes it was encoded from. The vector is Python's base64.b64encode
// over bytes((i * 7 + 3) % 256 for i in range(64)), so it does not lean on this
// library's own encoder.
//
// Any input over five characters carries the decoder's accumulator past 31 bits.
// This pins the bytes, which is all an assertion can see: the shift itself is
// undefined behaviour that every compiler in use wraps, and only a UBSan build of
// this test reports it (issue #23).
TEST(KrakenAuth, Base64Decode_LongInputDecodesToExactBytes) {
    std::string expected;
    for (int i = 0; i < 64; ++i)
        expected.push_back(static_cast<char>((i * 7 + 3) % 256));

    EXPECT_EQ(detail::base64_decode(
                  "AwoRGB8mLTQ7QklQV15lbHN6gYiPlp2kq7K5wMfO1dzj6vH4/wYNFBsiKTA3PkVM"
                  "U1phaG92fYSLkpmgp661vA=="),
              expected);
}
