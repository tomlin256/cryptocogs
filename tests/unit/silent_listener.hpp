// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// SilentListener — a server that has stopped answering, for the tests of the HTTP timeouts (plan 042, #56).
//
// A TCP listener on 127.0.0.1 that never calls accept(). The kernel completes the handshake of a client that connects and keeps the
// bytes it sends, and nothing ever answers: to the client this is a server that accepted the connection, read the request and went
// quiet. There is no thread, no flag and no release; the destructor drops whatever is queued. The kernel picks the port.
//
// The same listener serves `https://`: the client's TLS ClientHello is queued and never answered, so the handshake never completes
// and the request is never sent.
//
// Its queue is finite (backlog 16). macOS refuses the 17th connection to a listener that never accepts, with an error that is not a
// timeout; Linux does not. A test makes a few connections to one listener.
//
//   const CurlGlobal curl;                 // libcurl's global state: first, so that it is cleaned up last
//   ASSERT_TRUE(curl.ok());
//   const SilentListener silent;
//   ASSERT_TRUE(silent.listening());
//   CurlHttpClient client(silent.url("http"), HttpTimeouts{.connect = 1h, .total = 1ms});

#pragma once

#include <curl/curl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <string_view>

class SilentListener {
public:
    SilentListener() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return;
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;  // the kernel picks a free port
        socklen_t length        = sizeof address;
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 || ::listen(fd, kBacklog) != 0 ||
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            ::close(fd);
            return;
        }
        fd_   = fd;
        port_ = ntohs(address.sin_port);
    }

    ~SilentListener() {
        if (fd_ >= 0)
            ::close(fd_);
    }

    SilentListener(const SilentListener&)            = delete;
    SilentListener& operator=(const SilentListener&) = delete;

    // False if the socket could not be made, bound or put to listen: the test stops there.
    bool listening() const { return fd_ >= 0; }

    // `scheme` is "http" or "https": the same listener answers neither.
    std::string url(const std::string_view scheme) const {
        std::string text(scheme);
        text.append("://127.0.0.1:").append(std::to_string(port_));
        return text;
    }

private:
    static constexpr int kBacklog = 16;

    int fd_{-1};
    int port_{0};
};

// libcurl's global state for the length of one test, for a test that is not in a fixture that does it once per suite. The library's own
// rule (CLAUDE.md) is that libcurl is initialised before the first client and cleaned up after the last, so declare this first.
class CurlGlobal {
public:
    CurlGlobal() : status_(curl_global_init(CURL_GLOBAL_ALL)) {}

    ~CurlGlobal() {
        if (status_ == CURLE_OK)
            curl_global_cleanup();
    }

    CurlGlobal(const CurlGlobal&)            = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;

    bool ok() const { return status_ == CURLE_OK; }

private:
    CURLcode status_;
};
