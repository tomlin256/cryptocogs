// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies CoinbaseRestClient::execute() end-to-end via an injected mock HTTP
// performer (no libcurl, no network): public round-trips, the four CB-ACCESS-*
// signing headers on private calls, POST-body signing, status->error mapping,
// and transport-exception folding.

#include "exchange/coinbase/rest_client.hpp"
#include "assert_has_value.hpp"
#include "coinbase_rest_example_json.hpp"
#include "coinbase_account_example_json.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace exchange::coinbase;        // Side, OrderType, TimeInForce
using namespace exchange::coinbase::rest;  // client + request types
namespace fx = coinbase_fixtures;

namespace {

std::string method_str(HttpRequest::Method m) {
    switch (m) {
        case HttpRequest::Method::GET:    return "GET";
        case HttpRequest::Method::POST:   return "POST";
        case HttpRequest::Method::DELETE: return "DELETE";
    }
    return "GET";
}

std::string request_path(const HttpRequest& h) {
    return h.query.empty() ? h.path : h.path + "?" + h.query;
}

CoinbaseCredentials test_creds() {
    // api_secret must be valid base64 (CoinbaseAuth base64-decodes it).
    return CoinbaseCredentials{"my-key", "dGVzdHNlY3JldGtleQ==", "my-pass"};
}

} // namespace

// ── Public ────────────────────────────────────────────────────────────────────

TEST(CoinbaseClient, PublicServerTime_RoundTripUnsigned) {
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::TIME_JSON)};
    });

    auto resp = client.execute(CoinbaseServerTimeRequest{});

    EXPECT_EQ(captured.method, HttpRequest::Method::GET);
    EXPECT_EQ(captured.path, "/time");
    // Public requests are not signed.
    EXPECT_EQ(captured.headers.find("CB-ACCESS-KEY"), captured.headers.end());
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_DOUBLE_EQ(resp.result->epoch, 1781887413.188);
}

// ── Private signing ────────────────────────────────────────────────────────────

TEST(CoinbaseClient, PrivateAccounts_SignsAllFourHeaders) {
    const auto creds = test_creds();
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::ACCOUNTS_JSON)};
    });

    auto resp = client.execute(CoinbaseAccountsRequest{}, creds);

    EXPECT_EQ(captured.headers.at("CB-ACCESS-KEY"), "my-key");
    EXPECT_EQ(captured.headers.at("CB-ACCESS-PASSPHRASE"), "my-pass");
    const std::string ts = captured.headers.at("CB-ACCESS-TIMESTAMP");
    EXPECT_FALSE(ts.empty());
    // CB-ACCESS-SIGN must match the prehash the client actually sent.
    EXPECT_EQ(captured.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(creds.api_secret, ts, method_str(captured.method),
                                    request_path(captured), captured.body));
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->accounts.size(), 2u);
}

TEST(CoinbaseClient, PlaceOrder_PostBodySignedWithJsonContentType) {
    const auto creds = test_creds();
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::ORDER_OPEN_JSON)};
    });

    CoinbasePlaceOrderRequest req;
    req.product_id    = "BTC-USD";
    req.side          = Side::Buy;
    req.type          = OrderType::Limit;
    req.price         = "30000.00";
    req.size          = "0.01";
    req.time_in_force = TimeInForce::GTC;

    auto resp = client.execute(req, creds);

    EXPECT_EQ(captured.method, HttpRequest::Method::POST);
    EXPECT_EQ(captured.path, "/orders");
    EXPECT_EQ(captured.headers.at("Content-Type"), "application/json");
    EXPECT_FALSE(captured.body.empty());
    const std::string ts = captured.headers.at("CB-ACCESS-TIMESTAMP");
    EXPECT_EQ(captured.headers.at("CB-ACCESS-SIGN"),
              detail::coinbase_sign(creds.api_secret, ts, "POST", "/orders", captured.body));
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->id, "d0c5a4f3-0000-4000-8000-000000000010");
}

TEST(CoinbaseClient, CancelOrder_DeleteWithBareStringResult) {
    const auto creds = test_creds();
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::CANCEL_ONE_JSON)};
    });

    CoinbaseCancelOrderRequest req;
    req.order_id = "d0c5a4f3-0000-4000-8000-000000000010";
    auto resp = client.execute(req, creds);

    EXPECT_EQ(captured.method, HttpRequest::Method::DELETE);
    EXPECT_EQ(captured.path, "/orders/d0c5a4f3-0000-4000-8000-000000000010");
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->order_id, "d0c5a4f3-0000-4000-8000-000000000010");
}

// ── Error handling ─────────────────────────────────────────────────────────────

TEST(CoinbaseClient, ErrorStatus_MapsMessageToEnvelopeError) {
    const auto creds = test_creds();
    auto client = make_coinbase_test_client([&](const HttpRequest&) {
        return HttpResponse{401, std::string(R"({"message":"Invalid API Key"})")};
    });

    auto resp = client.execute(CoinbaseAccountsRequest{}, creds);

    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.result.has_value());
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "Invalid API Key");
}

TEST(CoinbaseClient, TransportException_FoldsIntoEnvelope) {
    auto client = make_coinbase_test_client([&](const HttpRequest&) -> HttpResponse {
        throw std::runtime_error("connection refused");
    });

    auto resp = client.execute(CoinbaseServerTimeRequest{});

    EXPECT_FALSE(resp.ok);
    ASSERT_FALSE(resp.errors.empty());
    EXPECT_NE(resp.errors[0].find("connection refused"), std::string::npos);
}

// ── User-Agent (Coinbase rejects requests without one) ───────────────────────

TEST(CoinbaseClient, PublicRequest_SetsDefaultUserAgent) {
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::TIME_JSON)};
    });

    client.execute(CoinbaseServerTimeRequest{});
    EXPECT_EQ(captured.headers.at("User-Agent"), "cryptocogs");
}

TEST(CoinbaseClient, PrivateRequest_SetsDefaultUserAgent) {
    HttpRequest captured;
    auto client = make_coinbase_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::ACCOUNTS_JSON)};
    });

    client.execute(CoinbaseAccountsRequest{}, test_creds());
    EXPECT_EQ(captured.headers.at("User-Agent"), "cryptocogs");
}
