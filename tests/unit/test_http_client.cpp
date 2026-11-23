// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

// CurlHttpClient::perform against a loopback ix::HttpServer (plan curl-header-list-append-failure, #48; plan curl-handle-reset,
// #51): what a request carries over a real libcurl transfer, and that nothing of an earlier request on the same client comes with
// it.
//
// perform builds the header list from HttpRequest::headers and hands it to libcurl, and nothing else in the suite runs the transport
// (the adapter tests inject a performer). Real sockets on 127.0.0.1, no network. No sleeps: perform() returns once the server has
// replied, and the request the server saw is read after that. A wait that never ends is stopped by the test preset's 120 s timeout.
//
// A client reuses one libcurl handle for every call, and libcurl's options stay set until they are changed or the handle is reset
// (#51). The tests that send several requests through one client check that each reaches the server with what it asked for and with
// nothing of the one before it, a call that threw included. The server's reply says "Connection: close", so libcurl never tries a
// connection that the server is closing: there is no timing in them.
//
// Timeouts (plan curl-timeouts, #56). A SilentListener, a TCP listener that never accepts, stands for a server that has stopped
// answering. The two live tests give one limit a value that cannot end the call within the test's life (an hour) and the other
// 1 ms, so only the option under test can end it: an option that is missing, misplaced or set on the first call only leaves the
// call blocked until the preset's 120 s timeout. At 1 ms the outcome is certain, because libcurl's own timer ends the call (1,200
// of 1,200 runs on macOS and on Linux), but the phase is not: whether a plain-HTTP request had begun varied from 99 of 300 runs
// on macOS to 300 of 300 on Linux. So no live test asserts it. The text for each phase is tested as a pure function, and the live
// phase test is the TLS handshake that is never answered, where nothing is ever sent.
//
// Connections. libcurl keeps connections between calls, and when a reused one ends before a byte of reply it writes the request
// again on a new connection, so a POST that places an order could reach the server twice and perform() could not tell. perform()
// leaves that to libcurl for a GET only. The ix::HttpServer above cannot show it: it closes every connection after its reply. A
// KeepAliveServer (keepalive_server.hpp) keeps them open, records every request with the connection it came on, and closes a
// connection with no reply when its rule says so. The tests count what the server read, after perform() has returned: no timer.
// Every method shares one connection. A POST or a DELETE is read once, whichever connection it is on, and perform() says that it
// was not sent again. A GET is read again and answered.
//
// Holding a request to one write. perform() sets CURLOPT_PREREQFUNCTION and CURLOPT_PREREQDATA on every POST and DELETE through
// detail::hold_to_one_write, which reads libcurl's answer to each. A libcurl that has an option does not refuse it, so no call to
// perform() can show what a refusal does. The tests of hold_to_one_write hand it functions of the type of curl_easy_setopt that stand
// for a libcurl that is older than the headers: one refuses an option, one takes both and counts what it was offered. The connection
// tests described above are the ones that run the callback and the counter over a real libcurl.

#include "exchange/common/http_client.hpp"

#include "keepalive_server.hpp"
#include "silent_listener.hpp"

#include <gtest/gtest.h>
#include <ixwebsocket/IXHttpServer.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using exchange::rest::CurlHttpClient;
using exchange::rest::HttpRequest;
using exchange::rest::HttpResponse;
using exchange::rest::HttpTimeouts;

using namespace std::chrono_literals;

namespace {

// What the server saw of one request.
struct SeenRequest {
    std::string              method;
    std::string              uri;
    std::string              body;
    ix::WebSocketHttpHeaders headers;
};

// An HTTP server on 127.0.0.1 that answers every request 200 "ok" and keeps the requests it saw, in order. ixwebsocket cannot report the
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
                    // ix::HttpServer closes the connection after every reply and does not say so. Saying it keeps libcurl from trying a
                    // connection that is going away, whose outcome (a dead-connection check, or a retry) would depend on timing.
                    return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok,
                                                              ix::WebSocketHttpHeaders{{"Connection", "close"}}, "ok");
                });
            if (candidate->listenAndStart()) {
                port_   = port;
                server_ = std::move(candidate);
            }
        }
    }

    // Stops the server, which joins its threads, before any member is destroyed: the callback above uses mutex_ and seen_.
    ~LoopbackHttpServer() {
        if (server_)
            server_->stop();
    }

    LoopbackHttpServer(const LoopbackHttpServer&)            = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    bool        running() const { return server_ != nullptr; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    // The request the server saw last, or an empty one if it saw none. Read it after perform() has returned: the server stored it before
    // it sent its reply, and the mutex orders the two.
    SeenRequest last() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return seen_.empty() ? SeenRequest{} : seen_.back();
    }

    // Every request the server saw, in the order it saw them; read after perform() has returned, as for last().
    std::vector<SeenRequest> requests() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return seen_;
    }

