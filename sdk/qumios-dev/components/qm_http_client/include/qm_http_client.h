#ifndef QM_HTTP_CLIENT_H
#define QM_HTTP_CLIENT_H

#include "qm_errno.h"
#include "qm_config.h"
#include "qm_types.h"
#include "qm_http_client_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef QM_HTTP_METHOD_T_DEFINED
#define QM_HTTP_METHOD_T_DEFINED
/**
 * @brief HTTP request method.
 */
typedef enum {
    QM_HTTP_GET = 0,   /*!< HTTP GET */
    QM_HTTP_POST,      /*!< HTTP POST */
    QM_HTTP_PUT,       /*!< HTTP PUT */
    QM_HTTP_DELETE,    /*!< HTTP DELETE */
    QM_HTTP_HEAD,      /*!< HTTP HEAD */
    QM_HTTP_ANY,       /*!< Server-side route wildcard; invalid for client requests */
} qm_http_method_t;
#endif

/**
 * @brief TLS configuration for HTTPS requests.
 *
 * The structure is copied by qm_http_client_init() and qm_http_client_execute(),
 * but certificate and key buffers are referenced directly. Keep those buffers
 * valid while the persistent client exists or the one-shot request is running.
 */
typedef struct {
    const char *ca_crt;        /*!< Server CA certificate buffer, or NULL to use transport defaults */
    int ca_crt_len;            /*!< CA certificate length in bytes */
    const char *client_crt;    /*!< Client certificate buffer for mutual TLS, or NULL */
    int client_crt_len;        /*!< Client certificate length in bytes */
    const char *client_key;    /*!< Client private key buffer for mutual TLS, or NULL */
    int client_key_len;        /*!< Client private key length in bytes */
} qm_http_tls_config_t;

/**
 * @brief HTTP header name/value pair.
 */
typedef struct {
    const char *name;     /*!< Header name, for example "Content-Type" */
    const char *value;    /*!< Header value */
} qm_http_header_t;

/**
 * @brief Opaque HTTP client session handle.
 */
typedef struct qm_http_client *qm_http_client_handle_t;

/**
 * @brief HTTP client event id.
 *
 * Event callbacks provide finer-grained request lifecycle notifications than
 * the legacy body-only callback.
 */
typedef enum {
    QM_HTTP_CLIENT_EVENT_CONNECTED = 0,     /*!< Transport connection is established */
    QM_HTTP_CLIENT_EVENT_HEADER,            /*!< One response header line has been parsed */
    QM_HTTP_CLIENT_EVENT_DATA,              /*!< Response body bytes are available */
    QM_HTTP_CLIENT_EVENT_FINISH,            /*!< Response body has been fully received */
    QM_HTTP_CLIENT_EVENT_DISCONNECTED,      /*!< Transport connection is closed */
    QM_HTTP_CLIENT_EVENT_ERROR,             /*!< Request processing failed */
    QM_HTTP_CLIENT_EVENT_REDIRECT,          /*!< Redirect response with Location was received */
} qm_http_client_event_id_t;

/**
 * @brief HTTP client event data.
 *
 * The event object and any pointed strings or buffers are owned by the
 * component and are valid only during the callback. Copy them in the callback
 * if they must be used later.
 */
typedef struct {
    qm_http_client_event_id_t event_id;     /*!< Event type */
    qm_http_client_handle_t client;         /*!< Client handle that generated the event */
    void *user;                             /*!< User context stored in the client handle */
    const char *data;                       /*!< DATA payload, or raw header line for HEADER */
    int data_len;                           /*!< Length of data in bytes */
    const char *header_name;                /*!< Header name for HEADER events */
    const char *header_value;               /*!< Header value for HEADER events */
    int status_code;                        /*!< Current response status code when available */
    int error_code;                         /*!< Negative error code for ERROR events */
    const char *redirect_url;               /*!< Location value for REDIRECT events */
} qm_http_client_event_t;

/**
 * @brief Legacy response body callback.
 *
 * This callback is kept for compatibility with existing streaming users.
 * New code should prefer qm_http_client_event_cb_t for lifecycle-aware
 * processing.
 *
 * @param[in] data Body chunk buffer
 * @param[in] len Body chunk length in bytes
 * @param[in] user User context
 * @return QM_EOK to continue receiving
 * @return Other value to abort the transfer
 */
