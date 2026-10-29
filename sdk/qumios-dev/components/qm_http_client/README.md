# QM HTTP Client

`qm_http_client` is a QUMI HTTP/HTTPS client with request/response parsing,
redirects, Basic and Digest authentication, and streaming body callbacks. The
public API does not expose platform TCP or TLS types.

## Architecture

```text
application request and callbacks
                v
         qm_http_client
                | URL, headers, auth, response parser
                v
      QUMI TCP or TLS transport
                v
             HTTP service
```

The component owns HTTP protocol handling. The caller owns request strings and
callback behavior, and must copy event buffers if data is retained after a
callback. It does not persist response bodies or configure networking.

## Responsibility Boundary

Network readiness, storage of response data, and application retry decisions
remain outside the client.

An optional `interrupted(user)` predicate provides cooperative cancellation.
The client checks it before and after each transport read/write, including
header and chunk parsing, and returns `-QM_EINTR`. The predicate must be
nonblocking and read caller-owned synchronized state that remains set until
the request returns. The requesting task must not concurrently close or free
the client; the HTTP caller performs cleanup after its current call returns.
`timeout_ms` bounds an individual transport read/write. DNS, TCP connect, and
TLS handshake still follow the HAL implementation's bounds; the predicate
does not preempt a connection call already in progress.

## Usage

Use either a one-shot request or a reusable client handle. Fill
`qm_http_client_request_t` with a URL, method, headers/body when needed, and
an `on_event` or compatible `on_body` callback; then call the request API
described in `include/qm_http_client.h`.

`CONNECTED`, `HEADER`, `DATA`, `FINISH`, `DISCONNECTED`, `ERROR`, and
`REDIRECT` events expose transport and parser progress. Body data may be
fixed-length, chunked, or until-close and is delivered incrementally.

## Configuration

Enable `QM_HTTP_CLIENT_SUPPORT` in `qm_kconfig`. Kconfig defines default
timeouts and buffer limits; per-request fields override them. The detailed
Chinese API guide and OpenCPU integration notes are in `README_CN.md`.
