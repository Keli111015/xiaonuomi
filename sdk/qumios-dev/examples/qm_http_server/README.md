# QM HTTP Server Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_http_server` from the `qumi/examples` tree.

## Files

```text
qumi/examples/qm_http_server/
├── README.md
├── README_CN.md
├── qm_http_server_example.c
└── qm_config/
    └── qm_config.h
```

## Features

- Wi-Fi STA initialization and reconnect
- HTTP server start and stop
- Route registration for GET, POST, PUT, DELETE, HEAD, and wildcard paths
- Query parameter parsing
- Request header reading
- Full body read and streaming body read
- Normal response, chunked response, and custom headers
- Optional HTTPS path controlled by `QM_HTTP_SERVER_EXAMPLE_USE_HTTPS`

## Common Macros

```c
#define EXAMPLE_WIFI_SSID     "your_ssid"
#define EXAMPLE_WIFI_PASSWORD "your_password"
```

Set `QM_HTTP_SERVER_EXAMPLE_USE_HTTPS` to `1` when the platform provides the
server certificate and private key symbols used by the example.

## Entry Point

```c
qm_http_server_wifi_demo_start();
```

After the device gets an IP address, the example starts the HTTP server and
prints route activity in logs.

## Integration

This example lives under `qumi/examples/qm_http_server/`. Add
`qumi/examples/qm_http_server/qm_http_server_example.c` to your project sources
and merge `qumi/examples/qm_http_server/qm_config/qm_config.h` into your project
configuration.
