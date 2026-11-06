// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// Verifies CryptoComRestClient::execute() end-to-end via an injected mock HTTP
// performer (no libcurl, no network): public round-trips, the signed envelope on
// private calls (api_key/nonce/sig in the body, sig recomputed), code->error
// mapping, transport-exception folding, and unique correlation ids. The
// exception is the last test: it makes the production client talk to a
// SilentListener on loopback, to show that its HttpTimeouts reach the
// transport (plan 042).

#include "exchange/cryptocom/rest_client.hpp"

#include "assert_has_value.hpp"
#include "cryptocom_account_example_json.hpp"
#include "cryptocom_rest_example_json.hpp"
#include "silent_listener.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using namespace exchange::cryptocom;
using namespace exchange::cryptocom::rest;
using json = nlohmann::json;
namespace fx = cryptocom_fixtures;

namespace {
// A function-local static, built on first use: a namespace-scope const would allocate in its static initialiser, which can throw
// (bugprone-throwing-static-initialization).
const CryptoComCredentials& creds() {
    static const CryptoComCredentials c{"my-key", "my-secret"};
    return c;
}

// Recompute the body's sig from its own id/params/nonce — pins client signing.
void expect_sig_matches(const json& body, const std::string& method) {
    ASSERT_TRUE(body.contains("sig"));
    EXPECT_EQ(body.at("sig"),
              creds().sign(method, body.at("id").get<int64_t>(),
                           body.at("params"), body.at("nonce").get<int64_t>()));
}
} // namespace

// ── Public ────────────────────────────────────────────────────────────────────

TEST(CryptoComClient, PublicTickers_RoundTripUnsigned) {
    HttpRequest captured;
    auto client = make_cryptocom_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::TICKERS)};
    });

    CryptoComTickersRequest req;
    req.instrument_name = "BTC_USD";
    auto resp = client.execute(req);

    EXPECT_EQ(captured.method, HttpRequest::Method::GET);
    EXPECT_EQ(captured.path, "/exchange/v1/public/get-tickers");
    EXPECT_EQ(captured.query, "instrument_name=BTC_USD");
    EXPECT_TRUE(captured.body.empty());  // public requests are unsigned
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_EQ(resp.result->tickers.size(), 1u);
    EXPECT_EQ(resp.result->tickers[0].instrument_name, "BTC_USD");
}

// ── Private signing ────────────────────────────────────────────────────────────

TEST(CryptoComClient, PrivateUserBalance_SignsEnvelope) {
    HttpRequest captured;
    auto client = make_cryptocom_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::USER_BALANCE)};
    });

    auto resp = client.execute(CryptoComUserBalanceRequest{}, creds());

    EXPECT_EQ(captured.method, HttpRequest::Method::POST);
    EXPECT_EQ(captured.path, "/exchange/v1/private/user-balance");
    auto body = json::parse(captured.body);
    EXPECT_EQ(body.at("api_key"), "my-key");
    EXPECT_EQ(body.at("method"), "private/user-balance");
    expect_sig_matches(body, "private/user-balance");
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_EQ(resp.result->data.size(), 1u);
    EXPECT_EQ(resp.result->data[0].instrument_name, "USD");
}

TEST(CryptoComClient, PrivateCreateOrder_SignsAndParses) {
    HttpRequest captured;
    auto client = make_cryptocom_test_client([&](const HttpRequest& h) {
        captured = h;
        return HttpResponse{200, std::string(fx::CREATE_ORDER)};
    });

    CryptoComCreateOrderRequest req;
    req.instrument_name = "BTC_USD";
    req.side            = Side::Buy;
    req.type            = OrderType::Limit;
    req.price           = "63000.00";
    req.quantity        = "0.001";
    req.time_in_force   = TimeInForce::GTC;

    auto resp = client.execute(req, creds());

    auto body = json::parse(captured.body);
    EXPECT_EQ(body.at("method"), "private/create-order");
    EXPECT_EQ(body.at("params").at("instrument_name"), "BTC_USD");
    expect_sig_matches(body, "private/create-order");
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->order_id, "18342311");
}

// ── Error handling ─────────────────────────────────────────────────────────────

TEST(CryptoComClient, AppErrorCode_MapsToEnvelopeError) {
    auto client = make_cryptocom_test_client([&](const HttpRequest&) {
        return HttpResponse{200, std::string(
            R"({"id":1,"method":"private/create-order","code":10004,"message":"Bad request"})")};
    });

    CryptoComCreateOrderRequest req;
    req.instrument_name = "BTC_USD";
    req.price           = "1";
    req.quantity        = "1";
    auto resp = client.execute(req, creds());

    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.result.has_value());
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_NE(resp.errors[0].find("10004"), std::string::npos);
}

TEST(CryptoComClient, TransportException_FoldsIntoEnvelope) {
    auto client = make_cryptocom_test_client([&](const HttpRequest&) -> HttpResponse {
        throw std::runtime_error("connection refused");
    });

    auto resp = client.execute(CryptoComInstrumentsRequest{});

    EXPECT_FALSE(resp.ok);
    ASSERT_FALSE(resp.errors.empty());
    EXPECT_NE(resp.errors[0].find("connection refused"), std::string::npos);
}

TEST(CryptoComClient, AssignsUniqueCorrelationIds) {
    std::vector<int64_t> ids;
    auto client = make_cryptocom_test_client([&](const HttpRequest& h) {
        ids.push_back(json::parse(h.body).at("id").get<int64_t>());
        return HttpResponse{200, std::string(fx::USER_BALANCE)};
    });

    client.execute(CryptoComUserBalanceRequest{}, creds());
    client.execute(CryptoComUserBalanceRequest{}, creds());

    ASSERT_EQ(ids.size(), 2u);
    EXPECT_NE(ids[0], ids[1]);
}

// The production constructor hands its HttpTimeouts to the transport. The
// server accepts the connection and never answers; one limit is an hour, so
// only the total limit can end the call. A client that dropped its timeouts
// would wait the default 30 s and name that limit instead (plan 042, #56).
TEST(CryptoComClient, ProductionClientHandsItsTimeoutsToTheTransport) {
    const CurlGlobal curl;
    ASSERT_TRUE(curl.ok());
    const SilentListener silent;
    ASSERT_TRUE(silent.listening());
    const HttpTimeouts timeouts{.connect = std::chrono::hours(1), .total = std::chrono::milliseconds(1)};
    CryptoComRestClient client(silent.url("http"), timeouts);

    const auto resp = client.execute(CryptoComInstrumentsRequest{});
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_NE(resp.errors[0].find("request failed: curl_easy_perform failed: Timeout was reached"), std::string::npos)
        << resp.errors[0];
    EXPECT_NE(resp.errors[0].find("total limit 1 ms"), std::string::npos) << resp.errors[0];
}
