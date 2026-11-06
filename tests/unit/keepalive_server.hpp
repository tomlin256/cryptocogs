// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================
//
// KeepAliveServer — an HTTP/1.1 server that keeps its connections open, for the tests of connection reuse (plan 043, #58).
//
// A TCP server on 127.0.0.1: raw sockets, one thread. It reads every request in full (the head, then Content-Length bytes), records it
// with the connection it came on, and then does what the test's DropRule says: answers 200 "ok" and keeps the connection open for the
// next request, or closes the connection with no reply at all, the way a server that crashed after reading the request, or a proxy that
// gave up on its upstream, looks to a client. The request is read in full before the close, so the client sees a clean close (FIN) and not
// a reset.
//
// ix::HttpServer, which the other tests of the transport use, closes every connection after its reply, so it cannot hold a connection
// for a second request to reuse. This server exists for that.
//
// There is no timer. The thread waits in poll() for ever, on the listener, the open connections and a pipe that the destructor writes
// to. A test reads requests() and connections() after perform() has returned: by then the server has recorded the request, because it
// records before it answers or closes, and both are behind one lock. The kernel picks the port.
//
//   KeepAliveServer server([](const ReadRequest& read) { return read.target == "/order"; });   // closes /order, answers the rest
//   ASSERT_TRUE(server.listening());
//   CurlHttpClient client(server.base_url());

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

// One request as the server read it.
struct ReadRequest {
    int         connection{0};     // 1 for the first connection the server accepted, 2 for the next, and so on
    int         served_before{0};  // requests that connection had carried before this one: 0 on a connection made for this request
    std::string method;
    std::string target;            // path and query, as in the request line
    std::string body;
};

namespace keepalive_detail {

// An open file descriptor, closed when it goes out of scope. Moved, never copied.
class Descriptor {
public:
    Descriptor() = default;
    explicit Descriptor(const int fd) : fd_(fd) {}
    ~Descriptor() { reset(); }

    Descriptor(const Descriptor&)            = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    Descriptor(Descriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int  get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    void reset() {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_{-1};
};

} // namespace keepalive_detail

class KeepAliveServer {
public:
    // What becomes of a request once the server has read it in full: true closes the connection with no reply, false answers 200 "ok" and
    // keeps the connection open. It runs on the server's thread, without the lock.
    using DropRule = std::function<bool(const ReadRequest&)>;

    explicit KeepAliveServer(DropRule drop) : drop_(std::move(drop)) {
        listener_ = Descriptor(::socket(AF_INET, SOCK_STREAM, 0));
        if (!listener_.valid())
            return;
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;  // the kernel picks a free port
        socklen_t length        = sizeof address;
        if (::bind(listener_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 ||
            ::listen(listener_.get(), kBacklog) != 0 ||
            ::getsockname(listener_.get(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            listener_.reset();
            return;
        }
        port_ = ntohs(address.sin_port);

        std::array<int, 2> wake{-1, -1};
        if (::pipe(wake.data()) != 0) {
            listener_.reset();
            return;
        }
        wake_read_  = Descriptor(wake[0]);
        wake_write_ = Descriptor(wake[1]);

        thread_ = std::thread([this] { run(); });
    }

    // Wakes the thread, which ends, and joins it. The connections it still holds are closed as it goes.
    ~KeepAliveServer() {
        if (thread_.joinable()) {
            const char wake = 1;
            std::ignore     = ::write(wake_write_.get(), &wake, 1);
            thread_.join();
        }
    }

    KeepAliveServer(const KeepAliveServer&)            = delete;
    KeepAliveServer& operator=(const KeepAliveServer&) = delete;

    // False if the socket could not be made, bound or put to listen: the test stops there.
    bool listening() const { return thread_.joinable(); }

    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    // Every request read so far, in the order it was read. A copy, taken under the lock.
    std::vector<ReadRequest> requests() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return read_;
    }

    // The connections accepted so far.
    int connections() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return accepted_;
    }

private:
    using Descriptor = keepalive_detail::Descriptor;

    // One open connection. `pending` holds bytes that have arrived and are not yet a whole request.
    struct Connection {
        Descriptor  fd;
        int         number{0};
        int         served{0};
        std::string pending;
    };

    static constexpr int              kBacklog = 16;
    static constexpr std::string_view kReply   = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nok";
#if defined(MSG_NOSIGNAL)
    static constexpr int kSendFlags = MSG_NOSIGNAL;  // Linux: a send to a peer that has gone is an error, not a SIGPIPE
#else
    static constexpr int kSendFlags = 0;             // macOS: SO_NOSIGPIPE is set on each accepted socket instead
#endif

    void run() {
        std::vector<Connection> connections;
        int                     next_number = 1;
        for (;;) {
            std::vector<pollfd> watched;
            watched.push_back(pollfd{wake_read_.get(), POLLIN, 0});
            watched.push_back(pollfd{listener_.get(), POLLIN, 0});
            for (const Connection& connection : connections)
                watched.push_back(pollfd{connection.fd.get(), POLLIN, 0});

            if (::poll(watched.data(), static_cast<nfds_t>(watched.size()), -1) < 0) {
                if (errno == EINTR)
                    continue;
                return;
            }
            if (watched[0].revents != 0)
                return;  // the destructor wrote to the pipe

            // The open connections first: `watched` lines up with the ones that existed at poll(), and accepting appends to the list.
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (watched[i + 2].revents != 0)
                    serve(connections[i]);
            }
            std::erase_if(connections, [](const Connection& connection) { return !connection.fd.valid(); });

            if (watched[1].revents != 0)
                accept_one(connections, next_number);
        }
    }

