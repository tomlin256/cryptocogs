// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "exchange/kraken/rest_client.hpp"
#include "assert_has_value.hpp"
#include "silent_listener.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>  // getpid

using namespace exchange::kraken::rest;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static KrakenCredentials dummy_creds() {
    return KrakenCredentials{"test-api-key", "dGVzdA=="};
}

// ---------------------------------------------------------------------------
// Public endpoint execute()
// ---------------------------------------------------------------------------

TEST(KrakenRestClient, PublicExecute_CallsBuildAndParsesResult) {
    auto client = make_test_client([](const HttpRequest& http) {
        EXPECT_EQ(http.path, "/0/public/Time");
        EXPECT_EQ(http.method, HttpRequest::Method::GET);
        return R"({"error":[],"result":{"unixtime":1700000000,"rfc1123":"Mon, 14 Nov 2023 00:00:00 +0000"}})";
    });

    auto resp = client.execute(GetServerTimeRequest{});
    ASSERT_TRUE(resp.ok);
    EXPECT_TRUE(resp.errors.empty());
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->unixtime, 1700000000);
    EXPECT_EQ(resp.result->rfc1123, "Mon, 14 Nov 2023 00:00:00 +0000");
}

TEST(KrakenRestClient, PublicExecute_PropagatesErrors) {
    auto client = make_test_client([](const HttpRequest&) {
        return R"({"error":["EGeneral:Invalid arguments"]})";
    });

    auto resp = client.execute(GetServerTimeRequest{});
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "EGeneral:Invalid arguments");
    EXPECT_FALSE(resp.result.has_value());
}

TEST(KrakenRestClient, PublicExecute_Ticker) {
    auto client = make_test_client([](const HttpRequest& http) {
        EXPECT_EQ(http.path, "/0/public/Ticker");
        EXPECT_EQ(http.method, HttpRequest::Method::GET);
        EXPECT_NE(http.query.find("pair=XBTUSD"), std::string::npos);
        return R"({
            "error":[],
            "result":{
                "XBTUSD":{
                    "a":["30000.00","1","1.000"],
                    "b":["29999.00","1","1.000"],
                    "c":["30000.50","0.001"],
                    "v":["100.0","200.0"],
                    "p":["30001.0","30002.0"],
                    "t":[500,1000],
                    "l":["29500.0","29000.0"],
                    "h":["30500.0","31000.0"],
                    "o":"29800.0"
                }
            }
        })";
    });

    GetTickerRequest req;
    req.pairs = {"XBTUSD"};
    auto resp = client.execute(req);
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    ASSERT_EQ(resp.result->tickers.count("XBTUSD"), 1u);
    EXPECT_DOUBLE_EQ(resp.result->tickers.at("XBTUSD").last, 30000.50);
}

// ---------------------------------------------------------------------------
// Private endpoint execute()
// ---------------------------------------------------------------------------

TEST(KrakenRestClient, PrivateExecute_SendsPostWithAuthHeaders) {
    auto client = make_test_client([](const HttpRequest& http) {
        EXPECT_EQ(http.path, "/0/private/Balance");
        EXPECT_EQ(http.method, HttpRequest::Method::POST);
        EXPECT_EQ(http.headers.count("API-Key"), 1u);
        EXPECT_EQ(http.headers.at("API-Key"), "test-api-key");
        EXPECT_EQ(http.headers.count("API-Sign"), 1u);
        EXPECT_NE(http.body.find("nonce="), std::string::npos);
        return R"({"error":[],"result":{"XXBT":"1.5","ZUSD":"10000.00"}})";
    });

    auto resp = client.execute(GetAccountBalanceRequest{}, dummy_creds());
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_DOUBLE_EQ(resp.result->balances.at("XXBT"), 1.5);
    EXPECT_DOUBLE_EQ(resp.result->balances.at("ZUSD"), 10000.0);
}