private:
    void remember(const ix::HttpRequest& request) {
        const std::lock_guard<std::mutex> lock(mutex_);
        seen_.push_back(SeenRequest{request.method, request.uri, request.body, request.headers});
    }

    std::mutex                      mutex_;
    std::vector<SeenRequest>        seen_;
    std::unique_ptr<ix::HttpServer> server_{};
    int                             port_{0};
};

// The value of a header as the server saw it, or "<missing>".
std::string header_of(const SeenRequest& seen, const std::string& name) {
    const auto found = seen.headers.find(name);
    return found == seen.headers.end() ? std::string("<missing>") : found->second;
}

HttpRequest make_request(const HttpRequest::Method method, std::string path, std::string body,
                         std::map<std::string, std::string> headers) {
    HttpRequest request;
    request.method  = method;
    request.path    = std::move(path);
    request.body    = std::move(body);
    request.headers = std::move(headers);
    return request;
}

std::string method_name(const HttpRequest::Method method) {
    switch (method) {
        case HttpRequest::Method::GET:    return "GET";
        case HttpRequest::Method::POST:   return "POST";
        case HttpRequest::Method::DELETE: return "DELETE";
    }
    return "<unknown>";
}

// A path that libcurl refuses: a control character makes the URL malformed, so perform() throws before anything is connected or sent.
// Built in a function: a namespace-scope std::string would allocate before main (bugprone-throwing-static-initialization).
std::string refused_path() {
    std::string path = "/refused";
    path += '\x01';
    path += "path";
    return path;
}

// A DELETE that asked for no body and carries the header X-Second must reach the server as exactly that: nothing of the POST that came
// before it, which had a body and the header X-First (the bytes of the body are what #51 re-sent).
void expect_a_plain_delete(const SeenRequest& seen) {
    EXPECT_EQ(seen.method, "DELETE");
    EXPECT_EQ(seen.uri, "/order/1");
    EXPECT_EQ(seen.body, "");
    EXPECT_EQ(header_of(seen, "X-Second"), "2");
    EXPECT_EQ(header_of(seen, "X-First"), "<missing>");
    EXPECT_EQ(header_of(seen, "Content-Length"), "<missing>");
    EXPECT_EQ(header_of(seen, "Content-Type"), "<missing>");
}

// One kind of request for the table test. A kind that `fails` has a path that libcurl refuses, so perform() throws and nothing is sent.
struct Kind {
    std::string name;
    HttpRequest request;
    bool        fails{false};
};

// The kinds of request the table test sends. Each that has headers has one of its own, named for the kind, so a header that turns up on
// another request can be traced to where it came from.
std::vector<Kind> request_kinds() {
    using M = HttpRequest::Method;
    return {
        Kind{"GET with a header", make_request(M::GET, "/get-with-header", "", {{"X-Get", "g"}}), false},
        Kind{"bare GET", make_request(M::GET, "/get", "", {}), false},
        Kind{"POST with a body", make_request(M::POST, "/post", "a=1&b=2", {{"X-Post", "p"}}), false},
        Kind{"DELETE with a body", make_request(M::DELETE, "/delete-with-body", "c=3", {{"X-Delete", "d"}}), false},
        Kind{"bare DELETE", make_request(M::DELETE, "/delete", "", {}), false},
        Kind{"refused POST with a body", make_request(M::POST, refused_path(), "e=5&secret=1", {{"X-Refused-Post", "rp"}}), true},
        Kind{"refused GET with a header", make_request(M::GET, refused_path(), "", {{"X-Refused-Get", "rg"}}), true},
    };
}