    void accept_one(std::vector<Connection>& connections, int& next_number) {
        Descriptor client(::accept(listener_.get(), nullptr, nullptr));
        if (!client.valid())
            return;
#if defined(SO_NOSIGPIPE)
        const int on = 1;
        ::setsockopt(client.get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++accepted_;
        }
        connections.push_back(Connection{std::move(client), next_number++, 0, {}});
    }

    // Reads what the client has sent on this connection and acts on each request that is now whole. A connection that the client closed, or
    // that fails, or that a rule drops, is closed here (its descriptor reset) and removed by the caller.
    void serve(Connection& connection) {
        std::array<char, 4096> chunk{};
        const ssize_t          received = ::recv(connection.fd.get(), chunk.data(), chunk.size(), 0);
        if (received <= 0) {
            connection.fd.reset();
            return;
        }
        connection.pending.append(chunk.data(), static_cast<std::size_t>(received));

        ReadRequest request;
        while (connection.fd.valid() && take_request(connection.pending, request)) {
            request.connection    = connection.number;
            request.served_before = connection.served++;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                read_.push_back(request);
            }
            // A dropped request gets no reply and its connection is closed; the request was read in full, so the client sees a clean close.
            // A reply that cannot be sent closes the connection too.
            if (drop_(request) || !send_all(connection.fd.get(), kReply))
                connection.fd.reset();
        }
    }

    static bool send_all(const int fd, std::string_view data) {
        while (!data.empty()) {
            const ssize_t sent = ::send(fd, data.data(), data.size(), kSendFlags);
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent <= 0)
                return false;
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return true;
    }

    // The Content-Length of a request head, or 0 if it has none (a GET, or a DELETE without a body).
    static std::size_t content_length(const std::string_view head) {
        std::string lowered(head);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        constexpr std::string_view key = "\r\ncontent-length:";
        const std::size_t          at  = lowered.find(key);
        if (at == std::string::npos)
            return 0;
        std::size_t first = at + key.size();
        while (first < lowered.size() && lowered[first] == ' ')
            ++first;
        std::size_t value  = 0;
        const auto  parsed = std::from_chars(lowered.data() + first, lowered.data() + lowered.size(), value);
        return parsed.ec == std::errc{} ? value : 0;
    }

    // Moves the first whole request out of `pending` into `request`. False, with `pending` untouched, if it holds less than one.
    static bool take_request(std::string& pending, ReadRequest& request) {
        const std::size_t head_end = pending.find("\r\n\r\n");
        if (head_end == std::string::npos)
            return false;
        const std::string_view head(pending.data(), head_end);
        const std::size_t      body_size = content_length(head);
        const std::size_t      total     = head_end + 4 + body_size;
        if (pending.size() < total)
            return false;

        // The request line: METHOD SP TARGET SP VERSION.
        const std::size_t method_end = head.find(' ');
        const std::size_t target_end = method_end == std::string_view::npos ? std::string_view::npos : head.find(' ', method_end + 1);
        if (target_end == std::string_view::npos) {
            request.method = std::string(head);  // not a request line: keep what arrived
            request.target.clear();
        } else {
            request.method = std::string(head.substr(0, method_end));
            request.target = std::string(head.substr(method_end + 1, target_end - method_end - 1));
        }
        request.body = pending.substr(head_end + 4, body_size);
        pending.erase(0, total);
        return true;
    }

    DropRule   drop_;
    Descriptor listener_;
    Descriptor wake_read_;
    Descriptor wake_write_;
    int        port_{0};

    mutable std::mutex       mutex_;
    std::vector<ReadRequest> read_;  // guarded by mutex_
    int                      accepted_{0};  // guarded by mutex_

    // Last, so that it is destroyed first; the destructor has joined it by then, and it uses every member above.
    std::thread thread_;
};