typedef int (*qm_http_body_cb_t)(const char *data, int len, void *user);

/**
 * @brief Request body provider callback type.
 *
 * The callback is used by qm_http_client_execute() to stream a request body
 * from a user source without holding the entire payload in memory. The total
 * body length must still be supplied in body_len because chunked request-body
 * upload is not supported by this component.
 *
 * @param[out] buffer Destination buffer to fill with the next request body chunk
 * @param[in] max_len Maximum number of bytes that may be written to buffer
 * @param[in] user User context
 * @return Positive number of bytes produced
 * @return 0 when the source reached EOF before body_len bytes were produced
 * @return Negative error code on failure
 */
typedef int (*qm_http_request_body_cb_t)(char *buffer, int max_len, void *user);

/**
 * @brief Request body reset callback type.
 *
 * The callback is used when qm_http_client_execute() must replay the request
 * body because of automatic redirect handling or Digest authentication retry.
 *
 * @param[in] user User context
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
typedef int (*qm_http_request_body_reset_cb_t)(void *user);

/**
 * @brief HTTP client event callback type.
 *
 * @param[in] event Event data, valid only during the callback
 * @return QM_EOK to continue processing
 * @return Other value to abort the request
 */
typedef int (*qm_http_client_event_cb_t)(const qm_http_client_event_t *event);

/**
 * @brief Query caller-owned cancellation state. / 查询调用方持有的取消状态。
 * Return QM_TRUE to interrupt the request with -QM_EINTR. Runs on the HTTP
 * caller before and after transport I/O; must be nonblocking and thread-safe.
 * Keep the latch set until the request returns. This cannot interrupt a HAL
 * connection call already in progress: DNS, connect and TLS handshake bounds
 * remain the transport implementation's contract. / 返回 QM_TRUE 时以 -QM_EINTR
 * 中断请求。由 HTTP 调用任务在传输 I/O 前后调用，必须非阻塞且线程安全；请求返回前
 * 保持取消状态。不能打断已进入的 HAL 建连；DNS、连接及 TLS 握手的时限由传输实现保证。
 */
typedef qm_bool (*qm_http_client_interrupt_fn)(void *user);

/**
 * @brief HTTP authentication mode.
 */
typedef enum {
    QM_HTTP_AUTH_TYPE_NONE = 0,    /*!< No Authorization header is generated */
    QM_HTTP_AUTH_TYPE_BASIC,       /*!< HTTP Basic authentication */
    QM_HTTP_AUTH_TYPE_DIGEST,      /*!< HTTP Digest authentication: MD5, MD5-sess, SHA-256, SHA-256-sess */
} qm_http_client_auth_type_t;

/**
 * @brief Persistent HTTP client configuration.
 *
 * String fields and header arrays are copied during qm_http_client_init().
 * Zero-valued timeout and size fields use component defaults. The user and tls
 * pointers are stored as-is.
 */
typedef struct {
    qm_http_method_t method;                  /*!< Initial request method */
    const char *url;                          /*!< Initial request URL, supports http:// and https://, bracketed IPv6 hosts, and query-only request targets such as http://host?x=1 */
    const char *username;                     /*!< Username for Basic or Digest authentication */
    const char *password;                     /*!< Password for Basic or Digest authentication */
    qm_http_client_auth_type_t auth_type;     /*!< Authentication mode */
    const char *content_type;                 /*!< Content-Type used when a request body is sent */
    const char *user_agent;                   /*!< User-Agent header, or NULL for the default */
    const qm_http_header_t *headers;          /*!< Extra request headers copied during init */
    int header_count;                         /*!< Number of entries in headers */
    uint32_t timeout_ms;                      /*!< Per-transfer read/write timeout; HAL owns connection bounds. / 单次收发时限；建连时限归 HAL。 */
    uint32_t max_header_len;                  /*!< Maximum response header length accepted */
    uint32_t max_body_len;                    /*!< Maximum response body length accepted */
    uint32_t rx_buf_size;                     /*!< Per-read transfer buffer size, 0 uses CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE */
    qm_bool disable_auto_redirect;            /*!< Disable automatic 30x redirect handling when QM_TRUE */
    int max_redirection_count;                /*!< Maximum automatic redirect count, 0 uses component default */
    qm_bool keep_alive;                       /*!< Request connection reuse when the server allows it */
    const qm_http_tls_config_t *tls;          /*!< HTTPS TLS configuration, or NULL */
    void *user;                               /*!< User context returned by qm_http_client_get_user_data() */
    qm_http_client_event_cb_t on_event;       /*!< Optional event callback */
    qm_http_client_interrupt_fn interrupted; /*!< Optional cancellation predicate. / 可选取消查询。 */
} qm_http_client_config_t;