// What the server must have seen for `asked`, whatever came before it on the client: its method, target and body; each of its headers
// with its value; none of the headers that another kind carries; and a Content-Length and Content-Type only if there is a body to
// describe (and then a Content-Length that is the body's size).
void expect_only_its_own(const SeenRequest& seen, const HttpRequest& asked, const std::vector<Kind>& kinds) {
    EXPECT_EQ(seen.method, method_name(asked.method));
    EXPECT_EQ(seen.uri, asked.path);
    EXPECT_EQ(seen.body, asked.body);
    for (const auto& [name, value] : asked.headers)
        EXPECT_EQ(header_of(seen, name), value) << name;
    for (const Kind& other : kinds) {
        for (const auto& entry : other.request.headers) {
            if (asked.headers.find(entry.first) == asked.headers.end()) {
                EXPECT_EQ(header_of(seen, entry.first), "<missing>") << entry.first << " belongs to another request";
            }
        }
    }
    if (asked.body.empty()) {
        EXPECT_EQ(header_of(seen, "Content-Length"), "<missing>");
        EXPECT_EQ(header_of(seen, "Content-Type"), "<missing>");
    } else {
        EXPECT_EQ(header_of(seen, "Content-Length"), std::to_string(asked.body.size()));
    }
}

// The text of the exception that `call` throws, which must be an E: "<nothing thrown>" if it throws nothing, and "<another exception>"
// and a failure if it throws something else.
template <typename E, typename Call>
std::string thrown_message(const Call& call) {
    try {
        call();
    } catch (const E& e) {
        return e.what();
    } catch (...) {
        ADD_FAILURE() << "an exception of another type than the one expected";
        return "<another exception>";
    }
    return "<nothing thrown>";
}

bool has(const std::string& text, const std::string_view needle) {
    return text.find(needle) != std::string::npos;
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

// #51, the first sequence: a request with no headers after one with headers. The client handed libcurl the first request's header list
// again, freed, and libcurl read it.
TEST_F(CurlHttpClientPerform, AGetWithNoHeadersAfterOneWithHeadersSendsNone) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    const HttpRequest first  = make_request(HttpRequest::Method::GET, "/first", "", {{"X-Secret", "abc123"}});
    const HttpRequest second = make_request(HttpRequest::Method::GET, "/second", "", {});

    EXPECT_EQ(client.perform(first).status, 200);
    const HttpResponse response = client.perform(second);
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");

    const std::vector<SeenRequest> seen = server.requests();
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_EQ(header_of(seen[0], "X-Secret"), "abc123");
    EXPECT_EQ(seen[1].method, "GET");
    EXPECT_EQ(seen[1].uri, "/second");
    EXPECT_EQ(header_of(seen[1], "X-Secret"), "<missing>");
}

// #51, the second sequence: a DELETE with no body after a POST. The client left the POST's body, its length and its method on the handle,
// so the DELETE went out with the POST's Content-Length and Content-Type and the bytes behind them. The DELETE has a header of its own, so
// the header list is not what differs. Both requests outlive the calls, so the stale body is still readable and the failure is an
// assertion, not undefined behaviour.
TEST_F(CurlHttpClientPerform, ADeleteWithNoBodyAfterAPostSendsNoBody) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    const HttpRequest post = make_request(HttpRequest::Method::POST, "/order", "symbol=X&secret=1", {{"X-First", "1"}});
    const HttpRequest del  = make_request(HttpRequest::Method::DELETE, "/order/1", "", {{"X-Second", "2"}});

    EXPECT_EQ(client.perform(post).status, 200);
    EXPECT_EQ(client.perform(del).status, 200);

    const std::vector<SeenRequest> seen = server.requests();
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_EQ(seen[0].body, "symbol=X&secret=1");
    expect_a_plain_delete(seen[1]);
}

// A call that throws leaves the same state on the handle as one that returns. The first request has a path libcurl refuses, so perform()
// throws and nothing is sent; the DELETE after it has a header of its own, so what could differ is the refused POST's body. (The header
// list of a refused call is in the table test.)
TEST_F(CurlHttpClientPerform, ARequestAfterAFailedOneSendsOnlyItsOwn) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    CurlHttpClient client(server.base_url());

    const HttpRequest refused = make_request(HttpRequest::Method::POST, refused_path(), "symbol=X&secret=1", {{"X-First", "1"}});
    const HttpRequest del     = make_request(HttpRequest::Method::DELETE, "/order/1", "", {{"X-Second", "2"}});

    EXPECT_THROW(client.perform(refused), std::runtime_error);
    EXPECT_TRUE(server.requests().empty()) << "the refused request reached the server";
    EXPECT_EQ(client.perform(del).status, 200);

    const std::vector<SeenRequest> seen = server.requests();
    ASSERT_EQ(seen.size(), 1U);
    expect_a_plain_delete(seen[0]);
}

