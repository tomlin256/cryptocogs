// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// CurlHttpClient::perform against a loopback ix::HttpServer (plan 038, #48): what a request carries over a real libcurl transfer.
//
// perform builds the header list from HttpRequest::headers and hands it to libcurl, and nothing else in the suite runs the transport
// (the adapter tests inject a performer). Real sockets on 127.0.0.1, no network. No sleeps: perform() returns once the server has
// replied, and the request the server saw is read after that. A wait that never ends is stopped by the test preset's 120 s timeout.
//
// One request per client, on purpose. A second request on the same client is #51: the handle keeps the previous call's header list
// and body, so a request with no headers after one with headers crashes in libcurl. The tests for #51 belong in this file.

#include "exchange/common/http_client.hpp"

#include <gtest/gtest.h>
#include <ixwebsocket/IXHttpServer.h>

#include <unistd.h>

#include <memory>
#include <mutex>
#include <string>

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpRequest;
using exchange::rest::HttpResponse;

namespace {

// What the server saw of one request.
struct SeenRequest {
    std::string              method;
    std::string              uri;
    std::string              body;
    ix::WebSocketHttpHeaders headers;
};

// An HTTP server on 127.0.0.1 that answers every request 200 "ok" and keeps the last request it saw. ixwebsocket cannot report the
// port it bound when asked for port 0, so this scans upward from a port derived from the process id until one binds, the way
// test_ix_ws_connection finds one: two test processes under ctest -j then settle on different ports without any randomness.
class LoopbackHttpServer {
public:
    LoopbackHttpServer() {
        const int first_port = 49152 + static_cast<int>(::getpid() % 16000);
        for (int attempt = 0; attempt < 200 && !server_; ++attempt) {
            const int port      = first_port + attempt;
            auto      candidate = std::make_unique<ix::HttpServer>(port, "127.0.0.1");
            candidate->setOnConnectionCallback(
                [this](const ix::HttpRequestPtr& request, const std::shared_ptr<ix::ConnectionState>&) {
                    remember(*request);
                    return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(),
                                                              "ok");
                });
            if (candidate->listenAndStart()) {
                port_   = port;
                server_ = std::move(candidate);
            }
        }
    }

    // Stops the server, which joins its threads, before any member is destroyed: the callback above uses mutex_ and last_.
    ~LoopbackHttpServer() {
        if (server_)
            server_->stop();
    }

    LoopbackHttpServer(const LoopbackHttpServer&)            = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    bool        running() const { return server_ != nullptr; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    // The request the server saw last. Read it after perform() has returned: the server stored it before it sent its reply, and the
    // mutex orders the two.
    SeenRequest last() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return last_;
    }

private:
    void remember(const ix::HttpRequest& request) {
        const std::lock_guard<std::mutex> lock(mutex_);
        last_ = SeenRequest{request.method, request.uri, request.body, request.headers};
    }

    std::mutex                      mutex_;
    SeenRequest                     last_;
    std::unique_ptr<ix::HttpServer> server_{};
    int                             port_{0};
};

// The value of a header as the server saw it, or "<missing>".
std::string header_of(const SeenRequest& seen, const std::string& name) {
    const auto found = seen.headers.find(name);
    return found == seen.headers.end() ? std::string("<missing>") : found->second;
}

// The library's own rule (CLAUDE.md): libcurl is initialised once, before the first client, and cleaned up after the last.
class CurlHttpClientPerform : public ::testing::Test {
protected:
    static void SetUpTestSuite() { EXPECT_EQ(curl_global_init(CURL_GLOBAL_ALL), CURLE_OK); }
    static void TearDownTestSuite() { curl_global_cleanup(); }
};

} // namespace

TEST_F(CurlHttpClientPerform, SendsEveryHeaderOfAGet) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    HttpRequest request;
    request.method  = HttpRequest::Method::GET;
    request.path    = "/one";
    request.query   = "a=1&b=2";
    request.headers = {{"X-First", "1"}, {"X-Second", "two words"}, {"X-Third", "3"}};

    const HttpResponse response = client.perform(request);
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");

    const SeenRequest seen = server.last();
    EXPECT_EQ(seen.method, "GET");
    EXPECT_EQ(seen.uri, "/one?a=1&b=2");
    EXPECT_EQ(header_of(seen, "X-First"), "1");
    EXPECT_EQ(header_of(seen, "X-Second"), "two words");
    EXPECT_EQ(header_of(seen, "X-Third"), "3");
}

TEST_F(CurlHttpClientPerform, SendsTheBodyAndTheHeadersOfAPost) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    HttpRequest request;
    request.method  = HttpRequest::Method::POST;
    request.path    = "/order";
    request.body    = "symbol=X&secret=1";
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"X-Key", "k"}};

    const HttpResponse response = client.perform(request);
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");

    const SeenRequest seen = server.last();
    EXPECT_EQ(seen.method, "POST");
    EXPECT_EQ(seen.uri, "/order");
    EXPECT_EQ(seen.body, "symbol=X&secret=1");
    EXPECT_EQ(header_of(seen, "Content-Type"), "application/x-www-form-urlencoded");
    EXPECT_EQ(header_of(seen, "X-Key"), "k");
}

// No header list at all: the request is made, and the answer comes back.
TEST_F(CurlHttpClientPerform, PerformsARequestWithNoHeaders) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    HttpRequest request;
    request.method = HttpRequest::Method::GET;
    request.path   = "/bare";

    const HttpResponse response = client.perform(request);
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");

    const SeenRequest seen = server.last();
    EXPECT_EQ(seen.method, "GET");
    EXPECT_EQ(seen.uri, "/bare");
}