/**
 * @brief One-shot request configuration.
 *
 * qm_http_client_execute() creates an internal client handle, performs the
 * request, and cleans the handle up before returning. When both on_event and
 * on_body are set, QM_HTTP_CLIENT_EVENT_DATA is dispatched before on_body for
 * each received chunk.
 *
 * Request body streaming rules:
 * - body and body_cb are mutually exclusive
 * - body_len is always the total request body length
 * - body_cb is called repeatedly until body_len bytes are uploaded
 * - body_reset is used only when the request must be replayed
 */
typedef struct {
    qm_http_method_t method;                  /*!< Request method */
    const char *url;                          /*!< Request URL, supports http:// and https://, bracketed IPv6 hosts, and query-only request targets such as http://host?x=1 */
    const char *username;                     /*!< Username for Basic or Digest authentication */
    const char *password;                     /*!< Password for Basic or Digest authentication */
    qm_http_client_auth_type_t auth_type;     /*!< Authentication mode */
    const char *body;                         /*!< Request body buffer, or NULL when body_len is 0 */
    int body_len;                             /*!< Request body length in bytes */
    qm_http_request_body_cb_t body_cb;        /*!< Optional streaming request body provider callback */
    qm_http_request_body_reset_cb_t body_reset; /*!< Optional reset callback for request body replay */
    const char *content_type;                 /*!< Content-Type used when body_len is greater than 0 */
    const char *user_agent;                   /*!< User-Agent header, or NULL for the default */
    const qm_http_header_t *headers;          /*!< Extra request headers */
    int header_count;                         /*!< Number of entries in headers */
    uint32_t timeout_ms;                      /*!< Per-transfer read/write timeout; HAL owns connection bounds. / 单次收发时限；建连时限归 HAL。 */
    uint32_t max_header_len;                  /*!< Maximum response header length accepted */
    uint32_t max_body_len;                    /*!< Maximum response body length accepted */
    uint32_t rx_buf_size;                     /*!< Per-read transfer buffer size, 0 uses CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE */
    qm_bool disable_auto_redirect;            /*!< Disable automatic 30x redirect handling when QM_TRUE */
    int max_redirection_count;                /*!< Maximum automatic redirect count, 0 uses component default */
    qm_bool keep_alive;                       /*!< Reserved; execute always closes the connection */
    const qm_http_tls_config_t *tls;          /*!< HTTPS TLS configuration, or NULL */
    void *user;                               /*!< User context passed to callbacks */
    qm_http_body_cb_t on_body;                /*!< Optional legacy response body callback */
    qm_http_client_event_cb_t on_event;       /*!< Optional response event callback */
    qm_http_client_interrupt_fn interrupted; /*!< Optional cancellation predicate. / 可选取消查询。 */
} qm_http_client_request_t;

/**
 * @brief HTTP response metadata.
 */
typedef struct {
    int status_code;          /*!< HTTP status code */
    qm_bool content_length_known; /*!< QM_TRUE when Content-Length was present */
    uint32_t content_length;  /*!< Content-Length value when content_length_known is QM_TRUE */
    qm_bool chunked;          /*!< Transfer-Encoding contains chunked */
    qm_bool keep_alive;       /*!< Actual connection reuse state; execute always returns QM_FALSE */
    const char *location;     /*!< Redirect Location header, valid until the client handle is cleaned up */
} qm_http_client_response_t;

/**
 * @brief Create a persistent HTTP client handle.
 *
 * @param[in] config Client configuration, or NULL to use defaults and configure later
 * @return HTTP client handle on success
 * @return NULL on allocation or configuration copy failure
 */
qm_http_client_handle_t qm_http_client_init(const qm_http_client_config_t *config);

/**
 * @brief Get global qm_http_client log categories.
 *
 * @return Active component log mask.
 */

/**
 * @brief Set global qm_http_client log categories.
 *
 * This only controls logs emitted by qm_http_client and does not modify the
 * logging configuration of TCP, TLS, or other components.
 *
 * @param[in] log_flags Bitmask of qm_http_client_log_flags_t.
 * @return QM_EOK on success, or -QM_EINVAL on invalid flags.
 */