// Whatever a client did before, a request carries only what it asked for: every kind of request, then each kind that succeeds, on a
// fresh client for each pair. Before #51 was fixed the pairs that start with a request that has headers or a body went wrong in one of
// the ways above (a crash in libcurl, or the earlier request's body on the wire).
TEST_F(CurlHttpClientPerform, ARequestSendsOnlyItsOwnWhateverCameBefore) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.running());
    const std::vector<Kind> kinds = request_kinds();

    for (const Kind& first : kinds) {
        for (const Kind& second : kinds) {
            if (second.fails)
                continue;
            std::string pair = first.name;
            pair += ", then ";
            pair += second.name;
            SCOPED_TRACE(pair);

            CurlHttpClient    client(server.base_url());
            const std::size_t before = server.requests().size();
            if (first.fails) {
                EXPECT_THROW(client.perform(first.request), std::runtime_error);
            } else {
                EXPECT_EQ(client.perform(first.request).status, 200);
            }
            const HttpResponse response = client.perform(second.request);
            EXPECT_EQ(response.status, 200);
            EXPECT_EQ(response.body, "ok");

            const std::vector<SeenRequest> seen = server.requests();
            ASSERT_EQ(seen.size(), before + (first.fails ? 1U : 2U));
            expect_only_its_own(seen.back(), second.request, kinds);
        }
    }
}

// #56, the issue's case: a server that accepted the connection and never answers. Only the total limit can end the call (the connect limit
// is an hour), and three calls on one client, because the options are set on each call: the reset at the top of perform clears them, so
// set once, in the constructor or on the first call, they would be gone. Each call must throw a runtime_error that names the limits.
TEST_F(CurlHttpClientPerform, EveryCallToAServerThatNeverRepliesTimesOut) {
    const SilentListener silent;
    ASSERT_TRUE(silent.listening());
    CurlHttpClient client(silent.url("http"), HttpTimeouts{.connect = 1h, .total = 1ms});
    const HttpRequest request = make_request(HttpRequest::Method::GET, "/quiet", "", {});

    for (int call = 1; call <= 3; ++call) {
        SCOPED_TRACE(call);
        const std::string text = thrown_message<std::runtime_error>([&] { client.perform(request); });
        EXPECT_TRUE(has(text, "Timeout was reached")) << text;
        EXPECT_TRUE(has(text, "connect limit 3600000 ms")) << text;
        EXPECT_TRUE(has(text, "total limit 1 ms")) << text;
    }
}

// A TLS handshake that is never answered ends in the connect limit (the total is an hour), and nothing is ever sent, on every run and on
// both systems that were measured. So this is the one live test that can say which text a timeout gets.
TEST_F(CurlHttpClientPerform, ATlsHandshakeThatNeverCompletesTimesOutBeforeAnythingIsSent) {
    const SilentListener silent;
    ASSERT_TRUE(silent.listening());
    CurlHttpClient client(silent.url("https"), HttpTimeouts{.connect = 1ms, .total = 1h});
    const HttpRequest request = make_request(HttpRequest::Method::POST, "/order", "symbol=X&secret=1", {{"X-Key", "k"}});

    const std::string text = thrown_message<std::runtime_error>([&] { client.perform(request); });
    EXPECT_TRUE(has(text, "Timeout was reached")) << text;
    EXPECT_TRUE(has(text, "connect limit 1 ms")) << text;
    EXPECT_TRUE(has(text, "total limit 3600000 ms")) << text;
    EXPECT_TRUE(has(text, "The request was not sent.")) << text;
    EXPECT_FALSE(has(text, "outcome is unknown")) << text;
}