TEST(KrakenRestClient, PrivateExecute_AddOrder) {
    auto client = make_test_client([](const HttpRequest& http) {
        EXPECT_EQ(http.path, "/0/private/AddOrder");
        EXPECT_EQ(http.method, HttpRequest::Method::POST);
        EXPECT_NE(http.body.find("pair=XBTUSD"), std::string::npos);
        return R"({
            "error":[],
            "result":{
                "descr":{"order":"buy 0.001 XBTUSD @ limit 30000"},
                "txid":["OABC12-DEFG34-HIJKL5"]
            }
        })";
    });

    AddOrderRequest req;
    req.params.order_type  = exchange::OrderType::Limit;
    req.params.side        = exchange::Side::Buy;
    req.params.symbol      = "XBTUSD";
    req.params.order_qty   = 0.001;
    req.params.limit_price = exchange::kraken::TickPrice::from(30000.0, 4);

    auto resp = client.execute(req, dummy_creds());
    ASSERT_TRUE(resp.ok);
    ASSERT_HAS_VALUE(resp.result);
    EXPECT_EQ(resp.result->descr_order, "buy 0.001 XBTUSD @ limit 30000");
    ASSERT_EQ(resp.result->txids.size(), 1u);
    EXPECT_EQ(resp.result->txids[0], "OABC12-DEFG34-HIJKL5");
}

TEST(KrakenRestClient, PrivateExecute_PropagatesErrors) {
    auto client = make_test_client([](const HttpRequest&) {
        return R"({"error":["EOrder:Insufficient funds"]})";
    });

    auto resp = client.execute(AddOrderRequest{}, dummy_creds());
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_EQ(resp.errors[0], "EOrder:Insufficient funds");
}

// ---------------------------------------------------------------------------
// KrakenCredentials::from_file — TOML format (plan credentials-from-file)
// ---------------------------------------------------------------------------

TEST(CredentialsFromFile, LoadsTomlFile) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("cryptocogs_kraken_creds_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "default");
        f << "api_key    = \"PUBLICKEY\"\n"
             "api_secret = \"YmFzZTY0c2VjcmV0\"\n";
    }

    const auto creds = KrakenCredentials::from_file("default", dir.string());
    EXPECT_EQ(creds.api_key, "PUBLICKEY");
    EXPECT_EQ(creds.api_secret, "YmFzZTY0c2VjcmV0");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// KrakenCredentials::from_file error paths
// ---------------------------------------------------------------------------

TEST(CredentialsFromFile, ThrowsWhenFileNotFound) {
    EXPECT_THROW(
        KrakenCredentials::from_file("nonexistent", "/tmp/no_such_dir"),
        std::runtime_error
    );
}

TEST(CredentialsFromFile, ThrowsWhenHomeUnset) {
    const char* saved = getenv("HOME");
    unsetenv("HOME");
    EXPECT_THROW(KrakenCredentials::from_file("default"), std::runtime_error);
    if (saved) setenv("HOME", saved, 1);
}

// ---------------------------------------------------------------------------
// Review M2: transport / malformed-body failures fold into ok=false rather
// than escaping as exceptions, so resp.ok is the single failure check.
// ---------------------------------------------------------------------------

TEST(KrakenRestClient, TransportFailureFoldsIntoErrorResponse) {
    auto client = make_test_client(
        [](const HttpRequest&) -> std::string { throw std::runtime_error("network down"); });

    const auto resp = client.execute(GetServerTimeRequest{});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.errors.empty());
}

TEST(KrakenRestClient, MalformedBodyFoldsIntoErrorResponse) {
    auto client = make_test_client(
        [](const HttpRequest&) -> std::string { return "<html>not json</html>"; });

    const auto resp = client.execute(GetServerTimeRequest{});
    EXPECT_FALSE(resp.ok);
    EXPECT_FALSE(resp.errors.empty());
}

// ---------------------------------------------------------------------------
// Timeouts (plan curl-timeouts, #56): the one test here that uses the real transport.
// ---------------------------------------------------------------------------

// The production constructor hands its HttpTimeouts to the transport. The
// server accepts the connection and never answers; one limit is an hour, so
// only the total limit can end the call. A client that dropped its timeouts
// would wait the default 30 s and name that limit instead.
TEST(KrakenRestClient, ProductionClientHandsItsTimeoutsToTheTransport) {
    const CurlGlobal curl;
    ASSERT_TRUE(curl.ok());
    const SilentListener silent;
    ASSERT_TRUE(silent.listening());
    const HttpTimeouts timeouts{.connect = std::chrono::hours(1), .total = std::chrono::milliseconds(1)};
    KrakenRestClient client(silent.url("http"), timeouts);

    const auto resp = client.execute(GetServerTimeRequest{});
    EXPECT_FALSE(resp.ok);
    ASSERT_EQ(resp.errors.size(), 1u);
    EXPECT_NE(resp.errors[0].find("request failed: curl_easy_perform failed: Timeout was reached"), std::string::npos)
        << resp.errors[0];
    EXPECT_NE(resp.errors[0].find("total limit 1 ms"), std::string::npos) << resp.errors[0];
}