/**
 * @brief Set or replace the request URL.
 *
 * The URL is parsed and validated before it is stored. Changing the URL clears
 * any cached Digest authentication challenge; if validation fails, the previous
 * URL and Digest state are kept.
 * Userinfo in the authority component is rejected. Oversized host or path
 * fields fail with -QM_EFULL.
 *
 * @param[in] client HTTP client handle
 * @param[in] url Request URL
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_url(qm_http_client_handle_t client, const char *url);

/**
 * @brief Set the request method.
 *
 * @param[in] client HTTP client handle
 * @param[in] method Request method
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_method(qm_http_client_handle_t client, qm_http_method_t method);

/**
 * @brief Set or clear the authentication username.
 *
 * @param[in] client HTTP client handle
 * @param[in] username Username string, or NULL to clear
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_username(qm_http_client_handle_t client, const char *username);

/**
 * @brief Set or clear the authentication password.
 *
 * @param[in] client HTTP client handle
 * @param[in] password Password string, or NULL to clear
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_password(qm_http_client_handle_t client, const char *password);

/**
 * @brief Set the authentication mode.
 *
 * Changing the mode clears any cached Digest authentication challenge.
 *
 * @param[in] client HTTP client handle
 * @param[in] auth_type Authentication mode
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_auth_type(qm_http_client_handle_t client, qm_http_client_auth_type_t auth_type);

/**
 * @brief Add or replace a request header.
 *
 * Host, Connection and Content-Length are managed by the component and are
 * rejected by this API.
 *
 * @param[in] client HTTP client handle
 * @param[in] name Header name
 * @param[in] value Header value
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_header(qm_http_client_handle_t client, const char *name, const char *value);

/**
 * @brief Delete a request header by name.
 *
 * Host, Connection and Content-Length are managed by the component and cannot
 * be deleted through this API.
 *
 * @param[in] client HTTP client handle
 * @param[in] name Header name to delete
 * @return QM_EOK on success
 * @return -QM_ENOENT if the header does not exist
 * @return Negative error code on failure
 */
int qm_http_client_delete_header(qm_http_client_handle_t client, const char *name);

/**
 * @brief Open the connection and send the request line and headers.
 *
 * If keep-alive is enabled and the current connection matches the target
 * endpoint, the existing transport may be reused.
 *
 * @param[in] client HTTP client handle
 * @param[in] write_len Request body length in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_open(qm_http_client_handle_t client, int write_len);

/**
 * @brief Write request body bytes after qm_http_client_open().
 *
 * @param[in] client HTTP client handle
 * @param[in] buffer Body bytes to write
 * @param[in] len Number of bytes to write
 * @return Positive bytes written on success
 * @return Negative error code on failure
 */
int qm_http_client_write(qm_http_client_handle_t client, const char *buffer, int len);

/**
 * @brief Fetch and parse response headers.
 *
 * HEADER and REDIRECT events are dispatched from this call when an event
 * callback is configured.
 *
 * @param[in] client HTTP client handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_fetch_headers(qm_http_client_handle_t client);

/**
 * @brief Read response body data.
 *
 * DATA and FINISH events are dispatched from this call when an event callback
 * is configured.
 *
 * @param[in] client HTTP client handle
 * @param[out] buffer Destination buffer
 * @param[in] len Maximum number of bytes to read
 * @return Positive bytes read on success
 * @return 0 when the response body has been fully received
 * @return Negative error code on failure
 */
int qm_http_client_read(qm_http_client_handle_t client, char *buffer, int len);

