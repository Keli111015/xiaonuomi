# QM HTTP Server

`qm_http_server` exposes a route-based HTTP/HTTPS server over QUMI TCP/TLS
transport. It parses requests and provides bounded streaming request and
chunked response APIs without exposing platform networking types.

## Architecture

```text
HTTP client
    v
QUMI TCP/TLS accept and byte stream
    v
qm_http_server
    | route match, request parser, response writer
    v
application route and error handlers
```

The component owns listener and protocol processing. Applications own static
route tables, response content, certificate lifetime, and any Wi-Fi or network
bring-up. Request and response objects are valid only during their handler.

## Responsibility Boundary

Handlers own business authorization, storage, and response payloads; the server
does not configure network interfaces or persist request bodies.

## Basic Usage

Define static `qm_http_route_t` entries, fill `qm_http_server_config_t` with a
port and route table, then call `qm_http_server_create()` and
`qm_http_server_start()`. Use `qm_http_request_read()` for streamed input and
`qm_http_response_send()` or chunked response APIs for output. Destroy the
server when it is no longer required.

Enable `QM_HTTP_SERVER_SUPPORT` in `qm_kconfig`. `README_CN.md` contains the
route, HTTPS, and OpenCPU example details.
