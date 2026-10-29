# WebSocket Client Transport Layer Design

## 1. Transport Layer Overview

The WebSocket client transport layer is designed to provide a flexible and extensible interface for network communication, supporting both TCP (ws://) and TLS (wss://) protocols with various authentication mechanisms.

## 2. Transport Layer File Structure

```
components/qm_websocket_client/
├── include/
│   ├── qm_websocket_client.h    # WebSocket client API definitions
│   └── qm_websocket_transport.h # Internal transport adapter API
└── src/
    ├── qm_websocket_client.c    # Core WebSocket logic
    └── qm_websocket_transport.c # Transport layer implementation
```

## 3. Transport Layer API

### 3.1 Transport Interface Definition

```c
typedef struct {
    void *(*connect)(const char *host, uint16_t port, void *config);
    int (*write)(void *handle, const char *buf, int len, uint32_t timeout_ms);
    int (*read)(void *handle, char *buf, int len, uint32_t timeout_ms);
    int (*close)(void *handle);
    void *config;
} qm_websocket_transport_t;
```

### 3.2 TLS Configuration

```c
typedef struct {
    const char *ca_crt;             /*!< CA certificate */
    int ca_crt_len;                 /*!< CA certificate length */
    const char *client_crt;         /*!< Client certificate */
    int client_crt_len;             /*!< Client certificate length */
    const char *client_key;         /*!< Client private key */
    int client_key_len;             /*!< Client private key length */
} qm_websocket_tls_config_t;
```

### 3.3 TLS Configuration API

```c
int qm_websocket_tls_config_init(qm_websocket_tls_config_t *tls_config,
                                 const char *ca_crt, int ca_crt_len,
                                 const char *client_crt, int client_crt_len,
                                 const char *client_key, int client_key_len);
```

Set only `ca_crt` for server authentication. Additionally set `client_crt` and
`client_key` for mutual authentication.

`qm_websocket_client_init()` copies the TLS config structure, but certificate buffers are referenced and must remain valid while the client may reconnect.

The WebSocket client creates the built-in TCP/TLS transport according to `uri`.
External code uses `qm_websocket_client.h` only. The transport header is an
internal component header used by `qm_websocket_client.c` to call the adapter
layer create/destroy functions.

## 4. Implementation Details

### 4.1 TCP Transport Implementation

The TCP transport implementation is based on qumi's TCP interface (`qm_tcp.h`):

- `connect`: Uses `qm_tcp_establish(host, port)` to create a TCP connection
- `write`: Uses `qm_tcp_write(handle, buf, len, timeout_ms)` to send data
- `read`: Uses `qm_tcp_read(handle, buf, len, timeout_ms)` to receive data
- `close`: Uses `qm_tcp_destroy(handle)` to close the connection

### 4.2 TLS Transport Implementation

The TLS transport implementation is based on qumi's TLS interface (`qm_tls.h`):

- `connect`: Uses `qm_tls_establish(host, port, tls_config)` to create a TLS connection
- `write`: Uses `qm_tls_write(handle, buf, len, timeout_ms)` to send data
- `read`: Uses `qm_tls_read(handle, buf, len, timeout_ms)` to receive data
- `close`: Uses `qm_tls_destroy(handle)` to close the connection

### 4.3 Authentication Support

#### 4.3.1 Server Authentication (单向认证)

- Only CA certificate is provided
- Client verifies server's certificate against the CA
- Suitable for most production scenarios

#### 4.3.2 Mutual Authentication (双向认证)

- Both CA certificate and client certificate/key are provided
- Server verifies client's certificate against the CA
- Client verifies server's certificate against the CA
- Provides highest level of security
- Suitable for sensitive applications

## 5. Dependency on qumi Components

The transport layer has strict dependencies on qumi components, which are used consistently throughout the implementation:

| Component | Interface | Usage |
|-----------|-----------|-------|
| qm_tcp | qm_tcp.h | TCP connection management |
| qm_tls | qm_tls.h | TLS connection management |
| qm_log | qm_log.h | Logging |
| qm_event | qm_event.h | Event handling (used in core WebSocket logic) |

### 5.1 Interface严谨性验证

1. **命名一致性**: All functions and structures follow qumi's naming convention with `qm_` prefix
2. **Error handling**: Consistent error codes and return values matching qumi's error handling pattern
3. **Parameter validation**: All interfaces validate input parameters to prevent invalid operations
4. **Memory management**: Proper memory allocation and deallocation following qumi's best practices
5. **Configuration structure**: TLS configuration structure aligns with qumi's TLS interface requirements

## 6. Usage Examples

### 6.1 TCP WebSocket Client

```c
// Configure WebSocket client
qm_websocket_client_config_t websocket_cfg = {
    .uri = "ws://echo.websocket.org",
};

// Create and use WebSocket client...
```

### 6.2 TLS WebSocket Client with Server Authentication

```c
qm_websocket_tls_config_t tls_config;

qm_websocket_tls_config_init(&tls_config,
                             ca_cert, strlen(ca_cert),
                             NULL, 0,
                             NULL, 0);

// Configure WebSocket client
qm_websocket_client_config_t websocket_cfg = {
    .uri = "wss://echo.websocket.org",
    .tls_config = &tls_config,
};

// Create and use WebSocket client...
```

### 6.3 TLS WebSocket Client with Mutual Authentication

```c
qm_websocket_tls_config_t tls_config;

qm_websocket_tls_config_init(&tls_config,
                             ca_cert, strlen(ca_cert),
                             client_cert, strlen(client_cert),
                             client_key, strlen(client_key));

// Configure WebSocket client
qm_websocket_client_config_t websocket_cfg = {
    .uri = "wss://secure.websocket.example.com",
    .tls_config = &tls_config,
};

// Create and use WebSocket client...
```

### 6.4 Custom Transport

```c
// Create a custom transport only when replacing the built-in TCP/TLS transport.
qm_websocket_transport_t *transport = custom_websocket_transport_create();

// Configure WebSocket client
qm_websocket_client_config_t websocket_cfg = {
    .uri = "wss://echo.websocket.org",
    .transport = transport,
};

// Create and use WebSocket client...
```

## 7. Security Considerations

1. **Certificate Management**: Always use valid certificates from trusted CAs in production
2. **Private Key Security**: Ensure client private keys are stored securely and not exposed
3. **Secure Defaults**: TLS transport uses secure default settings provided by qumi's TLS implementation
4. **Connection Validation**: Server certificates are validated against the provided CA certificate

## 8. Extensibility

The transport layer is designed for extensibility:

1. **Custom Transport**: Users can implement their own transport by providing a `qm_websocket_transport_t` structure
2. **Protocol Support**: Easy to add support for other protocols (e.g., QUIC)
3. **Configuration**: Flexible configuration options for TLS and TCP settings

## 9. Conclusion

The WebSocket client transport layer provides a robust, secure, and flexible interface for network communication, supporting both TCP and TLS protocols with various authentication mechanisms. It follows qumi's component specifications and uses qumi's interfaces consistently, ensuring a high level of integration and reliability.