// A limit that is not positive would reach libcurl as 0, which it reads as "no limit" (total) or 300 s (connect): the hang this removes.
// The constructor refuses it before it asks libcurl for anything, and names the limit.
TEST(CurlHttpClientTimeouts, ANonPositiveConnectTimeoutIsRefused) {
    for (const std::chrono::milliseconds bad : {0ms, -1ms}) {
        SCOPED_TRACE(bad.count());
        const std::string text = thrown_message<std::invalid_argument>(
            [&] { const CurlHttpClient client("http://127.0.0.1:1", HttpTimeouts{.connect = bad, .total = 1s}); });
        EXPECT_TRUE(has(text, "connect timeout must be positive")) << text;
        EXPECT_FALSE(has(text, "total")) << text;
    }
}

TEST(CurlHttpClientTimeouts, ANonPositiveTotalTimeoutIsRefused) {
    for (const std::chrono::milliseconds bad : {0ms, -1ms}) {
        SCOPED_TRACE(bad.count());
        const std::string text = thrown_message<std::invalid_argument>(
            [&] { const CurlHttpClient client("http://127.0.0.1:1", HttpTimeouts{.connect = 1s, .total = bad}); });
        EXPECT_TRUE(has(text, "total timeout must be positive")) << text;
        EXPECT_FALSE(has(text, "connect")) << text;
    }
}

