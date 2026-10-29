/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _QM_WEBSOCKET_CLIENT_H_
#define _QM_WEBSOCKET_CLIENT_H_

#include "qm_types.h"
#include "qm_errno.h"
#include "qm_websocket_client_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket client event id.
 *
 * Event callbacks are registered with qm_websocket_register_events(). Use
 * WEBSOCKET_EVENT_MAX only as a wildcard when registering or unregistering
 * handlers; it is not dispatched as a runtime event.
 */
typedef enum {
    WEBSOCKET_EVENT_CONNECTED = 0,    /*!< Connected to server */
    WEBSOCKET_EVENT_DATA,             /*!< Data received */
    WEBSOCKET_EVENT_DISCONNECTED,     /*!< Disconnected from server */
    WEBSOCKET_EVENT_ERROR,            /*!< Error occurred */
    WEBSOCKET_EVENT_MAX               /*!< Event count; wildcard for register/unregister APIs */
} qm_websocket_event_id_t;

/**
 * @brief WebSocket client error type.
 *
 * The error type is reported in qm_websocket_event_data_t::error_info when a
 * WEBSOCKET_EVENT_ERROR event is dispatched.
 */
typedef enum {
    WEBSOCKET_ERROR_TYPE_NONE = 0,      /*!< No error recorded */
    WEBSOCKET_ERROR_TYPE_TRANSPORT,     /*!< Transport read/write/connect error */
    WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT,  /*!< PONG timeout retries were exhausted */
    WEBSOCKET_ERROR_TYPE_HANDSHAKE,     /*!< HTTP upgrade handshake failed */
    WEBSOCKET_ERROR_TYPE_SERVER_CLOSE,  /*!< Server sent a close frame or closed the connection */
    WEBSOCKET_ERROR_TYPE_PROTOCOL,      /*!< Invalid WebSocket protocol data */
    WEBSOCKET_ERROR_TYPE_MEMORY,        /*!< Memory allocation failed */
} qm_websocket_error_type_t;

/**
 * @brief WebSocket client error details.
 */
typedef struct {
    qm_websocket_error_type_t error_type; /*!< WebSocket error category */
    int transport_error;                  /*!< Transport layer return/error code if available, failures use negative values */
    int handshake_status_code;            /*!< HTTP status from upgrade response if available */
} qm_websocket_error_info_t;

/**
 * @brief WebSocket client event data.
 *
 * The event data object and data_ptr buffer are owned by the component and are
 * valid only during the event callback. Copy the payload in the callback if it
 * must be used later.
 */
typedef struct {
    const char *data_ptr;             /*!< Pointer to the received data, valid only during the event callback */
    int data_len;                     /*!< Length of the received data */
    int opcode;                       /*!< WebSocket opcode */
    qm_bool fin;                         /*!< FIN flag */
    int payload_len;                  /*!< Total payload length */
    int payload_offset;               /*!< Offset of data in the total payload */
    void *client;                     /*!< WebSocket client handle */
    void *user_context;               /*!< User context */
    int error_code;                   /*!< Error code, failures use negative values */
    qm_websocket_error_info_t error_info; /*!< Detailed error information */
} qm_websocket_event_data_t;

/**
 * @brief WebSocket client event handler type.
 *
 * @param[in] event_id Event currently being dispatched
 * @param[in] event_data Event payload and status data, valid only during callback
 * @param[in] user_context Context passed to qm_websocket_register_events()
 */
typedef void (*qm_websocket_event_handler_t)(qm_websocket_event_id_t event_id, qm_websocket_event_data_t *event_data, void *user_context);

/**
 * @brief WebSocket client transport interface for custom transports.
 */
