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
// in Step 3 of docs/plans/multi-exchange-abstraction.md.

#include "exchange/kraken/auth.hpp"
#include "exchange/common/rest.hpp"

#include <gtest/gtest.h>
#include <initializer_list>
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

// ── base64_encode / base64_decode — known answers ────────────────────────────
//
// The signature is base64(HMAC(base64_decode(secret), ...)), so a wrong shift in either helper changes every
// signature. The vectors are RFC 4648 section 10, then bytes with the high bit set (0x80 and up), which are the bytes
// the encoder's shifts must treat as unsigned. Each high-bit text was checked with the system `base64` and with
// `openssl base64`, so none leans on this library's own encoder.

static std::string b64_encode(const std::string& bytes) {
    return detail::base64_encode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

// The bytes of `v` as a string, so a vector can hold 0x00 and 0xFF.
static std::string raw_bytes(std::initializer_list<unsigned char> v) {
    std::string s;
    for (const unsigned char b : v)
        s.push_back(static_cast<char>(b));
    return s;
}

TEST(KrakenAuth, Base64Encode_Rfc4648Vectors) {
    EXPECT_EQ(b64_encode(""),       "");
    EXPECT_EQ(b64_encode("f"),      "Zg==");
    EXPECT_EQ(b64_encode("fo"),     "Zm8=");
    EXPECT_EQ(b64_encode("foo"),    "Zm9v");
    EXPECT_EQ(b64_encode("foob"),   "Zm9vYg==");
    EXPECT_EQ(b64_encode("fooba"),  "Zm9vYmE=");
    EXPECT_EQ(b64_encode("foobar"), "Zm9vYmFy");
}

TEST(KrakenAuth, Base64Decode_Rfc4648Vectors) {
    EXPECT_EQ(detail::base64_decode(""),         "");
    EXPECT_EQ(detail::base64_decode("Zg=="),     "f");
    EXPECT_EQ(detail::base64_decode("Zm8="),     "fo");
    EXPECT_EQ(detail::base64_decode("Zm9v"),     "foo");
    EXPECT_EQ(detail::base64_decode("Zm9vYg=="), "foob");
    EXPECT_EQ(detail::base64_decode("Zm9vYmE="), "fooba");
    EXPECT_EQ(detail::base64_decode("Zm9vYmFy"), "foobar");
}

TEST(KrakenAuth, Base64Encode_HighBitBytes) {
    EXPECT_EQ(b64_encode(raw_bytes({0xFF})),                   "/w==");
    EXPECT_EQ(b64_encode(raw_bytes({0xFF, 0xFE})),             "//4=");
    EXPECT_EQ(b64_encode(raw_bytes({0xFF, 0xFF, 0xFF})),       "////");
    EXPECT_EQ(b64_encode(raw_bytes({0x80})),                   "gA==");
    EXPECT_EQ(b64_encode(raw_bytes({0x80, 0x80, 0x80})),       "gICA");
    EXPECT_EQ(b64_encode(raw_bytes({0x80, 0x00, 0x00})),       "gAAA");
    EXPECT_EQ(b64_encode(raw_bytes({0x00, 0xFF, 0x80})),       "AP+A");
    EXPECT_EQ(b64_encode(raw_bytes({0xFF, 0x00, 0xFF, 0x80})), "/wD/gA==");
}

TEST(KrakenAuth, Base64Decode_HighBitBytes) {
    EXPECT_EQ(detail::base64_decode("/w=="),     raw_bytes({0xFF}));
    EXPECT_EQ(detail::base64_decode("//4="),     raw_bytes({0xFF, 0xFE}));
    EXPECT_EQ(detail::base64_decode("////"),     raw_bytes({0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(detail::base64_decode("gA=="),     raw_bytes({0x80}));
    EXPECT_EQ(detail::base64_decode("gICA"),     raw_bytes({0x80, 0x80, 0x80}));
    EXPECT_EQ(detail::base64_decode("gAAA"),     raw_bytes({0x80, 0x00, 0x00}));
    EXPECT_EQ(detail::base64_decode("AP+A"),     raw_bytes({0x00, 0xFF, 0x80}));
    EXPECT_EQ(detail::base64_decode("/wD/gA=="), raw_bytes({0xFF, 0x00, 0xFF, 0x80}));
}

// ── sign() — known answers ───────────────────────────────────────────────────
//
// API-Sign = base64(HMAC-SHA512(base64_decode(secret), uri_path + SHA256(nonce + post_body))). These values were computed
// with `openssl dgst` and the system `base64`, not with this library, so they pin the whole path (both base64 helpers
// included) to an outside answer. TEST_SECRET decodes to bytes above 0x7F, and so do the HMACs.
TEST(KrakenAuth, Sign_KnownAnswers) {
    const KrakenCredentials creds = make_creds();

    EXPECT_EQ(creds.sign("/0/private/Balance", "1616492376594", "nonce=1616492376594"),
              "1nH4vwR+8FHiYh1QT649xXkGd3JR3x0DWkgv3u9Ed/Qqv6KPtgQpEU4m+Emb/VgpEji3j1XNwI+HCbfXxmrTOg==");

    EXPECT_EQ(creds.sign("/0/private/AddOrder", "1616492376594",
                         "nonce=1616492376594&ordertype=limit&pair=XBTUSD&price=30000&type=buy&volume=1.25"),
              "PpisNDhZ/IDRMvc96GT6oFk7JD6ZHvn4JM5i17ILN6SUlSr8KYY+issX7+dmLDblVP+yaDZzcg21+AOliLeuIg==");

    EXPECT_EQ(creds.sign("/0/private/AddOrderBatch", "1616492376594",
                         R"({"nonce":"1616492376594","orders":[],"pair":"XBTUSD"})"),
              "dzZMVrzboYrTwmL07EwOkWy24cLDsc6KcQn/Izkj7pITZe1S9ozik9KgMXFfIaPJ3mmD3Igj6LcBps22ZfXVsQ==");
}
