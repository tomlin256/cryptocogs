// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// End-to-end signed round-trips through BinanceRestClient::execute(req, auth)
// with an injected mock HTTP performer — the Binance analog of Kraken's
// test_client.cpp. Each test asserts the exact signed wire request
// (deterministic via BinanceAuth's injectable ClockFn) and the parsed
// RestResponse<T>. No network or libcurl involved.

#include "exchange/binance/rest_client.hpp"
#include "assert_has_value.hpp"
#include "binance_account_example_json.hpp"
#include "binance_rest_example_json.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>

using namespace exchange::binance::rest;
using exchange::rest::HttpRequest;
using exchange::rest::HttpResponse;
namespace fixtures = exchange::binance::rest::test;

namespace {

// Literals, not std::string: a namespace-scope std::string allocates in its initialiser, which
// can throw (bugprone-throwing-static-initialization). Each use builds or compares a std::string.
constexpr int64_t     FIXED_TS   = 1499827319559LL;
constexpr const char* kApiKey    = "test-key";
constexpr const char* kSecretKey = "test-secret";

BinanceAuth make_fixed_auth() {
    return BinanceAuth{BinanceCredentials{kApiKey, kSecretKey},
                       []() { return FIXED_TS; }};
}

// Expected signature recomputed over the exact expected payload with the same
// primitives BinanceAuth uses — deterministic, no hard-coded magic hex.
std::string expected_sig(const std::string& payload) {
    return detail::to_hex(detail::hmac_sha256(kSecretKey, payload));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// GET signed round-trip — params (here: none) + auth land in the query
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceClient, GetAccount_SignedRoundTrip) {
    const std::string payload = "timestamp=1499827319559&recvWindow=5000";
    const std::string sig     = expected_sig(payload);

    auto client = make_binance_test_client(
        [&](const HttpRequest& http) -> HttpResponse {
            EXPECT_EQ(http.method, HttpRequest::Method::GET);
            EXPECT_EQ(http.path, "/api/v3/account");
            EXPECT_EQ(http.query, payload + "&signature=" + sig);
            EXPECT_TRUE(http.body.empty());
            EXPECT_EQ(http.headers.at("X-MBX-APIKEY"), kApiKey);
            return {200, fixtures::kAccountJson};
        });

    auto auth = make_fixed_auth();
    auto resp = client.execute(BinanceAccountRequest{}, auth);

    EXPECT_TRUE(resp.ok);
    EXPECT_TRUE(resp.errors.empty());
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->uid, 354937868LL);
}

// ─────────────────────────────────────────────────────────────────────────────
// POST signed round-trip — params + auth land in the body, query stays empty
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceClient, PostNewOrder_SignedRoundTrip) {
    const std::string payload =
        "symbol=BTCUSDT&side=SELL&type=MARKET&quantity=10.00000000"
        "&timestamp=1499827319559&recvWindow=5000";
    const std::string sig = expected_sig(payload);

    auto client = make_binance_test_client(
        [&](const HttpRequest& http) -> HttpResponse {
            EXPECT_EQ(http.method, HttpRequest::Method::POST);
            EXPECT_EQ(http.path, "/api/v3/order");
            EXPECT_TRUE(http.query.empty());
            EXPECT_EQ(http.body, payload + "&signature=" + sig);
            EXPECT_EQ(http.headers.at("Content-Type"),
                      "application/x-www-form-urlencoded");
            EXPECT_EQ(http.headers.at("X-MBX-APIKEY"), kApiKey);
            return {200, fixtures::kNewOrderFullJson};
        });

    BinanceNewOrderRequest req;
    req.symbol   = "BTCUSDT";
    req.side     = exchange::Side::Sell;
    req.type     = exchange::OrderType::Market;
    req.quantity = "10.00000000";

    auto auth = make_fixed_auth();
    auto resp = client.execute(req, auth);

    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_EQ(resp.result->fills.size(), 2u);
    EXPECT_EQ(resp.result->status, exchange::OrderStatus::Filled);
}

// ─────────────────────────────────────────────────────────────────────────────
// DELETE signed round-trip — params + auth land in the query, body stays empty
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceClient, DeleteCancelOrder_SignedRoundTrip) {
    const std::string payload =
        "symbol=LTCBTC&orderId=4&timestamp=1499827319559&recvWindow=5000";
    const std::string sig = expected_sig(payload);

    auto client = make_binance_test_client(
        [&](const HttpRequest& http) -> HttpResponse {
            EXPECT_EQ(http.method, HttpRequest::Method::DELETE);
            EXPECT_EQ(http.path, "/api/v3/order");
            EXPECT_EQ(http.query, payload + "&signature=" + sig);
            EXPECT_TRUE(http.body.empty());
            return {200, fixtures::kCancelOrderJson};
        });

    BinanceCancelOrderRequest req;
    req.symbol   = "LTCBTC";
    req.order_id = 4;

    auto auth = make_fixed_auth();
    auto resp = client.execute(req, auth);

    EXPECT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->status, exchange::OrderStatus::Canceled);
    EXPECT_EQ(resp.result->orig_client_order_id, "myOrder1");
}

// ─────────────────────────────────────────────────────────────────────────────
// Error paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(BinanceClient, ApiErrorEnvelope_SurfacesMsg) {
    auto client = make_binance_test_client(
        [](const HttpRequest&) -> HttpResponse {
            return {400, fixtures::kErrorJson};
        });

    auto auth = make_fixed_auth();
    auto resp = client.execute(BinanceAccountRequest{}, auth);

    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "Invalid symbol.");
    EXPECT_FALSE(resp.result.has_value());
}

TEST(BinanceClient, Non2xxWithoutErrorBody_SurfacesHttpStatus) {
    auto client = make_binance_test_client(
        [](const HttpRequest&) -> HttpResponse {
            return {500, "{}"};
        });

    auto auth = make_fixed_auth();
    auto resp = client.execute(BinanceAccountRequest{}, auth);

    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "HTTP 500");
    EXPECT_FALSE(resp.result.has_value());
}

// ── Review M2: transport / parse / signing failures fold into ok=false ────────

TEST(BinanceClient, TransportFailureFoldsIntoErrorResponse) {
    auto client = make_binance_test_client(
        [](const HttpRequest&) -> HttpResponse { throw std::runtime_error("network down"); });

    const auto resp = client.execute(BinancePingRequest{});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.errors.empty());
}

TEST(BinanceClient, MalformedBodyFoldsIntoErrorResponse) {
    auto client = make_binance_test_client(
        [](const HttpRequest&) -> HttpResponse { return HttpResponse{200, "<html>not json</html>"}; });

    const auto resp = client.execute(BinancePingRequest{});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.errors.empty());
}

// M1 + M2 composition: a rejected non-HMAC algorithm surfaces as ok=false, not
// an escaped exception (sign() throws inside execute()'s try).
TEST(BinanceClient, NonHmacAlgorithmFoldsIntoErrorResponse) {
    auto client = make_binance_test_client(
        [](const HttpRequest&) -> HttpResponse { return HttpResponse{200, "{}"}; });

    BinanceCredentials creds;
    creds.api_key    = "k";
    creds.secret_key = "s";
    creds.algorithm  = BinanceSignAlgorithm::Rsa;

    const auto resp = client.execute(BinanceAccountRequest{}, BinanceAuth(creds));
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.errors.empty());
}