// The documented defaults: neither limit fires on a healthy exchange, and both bound the worst case.
TEST(CurlHttpClientTimeouts, TheDefaultsAreTenSecondsToConnectAndThirtyInTotal) {
    const HttpTimeouts defaults;
    EXPECT_EQ(defaults.connect.count(), 10'000);
    EXPECT_EQ(defaults.total.count(), 30'000);
}

// The two texts, without a timer. A timeout of a plain-HTTP request at a few milliseconds is a race between libcurl's connect and its
// clock, so the text for "the request may have gone out" cannot be got from a live call that always means it; here it is asked for.
TEST(CurlHttpClientTimeouts, ATimeoutAfterTheRequestMayHaveGoneOutSaysItsOutcomeIsUnknown) {
    const std::string text = exchange::rest::detail::timeout_message(true, HttpTimeouts{.connect = 2s, .total = 5s});
    EXPECT_TRUE(has(text, "Timeout was reached")) << text;
    EXPECT_TRUE(has(text, "connect limit 2000 ms")) << text;
    EXPECT_TRUE(has(text, "total limit 5000 ms")) << text;
    EXPECT_TRUE(has(text, "outcome is unknown")) << text;
    EXPECT_TRUE(has(text, "check the exchange before sending it again")) << text;
    EXPECT_FALSE(has(text, "was not sent")) << text;
}

TEST(CurlHttpClientTimeouts, ATimeoutBeforeTheRequestWentOutSaysItWasNotSent) {
    const std::string text = exchange::rest::detail::timeout_message(false, HttpTimeouts{.connect = 2s, .total = 5s});
    EXPECT_TRUE(has(text, "Timeout was reached")) << text;
    EXPECT_TRUE(has(text, "connect limit 2000 ms")) << text;
    EXPECT_TRUE(has(text, "total limit 5000 ms")) << text;
    EXPECT_TRUE(has(text, "The request was not sent.")) << text;
    EXPECT_FALSE(has(text, "outcome is unknown")) << text;
    EXPECT_FALSE(has(text, "sending it again")) << text;
}

// An option that perform cannot do without, refused by libcurl. No libcurl that has an option refuses it, so the refusal is handed in:
// what a libcurl that is older than the headers returns for an option it does not know. The error names the option and both libcurls,
// the one in use and the one this was built with, and says that nothing was sent.
TEST(CurlHttpClientOptions, ARefusedOptionIsAnErrorThatNamesItAndBothLibcurls) {
    const std::string text = thrown_message<std::runtime_error>(
        [] { exchange::rest::detail::require_option(CURLE_UNKNOWN_OPTION, "CURLOPT_PREREQFUNCTION"); });
    EXPECT_TRUE(has(text, "libcurl refused CURLOPT_PREREQFUNCTION")) << text;
    EXPECT_TRUE(has(text, curl_easy_strerror(CURLE_UNKNOWN_OPTION))) << text;
    EXPECT_TRUE(has(text, std::string("The libcurl in use is ").append(curl_version_info(CURLVERSION_NOW)->version))) << text;
    EXPECT_TRUE(has(text, std::string("built with ").append(LIBCURL_VERSION))) << text;
    EXPECT_TRUE(has(text, "the request was not sent")) << text;
}

TEST(CurlHttpClientOptions, AnOptionThatLibcurlTookIsNoError) {
    EXPECT_NO_THROW(exchange::rest::detail::require_option(CURLE_OK, "CURLOPT_PREREQFUNCTION"));
}

// ---- Holding a request to one write ------------------------------------------------------------------------------------------

namespace {

using CurlHandle = std::unique_ptr<CURL, void (*)(CURL*)>;

// What a libcurl that is older than the headers does with the two options that hold a request to one write: it answers
// CURLE_UNKNOWN_OPTION for the one it does not have and takes the rest. Functions of the type of curl_easy_setopt, so that
// hold_to_one_write can be handed one.
CURLcode refuses_prereq_function(CURL* /*curl*/, const CURLoption option, ...) {
    return option == CURLOPT_PREREQFUNCTION ? CURLE_UNKNOWN_OPTION : CURLE_OK;
}

CURLcode refuses_prereq_data(CURL* /*curl*/, const CURLoption option, ...) {
    return option == CURLOPT_PREREQDATA ? CURLE_UNKNOWN_OPTION : CURLE_OK;
}

// What a libcurl that takes every option was offered: how often each option, and for which handle. It does not read the value of an
// option: clang refuses va_start on a parameter of the type CURLoption (an enumeration is not the type that its promotion gives).
// Plain members, so that the object at namespace scope allocates nothing (bugprone-throwing-static-initialization); a test resets it.
struct Offered {
    int   functions{0};
    int   data_offers{0};
    int   others{0};
    CURL* function_handle{nullptr};
    CURL* data_handle{nullptr};
};

Offered offered;

CURLcode takes_every_option(CURL* curl, const CURLoption option, ...) {
    if (option == CURLOPT_PREREQFUNCTION) {
        ++offered.functions;
        offered.function_handle = curl;
    } else if (option == CURLOPT_PREREQDATA) {
        ++offered.data_offers;
        offered.data_handle = curl;
    } else {
        ++offered.others;
    }
    return CURLE_OK;
}

} // namespace

TEST_F(CurlHttpClientPerform, HoldingARequestToOneWriteFailsWhenLibcurlRefusesThePrereqFunction) {
    const CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    ASSERT_NE(curl.get(), nullptr);
    exchange::rest::detail::SendOnce once;
    const std::string                text = thrown_message<std::runtime_error>(
        [&] { exchange::rest::detail::hold_to_one_write(curl.get(), once, &refuses_prereq_function); });
    EXPECT_TRUE(has(text, "libcurl refused CURLOPT_PREREQFUNCTION")) << text;
    EXPECT_TRUE(has(text, curl_easy_strerror(CURLE_UNKNOWN_OPTION))) << text;
    EXPECT_TRUE(has(text, "the request was not sent")) << text;
}

TEST_F(CurlHttpClientPerform, HoldingARequestToOneWriteFailsWhenLibcurlRefusesThePrereqData) {
    const CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    ASSERT_NE(curl.get(), nullptr);
    exchange::rest::detail::SendOnce once;
    const std::string                text = thrown_message<std::runtime_error>(
        [&] { exchange::rest::detail::hold_to_one_write(curl.get(), once, &refuses_prereq_data); });
    EXPECT_TRUE(has(text, "libcurl refused CURLOPT_PREREQDATA")) << text;
    EXPECT_TRUE(has(text, curl_easy_strerror(CURLE_UNKNOWN_OPTION))) << text;
    EXPECT_TRUE(has(text, "the request was not sent")) << text;
}

// The libcurl that takes both options: nothing is thrown, and each option is offered once, for the handle that was passed, and no
// other option is. That the callback and the counter are the right ones is held by the connection tests, over a real libcurl.
TEST_F(CurlHttpClientPerform, HoldingARequestToOneWriteOffersLibcurlBothOptions) {
    const CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    ASSERT_NE(curl.get(), nullptr);
    exchange::rest::detail::SendOnce once;
    offered = {};
    EXPECT_NO_THROW(exchange::rest::detail::hold_to_one_write(curl.get(), once, &takes_every_option));
    EXPECT_EQ(offered.functions, 1);
    EXPECT_EQ(offered.data_offers, 1);
    EXPECT_EQ(offered.others, 0);
    EXPECT_EQ(offered.function_handle, curl.get());
    EXPECT_EQ(offered.data_handle, curl.get());
}

// ---- Connections -------------------------------------------------------------------------------------------------------------

namespace {

// The rules of the KeepAliveServer. An order is any target that starts with /order.
bool never_drop(const ReadRequest& /*request*/) {
    return false;
}

// Every order is read and then closed with no reply, on whatever connection it comes.
bool drops_every_order(const ReadRequest& request) {
    return request.target.starts_with("/order");
}

// An order that comes on a connection which has carried a request before is read and then closed with no reply: a reused connection that
// the server closes after reading the request. On a connection made for the order it is answered.
bool drops_an_order_on_a_used_connection(const ReadRequest& request) {
    return request.target.starts_with("/order") && request.served_before > 0;
}

// How many of the requests that the server read have this target.
std::size_t times_read(const std::vector<ReadRequest>& reads, const std::string_view target) {
    return static_cast<std::size_t>(
        std::count_if(reads.begin(), reads.end(), [&](const ReadRequest& one) { return one.target == target; }));
}

// What perform() throws when the first attempt of a POST or a DELETE got no reply: that it did not send the request again, and that the
// request may have been acted on all the same.
void expect_it_says_the_request_was_not_sent_again(const std::string& text) {
    EXPECT_TRUE(has(text, "was not sent a second time")) << text;
    EXPECT_TRUE(has(text, "outcome is unknown")) << text;
}

// One row of the table. The order goes out on the connection that the earlier request used, the server reads it there and closes the
// connection with no reply, and perform() throws: the server has read the order once, and no second connection carried it. The client
// is not left broken: a GET after it is answered.
void expect_an_order_on_a_used_connection_is_sent_once(const std::string& name, const HttpRequest& earlier, const HttpRequest& order) {
    SCOPED_TRACE(name);
    KeepAliveServer server(drops_an_order_on_a_used_connection);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    EXPECT_EQ(client.perform(earlier).status, 200);
    expect_it_says_the_request_was_not_sent_again(thrown_message<std::runtime_error>([&] { client.perform(order); }));

    std::vector<ReadRequest> reads = server.requests();
    ASSERT_EQ(reads.size(), 2U) << "the order was read " << times_read(reads, order.path) << " times";
    EXPECT_EQ(reads[1].target, order.path);
    EXPECT_EQ(reads[1].connection, reads[0].connection);
    EXPECT_EQ(reads[1].served_before, 1);

    EXPECT_EQ(client.perform(make_request(HttpRequest::Method::GET, "/after", "", {})).status, 200);
    reads = server.requests();
    ASSERT_EQ(reads.size(), 3U);
    EXPECT_EQ(reads[2].target, "/after");
}

} // namespace

// The server reads the order and closes the connection with no reply, on every connection, so the order is read at least once whatever
// perform does. The order goes out on the connection of the request before it, and when that closes libcurl is about to write it again
// on a new one, where the server would read it a second time. perform throws, and its text says that the request was not sent again.
TEST_F(CurlHttpClientPerform, APostThatTheServerClosesWithoutAReplyIsSentOnce) {
    KeepAliveServer server(drops_every_order);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    const HttpRequest first = make_request(HttpRequest::Method::POST, "/first", "a=1", {});
    const HttpRequest order = make_request(HttpRequest::Method::POST, "/order", "symbol=X&qty=1", {});

    EXPECT_EQ(client.perform(first).status, 200);
    expect_it_says_the_request_was_not_sent_again(thrown_message<std::runtime_error>([&] { client.perform(order); }));
    EXPECT_EQ(times_read(server.requests(), "/order"), 1U);
}

// The same for a DELETE, a cancel: the reply to a second write would tell the caller what became of the second, not of the first, which
// had already gone through.
TEST_F(CurlHttpClientPerform, ADeleteThatTheServerClosesWithoutAReplyIsSentOnce) {
    KeepAliveServer server(drops_every_order);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    const HttpRequest first  = make_request(HttpRequest::Method::POST, "/first", "a=1", {});
    const HttpRequest cancel = make_request(HttpRequest::Method::DELETE, "/order/1", "", {});

    EXPECT_EQ(client.perform(first).status, 200);
    expect_it_says_the_request_was_not_sent_again(thrown_message<std::runtime_error>([&] { client.perform(cancel); }));
    EXPECT_EQ(times_read(server.requests(), "/order/1"), 1U);
}

// The same as a table, with a server that would answer a second write: it closes an order with no reply only on a connection that has
// carried a request before, so an order that libcurl wrote again on a new connection would be read twice and answered 200. The rows are
// both methods, each after a POST and after a GET.
TEST_F(CurlHttpClientPerform, APostOrADeleteOnAUsedConnectionThatClosesIsSentOnce) {
    using M = HttpRequest::Method;
    const HttpRequest post_first   = make_request(M::POST, "/first", "a=1", {});
    const HttpRequest get_first    = make_request(M::GET, "/first", "", {});
    const HttpRequest post_order   = make_request(M::POST, "/order", "symbol=X&qty=1", {});
    const HttpRequest delete_order = make_request(M::DELETE, "/order/1", "", {});

    expect_an_order_on_a_used_connection_is_sent_once("a POST after a POST", post_first, post_order);
    expect_an_order_on_a_used_connection_is_sent_once("a DELETE after a POST", post_first, delete_order);
    expect_an_order_on_a_used_connection_is_sent_once("a POST after a GET", get_first, post_order);
    expect_an_order_on_a_used_connection_is_sent_once("a DELETE after a GET", get_first, delete_order);
}

// Three GETs through one client make one connection: the reset at the top of perform keeps the open connections.
TEST_F(CurlHttpClientPerform, GetsKeepSharingOneConnection) {
    KeepAliveServer server(never_drop);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    for (const char* path : {"/a", "/b", "/c"}) {
        EXPECT_EQ(client.perform(make_request(HttpRequest::Method::GET, path, "", {})).status, 200) << path;
    }

    EXPECT_EQ(server.requests().size(), 3U);
    EXPECT_EQ(server.connections(), 1);
}

// Every method in one sequence: all five requests share one connection, each on the one that has carried every request before it. The
// POST and the DELETE are each written once and answered, so what perform counts is the writes of one call and not of the client.
TEST_F(CurlHttpClientPerform, AMixOfMethodsSharesOneConnection) {
    using M = HttpRequest::Method;
    KeepAliveServer server(never_drop);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    const std::vector<HttpRequest> sequence = {
        make_request(M::GET, "/a", "", {}),      make_request(M::POST, "/b", "x=1", {}), make_request(M::GET, "/c", "", {}),
        make_request(M::DELETE, "/d", "", {}),   make_request(M::GET, "/e", "", {}),
    };
    for (const HttpRequest& request : sequence) {
        EXPECT_EQ(client.perform(request).status, 200) << request.path;
    }

    const std::vector<ReadRequest> reads = server.requests();
    ASSERT_EQ(reads.size(), sequence.size());
    EXPECT_EQ(server.connections(), 1);
    for (std::size_t i = 0; i < reads.size(); ++i) {
        EXPECT_EQ(reads[i].target, sequence[i].path);
        EXPECT_EQ(reads[i].served_before, static_cast<int>(i)) << sequence[i].path << " was not on the connection of the ones before it";
    }
}

// A GET is safe to repeat, so perform leaves libcurl's recovery in place for it. The server closes the second GET with no reply on the
// connection that the first used; libcurl writes it again on a new connection, the server answers it there, and the caller gets the
// reply. The server read it twice.
TEST_F(CurlHttpClientPerform, AGetOnAUsedConnectionThatClosesIsSentAgain) {
    KeepAliveServer server(drops_an_order_on_a_used_connection);
    ASSERT_TRUE(server.listening());
    CurlHttpClient client(server.base_url());

    EXPECT_EQ(client.perform(make_request(HttpRequest::Method::GET, "/first", "", {})).status, 200);
    const HttpResponse response = client.perform(make_request(HttpRequest::Method::GET, "/order/1", "", {}));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");

    const std::vector<ReadRequest> reads = server.requests();
    ASSERT_EQ(reads.size(), 3U);
    EXPECT_EQ(times_read(reads, "/order/1"), 2U);
    EXPECT_EQ(reads[1].served_before, 1);
    EXPECT_EQ(reads[2].served_before, 0);
}