/**
 * @brief Discard unread response body bytes.
 *
 * The function drains the remaining response body for the current response
 * without dispatching DATA or FINISH events for the discarded bytes. If
 * response headers have not been fetched yet, they are fetched first, so
 * HEADER / REDIRECT events may still be dispatched from that step.
 *
 * This helper is useful when the caller no longer needs the response payload
 * but still wants to consume it so a keep-alive connection can be reused when
 * the server allows it.
 *
 * @param[in] client HTTP client handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_drain_response_body(qm_http_client_handle_t client);

/**
 * @brief Perform a full request on an initialized handle.
 *
 * This helper covers open, optional write, header fetch, body streaming,
 * automatic redirect handling and Digest authentication retry.
 *
 * @param[in] client HTTP client handle
 * @param[in] body Request body buffer, or NULL when body_len is 0
 * @param[in] body_len Request body length in bytes
 * @param[in] on_body Optional legacy body callback
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_perform(qm_http_client_handle_t client, const char *body, int body_len,
                           qm_http_body_cb_t on_body);

/**
 * @brief Close the current connection.
 *
 * The client handle remains valid and can be reused for another request.
 *
 * @param[in] client HTTP client handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_close(qm_http_client_handle_t client);

/**
 * @brief Close and release an HTTP client handle.
 *
 * @param[in] client HTTP client handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_cleanup(qm_http_client_handle_t client);

/**
 * @brief Get the last parsed response status code.
 *
 * @param[in] client HTTP client handle
 * @return HTTP status code, or 0 when unavailable
 */
int qm_http_client_get_status_code(qm_http_client_handle_t client);

/**
 * @brief Get the last parsed Content-Length value.
 *
 * @param[in] client HTTP client handle
 * @return Content-Length value
 * @return 0 when unavailable or unknown
 */
uint32_t qm_http_client_get_content_length(qm_http_client_handle_t client);

/**
 * @brief Check whether the last response carried a Content-Length header.
 *
 * @param[in] client HTTP client handle
 * @return QM_TRUE when Content-Length is known
 * @return QM_FALSE when unavailable or unknown
 */
qm_bool qm_http_client_has_content_length(qm_http_client_handle_t client);

/**
 * @brief Get the last redirect Location value.
 *
 * The returned pointer remains valid until the next request or cleanup. In
 * qm_http_client_execute(), the internal handle is destroyed before return, so
 * callers should consume redirect_url through the event callback instead.
 *
 * @param[in] client HTTP client handle
 * @return Redirect URL pointer, or NULL when unavailable
 */
const char *qm_http_client_get_location(qm_http_client_handle_t client);

/**
 * @brief Return whether the last response used chunked transfer encoding.
 *
 * @param[in] client HTTP client handle
 * @return QM_TRUE when the last response was chunked
 * @return QM_FALSE otherwise
 */
qm_bool qm_http_client_is_chunked_response(qm_http_client_handle_t client);

/**
 * @brief Return whether the current response body has been fully read.
 *
 * @param[in] client HTTP client handle
 * @return QM_TRUE when the body is complete
 * @return QM_FALSE otherwise
 */
qm_bool qm_http_client_is_complete_data_received(qm_http_client_handle_t client);

/**
 * @brief Get user context stored in the client handle.
 *
 * @param[in] client HTTP client handle
 * @return Stored user context, or NULL when unavailable
 */
void *qm_http_client_get_user_data(qm_http_client_handle_t client);

/**
 * @brief Set user context stored in the client handle.
 *
 * @param[in] client HTTP client handle
 * @param[in] user User context pointer
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_set_user_data(qm_http_client_handle_t client, void *user);

/**
 * @brief Execute a one-shot HTTP request.
 *
 * The function creates an internal client handle, performs the request, copies
 * response metadata to resp and destroys the internal handle before returning.
 * Because of that, resp->location is always cleared before return.
 *
 * @param[in] req One-shot request configuration
 * @param[out] resp Response metadata output
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_client_execute(const qm_http_client_request_t *req,
                           qm_http_client_response_t *resp);

/**
 * @brief Initialize an HTTPS TLS configuration object.
 *
 * Set only ca_crt for server authentication, or additionally set client_crt
 * and client_key for mutual TLS.
 *
 * @param[out] cfg TLS configuration object to initialize
 * @param[in] ca_crt CA certificate buffer, or NULL
 * @param[in] ca_crt_len CA certificate length in bytes, or 0
 * @param[in] client_crt Client certificate buffer for mutual TLS, or NULL
 * @param[in] client_crt_len Client certificate length in bytes, or 0
 * @param[in] client_key Client private key buffer for mutual TLS, or NULL
 * @param[in] client_key_len Client private key length in bytes, or 0
 * @return QM_EOK on success
 * @return -QM_EINVAL when arguments are invalid
 */
int qm_http_client_tls_config_init(qm_http_tls_config_t *cfg,
                                   const char *ca_crt, int ca_crt_len,
                                   const char *client_crt, int client_crt_len,
                                   const char *client_key, int client_key_len);

#ifdef __cplusplus
}
#endif

#endif