typedef struct {
    /**
     * @brief Open a transport connection.
     *
     * @param[in] host Server host name or IP address parsed from URI
     * @param[in] port Server port parsed from URI
     * @param[in] config Transport-specific configuration pointer
     * @return A non-NULL connection handle on success. The handle is later
     *         passed to read, write and close.
     */
    void *(*connect)(const char *host, uint16_t port, void *config);

    /**
     * @brief Write data to the connection.
     *
     * @param[in] handle Connection handle returned by connect
     * @param[in] buf Data buffer to write
     * @param[in] len Number of bytes to write
     * @param[in] timeout_ms Write timeout in milliseconds
     * @return Positive bytes written, 0 on timeout, or negative error code.
     */
    int (*write)(void *handle, const char *buf, int len, uint32_t timeout_ms);

    /**
     * @brief Read data from the connection.
     *
     * @param[in] handle Connection handle returned by connect
     * @param[out] buf Buffer for received bytes
     * @param[in] len Maximum number of bytes to read
     * @param[in] timeout_ms Read timeout in milliseconds
     * @return Positive bytes read, 0 on timeout, or negative error code.
     */
    int (*read)(void *handle, char *buf, int len, uint32_t timeout_ms);

    /**
     * @brief Close and release a connection handle returned by connect.
     *
     * @param[in] handle Connection handle returned by connect
     * @return QM_EOK on success, or a negative error code on failure
     */
    int (*close)(void *handle);

    /**
     * @brief Transport-specific configuration passed to connect.
     */
    void *config;

} qm_websocket_transport_t;

/**
 * @brief WebSocket client TLS configuration.
 *
 * The structure is copied by qm_websocket_client_init(), but certificate and
 * key buffers are referenced directly. Keep the buffers valid while the client
 * exists and may reconnect.
 */
typedef struct {
    const char *ca_crt;             /*!< CA certificate */
    int ca_crt_len;                 /*!< CA certificate length */
    const char *client_crt;         /*!< Client certificate */
    int client_crt_len;             /*!< Client certificate length */
    const char *client_key;         /*!< Client private key */
    int client_key_len;             /*!< Client private key length */
} qm_websocket_tls_config_t;

/**
 * @brief Initialize TLS config.
 *
 * Set only ca_crt for server authentication; additionally set client_crt and
 * client_key for mutual authentication. The certificate buffers are referenced,
 * not copied. They must remain valid while the WebSocket client may reconnect.
 *
 * @param[out] tls_config TLS configuration object to initialize
 * @param[in] ca_crt CA certificate buffer, or NULL to use transport defaults
 * @param[in] ca_crt_len CA certificate length in bytes, or 0 when ca_crt is NULL
 * @param[in] client_crt Client certificate buffer for mutual TLS, or NULL
 * @param[in] client_crt_len Client certificate length in bytes, or 0
 * @param[in] client_key Client private key buffer for mutual TLS, or NULL
 * @param[in] client_key_len Client private key length in bytes, or 0
 * @return QM_EOK on success
 * @return -QM_EINVAL if tls_config is NULL, a length is negative, or only one of
 *         client_crt/client_key is provided
 */
int qm_websocket_tls_config_init(qm_websocket_tls_config_t *tls_config,
                                 const char *ca_crt, int ca_crt_len,
                                 const char *client_crt, int client_crt_len,
                                 const char *client_key, int client_key_len);

/**
 * @brief WebSocket client configuration.
 *
 * Zero-valued numeric fields use component defaults, except where a field
 * explicitly documents a different meaning. String fields are copied during
 * qm_websocket_client_init(). The user_context pointer is stored as-is.
 */
