# QM HTTP Client Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_http_client` from the `qumi/examples` tree.

## Files

```text
qumi/examples/qm_http_client/
├── README.md
├── README_CN.md
├── qm_http_client_example.c
└── qm_config/
    └── qm_config.h
```

## Features

- Wi-Fi STA initialization and reconnect
- Simple GET request
- POST, PUT, DELETE, and HEAD requests
- Streamed request body upload
- Handle-based manual flow
- Manual response body read
- Range download and resume-style download flow
- Redirect handling
- HTTPS one-way authentication and optional custom CA
- HTTPS mutual authentication
- Basic and Digest authentication examples

## Common Macros

```c
#define EXAMPLE_WIFI_SSID     "your_ssid"
#define EXAMPLE_WIFI_PASSWORD "your_password"
```

The example also defines test URLs such as `QM_HTTP_CLIENT_GET_URL`,
`QM_HTTP_CLIENT_POST_URL`, `QM_HTTP_CLIENT_HTTPS_URL`, and
`QM_HTTP_CLIENT_DIGEST_URL` in `qm_http_client_example.c`.

## Entry Points

Call `qm_http_client_wifi_demo_start()` to connect Wi-Fi and run the full demo.
Individual flows can also be called directly, for example:

```c
qm_http_client_example_start();
qm_http_client_post_example_start();
qm_http_client_https_example_start();
qm_http_client_digest_example_start();
```

## Integration

This example lives under `qumi/examples/qm_http_client/`. Add
`qumi/examples/qm_http_client/qm_http_client_example.c` to your project sources
and merge `qumi/examples/qm_http_client/qm_config/qm_config.h` into your project
configuration.
