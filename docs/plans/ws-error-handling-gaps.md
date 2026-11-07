# WebSocket Error Handling Gaps

Generated: 2026-03-23
Implemented: 2026-03-24 (Gaps 1 and 2)

---

## Background

Discovered during a live session (2026-03-23) in which the tick stream stopped at ~14:25
with no log output. Investigation traced the silence to three places where WebSocket errors
and disconnects can occur without any notification reaching the caller.

---

## Gap 1 — `IxWsConnection` drops `ix::WebSocketMessageType::Error` frames ✓ Fixed

**File:** `include/kraken_ix_ws_connection.hpp`

**Severity:** High

### Problem

The `setOnMessageCallback` switch in `IxWsConnection::connect()` handles `Open`, `Close`, and
`Message`, but falls through to `default: break` for `ix::WebSocketMessageType::Error`:

```cpp
switch (msg->type) {
    case ix::WebSocketMessageType::Open:
        if (open_cb_)  open_cb_();
        break;
    case ix::WebSocketMessageType::Close:
        if (close_cb_) close_cb_();
        break;
    case ix::WebSocketMessageType::Message:
        if (msg_cb_)   msg_cb_(msg->str);
        break;
    default:
        break;  // ← Error frames land here and are silently swallowed
}
```

ixwebsocket fires `Error` for TLS handshake failures, connection resets, protocol violations,
and DNS resolution failures. All of these are invisible to the caller.

### Fix

1. Add `ErrorCb = std::function<void(const std::string& reason)>` to `IWsConnection` alongside
   the existing `MessageCb`, `OpenCb`, and `CloseCb`.
2. Add `set_on_error(ErrorCb cb)` to the `IWsConnection` interface.
3. Handle the case in `IxWsConnection`:
   ```cpp
   case ix::WebSocketMessageType::Error:
       if (error_cb_) error_cb_(msg->errorInfo.reason);
       break;
   ```
4. In `KrakenWsClient::init()`, register a default error handler that logs via
   `error_handler_` (or a fallback `RateLimitedWsErrorHandler`).

### Implemented

- `IWsConnection` gained `ErrorCb` type alias and pure virtual `set_on_error(ErrorCb)`.
- `IWsErrorHandler` gained pure virtual `on_connection_error(const std::string& reason)`.
- `RateLimitedWsErrorHandler::on_connection_error` logs to `stderr` (same approach as
  `on_malformed_frame` — no external dep in `libkraken.a`).
- `IxWsConnection` handles `ix::WebSocketMessageType::Error`, stores `error_cb_`, and
  implements `set_on_error`.
- `KrakenWsClient::init()` registers the error callback, routing through `error_handler_`.
- 4 new tests in `tests/unit/test_ws_client.cpp` (`ConnectionError.*`).

---

## Gap 2 — `KrakenWsClient` close notification never reaches callers ✓ Fixed

**File:** `src/kraken_ws_client.cpp`

**Severity:** High

### Problem

`KrakenWsClient::init()` registers a close callback that only sets an internal flag:

```cpp
conn_->set_on_close([weak_self]() {
    if (auto self = weak_self.lock()) self->connected_.store(false);
});
```

There is no way for a caller to be notified when the socket closes. When the Kraken server
drops the connection, `connected_` becomes `false` and ticks silently stop — the caller's
feed exits cleanly with no warning.

### Fix

1. Add `set_on_disconnect(std::function<void()> cb)` to `KrakenWsClient` (public).
2. Store the callback and call it from the existing close handler in `init()`:
   ```cpp
   conn_->set_on_close([weak_self]() {
       if (auto self = weak_self.lock()) {
           self->connected_.store(false);
           if (self->disconnect_cb_) self->disconnect_cb_();
       }
   });
   ```
3. A caller registers a disconnect callback:
   ```cpp
   client->set_on_disconnect([]() {
       // log the loss, or schedule a reconnect
   });
   ```

### Implemented

- `KrakenWsClient` gained public `set_on_disconnect(std::function<void()> cb)`.
- `disconnect_cb_` field added; invoked from the close handler in `init()` after
  `connected_` is set to false.
- 5 new tests in `tests/unit/test_ws_client.cpp` (`Disconnect.*`).

---

## Gap 3 — Malformed frame errors log to `stderr`, not to the caller's logger

**File:** `RateLimitedWsErrorHandler`, the default `IWsErrorHandler` of `KrakenWsClient`

**Severity:** Medium

### Problem

`KrakenWsClient::init()` installs a `RateLimitedWsErrorHandler` when `nullptr` is passed as
the error handler, but that handler uses `std::fprintf(stderr, ...)`. In deployments where
stderr is not captured, malformed-frame warnings are silently lost.

### Fix

A caller that has a logging library passes a custom `IWsErrorHandler` that routes through it:

```cpp
class LoggingWsErrorHandler : public kraken::ws::IWsErrorHandler {
public:
    void on_malformed_frame(const std::string& raw,
                            const std::exception& e) override {
        log_warn("[ws] malformed frame ({}): {:.120}", e.what(), raw);
    }

    void on_connection_error(const std::string& reason) override {
        log_error("[ws] connection error: {}", reason);
    }
};
```

and hands it to the factory:

```cpp
auto client = kraken::ws::make_ws_client(
    std::string(kraken::ws::PUBLIC_WS_URL),
    std::make_shared<LoggingWsErrorHandler>());
```

Note: the library is not changed for this gap. The root cause is `RateLimitedWsErrorHandler`
writing to `stderr` rather than accepting an injectable sink, so the remedy is on the caller's
side.

---

## Summary

| Gap | File | Severity | Status |
|-----|------|----------|--------|
| `Error` frame dropped | `kraken_ix_ws_connection.hpp` | High | ✓ Fixed 2026-03-24 |
| No disconnect callback | `kraken_ws_client.cpp` / `.hpp` | High | ✓ Fixed 2026-03-24 |
| Malformed frames → stderr | `RateLimitedWsErrorHandler` | Medium | Open — a caller passes its own `IWsErrorHandler` |