typedef struct {
    const char *uri;                  /*!< WebSocket URI, e.g. ws://host[:port][/path][?query] or wss://host[:port][/path][?query] */
    const char *host;                 /*!< Server host name or IP address. Used when uri is NULL */
    int port;                         /*!< Server port. Used when host is set; 0 selects 80 for ws or 443 for wss */
    const char *path;                 /*!< WebSocket path. Used when host is set; NULL or empty uses "/" */
    qm_bool transport_over_ssl;       /*!< Use TLS transport when host is set without uri */
    const char *subprotocol;          /*!< WebSocket subprotocol, copied during init */
    const char *user_agent;           /*!< User agent, copied during init */
    const char *headers;              /*!< Additional headers, copied during init; same format as qm_websocket_client_set_headers() */
    const char *auth;                 /*!< HTTP authorization header, copied during init */
    int task_priority;                /*!< Task priority */
    int task_stack;                   /*!< Task stack size */
    int keepalive_ping_interval_ms;   /*!< Keepalive ping interval in milliseconds, 0 uses default unless disable_ping is QM_TRUE */
    int reconnect_interval_ms;        /*!< Base reconnect backoff interval in milliseconds */
    int max_reconnect_timeout_ms;     /*!< Maximum reconnect backoff timeout in milliseconds */
    int max_reconnect_count;          /*!< Maximum reconnect count */
    qm_bool disable_auto_reconnect;      /*!< Disable auto reconnect */
    qm_bool disable_ping;                /*!< Disable WebSocket ping at startup */
    qm_bool propagate_control_frames;    /*!< Propagate control frames to user */
    int network_timeout_ms;           /*!< Network read/write timeout in milliseconds */
    int buffer_size;                  /*!< Receive buffer size for chunked payload dispatch */
    void *user_context;               /*!< User context */
    const qm_websocket_tls_config_t *tls_config; /*!< TLS configuration, ignored when transport is set */
    qm_websocket_transport_t *transport;   /*!< Custom transport interface. Caller owns the transport object. */
    int send_queue_size;              /*!< Send queue capacity, 0 uses component default */
} qm_websocket_client_config_t;

/**
 * @brief WebSocket client state.
 */
typedef enum {
    WEBSOCKET_STATE_INIT = 0,  /*!< Client created or reset, connection task not connected */
    WEBSOCKET_STATE_CONNECTING, /*!< Client task is opening transport and performing handshake */
    WEBSOCKET_STATE_CONNECTED,  /*!< WebSocket handshake completed and data can be sent */
    WEBSOCKET_STATE_CLOSING,    /*!< Client is closing or stopping the active connection */
    WEBSOCKET_STATE_CLOSED      /*!< Client connection task is stopped or connection is closed */
} qm_websocket_state_t;

typedef enum {
    QM_WEBSOCKET_NETWORK_DOWN = 0, /*!< Network is unavailable; connection attempts are paused */
    QM_WEBSOCKET_NETWORK_UP,       /*!< Network is available; connection attempts may proceed */
} qm_websocket_network_state_t;

/**
 * @brief Create a new WebSocket client
 *
 * The returned handle must be released with qm_websocket_client_destroy().
 *
 * @param[in] config Configuration for the client. config must not be NULL.
 *            Either config->uri or config->host must be set.
 * @return WebSocket client handle, or NULL on error
 */
void *qm_websocket_client_init(const qm_websocket_client_config_t *config);

/**
 * @brief Start the WebSocket client
 *
 * Creates the client task. The connection and WebSocket handshake run
 * asynchronously; wait for WEBSOCKET_EVENT_CONNECTED before sending data.
 *
 * @param[in] client WebSocket client handle
 * @return QM_EOK on success
 * @return -QM_EBUSY if the client task is already running
 * @return error code on error
 */
int qm_websocket_client_start(void *client);

/**
 * @brief Stop the WebSocket client
 *
 * Stops the client task and closes the transport immediately. Use
 * qm_websocket_client_close() for a WebSocket close handshake.
 *
 * @param[in] client WebSocket client handle
 * @return QM_EOK on success
 * @return -QM_ETIMEOUT if the client task does not stop within the stop timeout
 * @return error code on error
 */
int qm_websocket_client_stop(void *client);

