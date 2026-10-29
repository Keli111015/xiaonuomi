# QM WebSocket Client Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_websocket_client` from the existing
`qumi/examples/websocket_client/` example directory.

## Files

```text
qumi/examples/websocket_client/
├── README.md
├── README_CN.md
└── websocket_client_example.c
```

## Features

- Wi-Fi STA initialization and reconnect
- WebSocket client creation from a URI
- WebSocket client creation from host, port, path, and SSL flag
- Text, binary, and fragmented frame sending
- Reconnect and graceful close examples
- TLS server authentication and mutual authentication entry points

## Common Macros

```c
#define EXAMPLE_WIFI_SSID       "your_ssid"
#define EXAMPLE_WIFI_PASSWORD   "your_password"
#define EXAMPLE_WEBSOCKET_URI   "ws://echo.websocket.events"
#define EXAMPLE_WEBSOCKET_HOST  "echo.websocket.events"
#define EXAMPLE_WEBSOCKET_PORT  80
#define EXAMPLE_WEBSOCKET_PATH  "/"
#define EXAMPLE_WEBSOCKET_SSL   QM_FALSE
```

TLS examples use `EXAMPLE_TLS_CA_CERT`, `EXAMPLE_TLS_CLIENT_CERT`, and
`EXAMPLE_TLS_CLIENT_KEY` when configured.

## Entry Points

```c
qm_application_start();
websocket_client_example_start(EXAMPLE_WEBSOCKET_URI);
websocket_client_example_stop(handle);
```

`qm_application_start()` connects Wi-Fi first and starts the WebSocket client
after IP acquisition.

## Integration

This example lives under `qumi/examples/websocket_client/`. Add
`qumi/examples/websocket_client/websocket_client_example.c` to your project
sources and enable the `qm_websocket_client`, Wi-Fi, and event queue
dependencies in your project configuration.