/**
 * @brief Destroy the WebSocket client
 *
 * Stops the client if needed, releases queued messages, transport resources and
 * all memory owned by the client. The handle becomes invalid after success.
 *
 * @param[in] client WebSocket client handle
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_destroy(void *client);

/**
 * @brief Send binary data over WebSocket connection
 *
 * The data is copied into the internal send queue and sent asynchronously by
 * the client task.
 * Zero timeout does not wait for the admission lock or capacity. A finite
 * timeout budgets both waits and must be less than UINT32_MAX / 2. Disconnect
 * cancels queued frames; a waiting sender cannot cross into a new connection.
 * Serialize lifecycle calls and stop external send calls before destruction.
 *
 * @param[in] client WebSocket client handle
 * @param[in] data Binary data to send
 * @param[in] len Length of data
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return Bytes queued; -QM_EBUSY on closed/contended admission, -QM_EFULL
 * on zero-wait capacity exhaustion, -QM_ETIMEOUT on wait expiry, or another
 * negative qm_errno value. Success does not imply network delivery.
 */
int qm_websocket_client_send_bin(void *client, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Send text data over WebSocket connection
 *
 * The data is copied into the internal send queue and sent asynchronously by
 * the client task.
 *
 * @param[in] client WebSocket client handle
 * @param[in] data Text data to send
 * @param[in] len Length of data
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return Number of bytes queued for asynchronous send, or -1 on error
 */
int qm_websocket_client_send_text(void *client, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Queue a non-final binary frame for asynchronous send
 *
 * Start a fragmented binary message. Follow with zero or more
 * qm_websocket_client_send_continuation_frame() calls and finish with
 * qm_websocket_client_send_fin().
 *
 * @param[in] client WebSocket client handle
 * @param[in] data Binary fragment to send
 * @param[in] len Fragment length
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return Number of bytes queued for asynchronous send, or -1 on error
 */
int qm_websocket_client_send_bin_partial(void *client, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Queue a non-final text frame for asynchronous send
 *
 * Start a fragmented text message. Follow with zero or more
 * qm_websocket_client_send_continuation_frame() calls and finish with
 * qm_websocket_client_send_fin().
 *
 * @param[in] client WebSocket client handle
 * @param[in] data Text fragment to send
 * @param[in] len Fragment length
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return Number of bytes queued for asynchronous send, or -1 on error
 */
int qm_websocket_client_send_text_partial(void *client, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Queue a non-final continuation frame for asynchronous send
 *
 * Continue a fragmented message previously started with
 * qm_websocket_client_send_bin_partial() or
 * qm_websocket_client_send_text_partial().
 *
 * @param[in] client WebSocket client handle
 * @param[in] data Continuation fragment to send
 * @param[in] len Fragment length
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return Number of bytes queued for asynchronous send, or -1 on error
 */
int qm_websocket_client_send_continuation_frame(void *client, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Queue a final empty continuation frame for asynchronous send
 *
 * Finishes a fragmented message. Use after a partial binary/text frame and any
 * continuation frames have been queued.
 *
 * @param[in] client WebSocket client handle
 * @param[in] timeout_ms Queue wait timeout in milliseconds; 0 means do not wait
 * @return 0 on success, or -1 on error
 */
int qm_websocket_client_send_fin(void *client, uint32_t timeout_ms);

/**
 * @brief Close the WebSocket connection with an empty close frame.
 *
 * Sends a WebSocket CLOSE frame, requests the client task to stop, and waits up
 * to timeout_ms for the task to exit. If timeout_ms is 0, the component stop
 * timeout is used.
 *
 * @param[in] client WebSocket client handle
 * @param[in] timeout_ms Close wait timeout in milliseconds, or 0 for default
 * @return QM_EOK on success
 * @return -QM_EIO if the client is not connected or the close frame cannot be sent
 * @return -QM_ETIMEOUT if the client task does not stop before timeout
 * @return error code on invalid argument
 */
int qm_websocket_client_close(void *client, uint32_t timeout_ms);

/**
 * @brief Close the WebSocket connection with a status code and reason.
 *
 * Sends a WebSocket CLOSE frame containing the optional close status code and
 * reason payload, then waits for the client task to exit.
 *
 * @param[in] client WebSocket client handle
 * @param[in] code WebSocket close status code. Use 0 to omit the status code.
 * @param[in] data Optional close reason data, or NULL when len is 0
 * @param[in] len Close reason length in bytes
 * @param[in] timeout_ms Close wait timeout in milliseconds, or 0 for default
 * @return QM_EOK on success
 * @return -QM_EIO if the client is not connected or the close frame cannot be sent
 * @return -QM_ETIMEOUT if the client task does not stop before timeout
 * @return error code on invalid argument
 */
int qm_websocket_client_close_with_code(void *client, int code, const char *data, int len, uint32_t timeout_ms);

/**
 * @brief Register an event handler for WebSocket client
 *
 * Registering a handler for a specific event replaces the existing handler for
 * that event. Passing WEBSOCKET_EVENT_MAX registers the same handler for all
 * dispatchable events. Registration updates are synchronized with event
 * dispatch, so changes take effect for later callbacks.
 *
 * @param[in] client WebSocket client handle
 * @param[in] event_id Event ID to register handler for, or WEBSOCKET_EVENT_MAX for all events
 * @param[in] event_handler Event handler function
 * @param[in] user_context User data to pass to event handler
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_register_events(void *client, qm_websocket_event_id_t event_id, qm_websocket_event_handler_t event_handler, void *user_context);

/**
 * @brief Unregister an event handler from WebSocket client
 *
 * Passing WEBSOCKET_EVENT_MAX applies the operation to all events. Passing
 * event_handler as NULL clears any handler on the selected event or events.
 * Unregistration is synchronized with event dispatch, so it affects later
 * callbacks.
 *
 * @param[in] client WebSocket client handle
 * @param[in] event_id Event ID to unregister handler from, or WEBSOCKET_EVENT_MAX for all events
 * @param[in] event_handler Event handler function to remove, or NULL to remove any handler
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_unregister_events(void *client, qm_websocket_event_id_t event_id, qm_websocket_event_handler_t event_handler);

/**
 * @brief Set URI for WebSocket client
 *
 * Supported format: ws://host[:port][/path][?query] or
 * wss://host[:port][/path][?query]. If port is omitted, ws uses 80 and wss uses 443.
 * The client must be in INIT or CLOSED state.
 *
 * @param[in] client WebSocket client handle
 * @param[in] uri URI to set
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_uri(void *client, const char *uri);

/**
 * @brief Set server endpoint from host, port and path
 *
 * This is an alternative to qm_websocket_client_set_uri(). If port is 0, ws
 * uses 80 and wss uses 443. If path is NULL or empty, "/" is used.
 * The client must be in INIT or CLOSED state.
 *
 * @param[in] client WebSocket client handle
 * @param[in] host Server host name or IP address
 * @param[in] port Server port, or 0 for default
 * @param[in] path WebSocket path, or NULL for "/"
 * @param[in] transport_over_ssl QM_TRUE for wss/TLS, QM_FALSE for ws/TCP
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_host(void *client, const char *host, int port,
                                 const char *path, qm_bool transport_over_ssl);

/**
 * @brief Set additional WebSocket handshake headers
 *
 * Replaces all previously configured extra headers. Passing NULL clears the
 * extra header list. Header lines must be in "key: value\r\n" form. Each line
 * must have a non-empty key, contain a colon, and be terminated with "\r\n";
 * bare CR/LF and blank lines are rejected.
 *
 * @pre Client must not be running.
 *
 * @param[in] client WebSocket client handle
 * @param[in] headers Additional headers, each terminated with "\r\n"
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_headers(void *client, const char *headers);

/**
 * @brief Append one WebSocket handshake header
 *
 * Appends "key: value\r\n" to the extra header list. The key must not be empty
 * and key/value must not contain CR or LF characters.
 *
 * @pre Client must not be running.
 *
 * @param[in] client WebSocket client handle
 * @param[in] key Header key
 * @param[in] value Header value
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_append_header(void *client, const char *key, const char *value);

/**
 * @brief Reconnect the WebSocket client
 *
 * Disconnects the active transport, resets reconnect backoff state, and lets
 * the running client task reconnect. If the task is not running, the state is
 * reset for the next qm_websocket_client_start().
 *
 * @param[in] client WebSocket client handle
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_reconnect(void *client);

/**
 * @brief Update network availability without stopping the client task.
 *
 * When the network is down, active reconnect attempts are paused. A state
 * transition wakes the task so it can disconnect or resume promptly.
 */
int qm_websocket_client_network_update(void *client,
                                       qm_websocket_network_state_t state);

/**
 * @brief Get WebSocket ping interval
 *
 * Returns the current runtime ping interval. A value of 0 means ping is
 * disabled.
 *
 * @param[in] client WebSocket client handle
 * @return Ping interval in milliseconds, or -1 on error
 */
int qm_websocket_client_get_ping_interval(void *client);

/**
 * @brief Set WebSocket ping interval
 *
 * Setting 0 disables runtime ping. Setting a positive value while connected
 * resets the next ping deadline.
 *
 * @param[in] client WebSocket client handle
 * @param[in] ping_interval_ms Ping interval in milliseconds, 0 disables ping
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_ping_interval(void *client, int ping_interval_ms);

/**
 * @brief Get WebSocket reconnect timeout
 *
 * Returns the current base reconnect backoff interval.
 *
 * @param[in] client WebSocket client handle
 * @return Reconnect timeout in milliseconds, -1 if auto reconnect is disabled or on error
 */
int qm_websocket_client_get_reconnect_timeout(void *client);

/**
 * @brief Set WebSocket reconnect timeout
 *
 * Updates the base reconnect backoff interval and resets the reconnect counter.
 *
 * @param[in] client WebSocket client handle
 * @param[in] reconnect_interval_ms Base reconnect backoff interval in milliseconds
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_reconnect_timeout(void *client, int reconnect_interval_ms);

/**
 * @brief Get global WebSocket client log categories.
 *
 * @return Bitmask of qm_websocket_log_flags_t
 */

/**
 * @brief Set runtime WebSocket client log categories.
 *
 * This controls logs emitted by every qm_websocket_client instance and its
 * built-in TCP/TLS transports. It does not change the global QUMI log level or
 * other components.
 *
 * @param[in] log_flags Bitmask of qm_websocket_log_flags_t; NONE disables all
 *                      WebSocket client component logs
 * @return QM_EOK on success, or -QM_EINVAL on invalid flags
 */

/**
 * @brief Get connection state of WebSocket client
 *
 * This is a convenience wrapper for checking whether the current state is
 * WEBSOCKET_STATE_CONNECTED.
 *
 * @param[in] client WebSocket client handle
 * @return QM_TRUE if connected, QM_FALSE otherwise
 */
qm_bool qm_websocket_client_is_connected(void *client);

/**
 * @brief Get WebSocket client state
 *
 * @param[in] client WebSocket client handle
 * @return Current state, or WEBSOCKET_STATE_CLOSED when client is NULL
 */
qm_websocket_state_t qm_websocket_client_get_state(void *client);

/**
 * @brief Set WebSocket client user context
 *
 * Updates the client-level user context reported in event data. This does not
 * change per-handler context supplied to qm_websocket_register_events().
 * The update is synchronized with event dispatch.
 *
 * @param[in] client WebSocket client handle
 * @param[in] user_context User context
 * @return QM_EOK on success
 * @return error code on error
 */
int qm_websocket_client_set_user_context(void *client, void *user_context);

/**
 * @brief Get WebSocket client user context
 *
 * @param[in] client WebSocket client handle
 * @return User context, or NULL if client is NULL or no context was set
 */
void *qm_websocket_client_get_user_context(void *client);

#ifdef __cplusplus
}
#endif

#endif /* _QM_WEBSOCKET_CLIENT_H_ */
