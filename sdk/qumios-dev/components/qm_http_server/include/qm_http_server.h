#ifndef QM_HTTP_SERVER_H
#define QM_HTTP_SERVER_H

#include "qm_errno.h"
#include "qm_config.h"
#include "qm_types.h"
#include "qm_http_server_log.h"

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
    QM_HTTP_ANY,       /*!< Match any method */
} qm_http_method_t;
#endif

/**
 * @brief HTTP server transport scheme.
 */
typedef enum {
    QM_HTTP_SERVER_SCHEME_HTTP = 0,   /*!< Plain HTTP over qumi TCP transport */
    QM_HTTP_SERVER_SCHEME_HTTPS,      /*!< HTTPS over qumi TLS transport */
} qm_http_server_scheme_t;

/**
 * @brief HTTP request object.
 */
typedef struct qm_http_request qm_http_request_t;

/**
 * @brief HTTP response object.
 */
typedef struct qm_http_response qm_http_response_t;

/**
 * @brief Opaque HTTP server handle.
 */
typedef struct qm_http_server *qm_http_server_t;

/**
 * @brief HTTP route handler callback.
 *
 * @param[in] req Parsed HTTP request object
 * @param[in] resp HTTP response object for the same connection
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
typedef int (*qm_http_handler_t)(qm_http_request_t *req, qm_http_response_t *resp);

/**
 * @brief HTTP error handler callback.
 *
 * @param[in] req HTTP request object
 * @param[in] resp HTTP response object for sending the error response
 * @param[in] status_code HTTP status code, such as 404, 405, 413, or 401
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
typedef int (*qm_http_error_handler_t)(qm_http_request_t *req, qm_http_response_t *resp, int status_code);

/**
 * @brief HTTP route entry.
 */
typedef struct {
    qm_http_method_t method;      /*!< HTTP method to match */
    const char *path;             /*!< Absolute route path, supports slash-star and ?* suffix wildcard matching */
    qm_http_handler_t handler;    /*!< Route callback */
} qm_http_route_t;

/**
 * @brief HTTP server configuration.
 *
 * Zero-valued optional numeric fields use backend defaults unless otherwise
 * noted. Request header and URI length limits are enforced by max_header_len.
 * The route table, HTTPS certificate, HTTPS private key, and client CA
 * certificate are referenced directly and must remain valid for the lifetime
 * of the server handle.
 */
typedef struct {
    uint16_t port;                        /*!< Listen port, must be non-zero */
    const qm_http_route_t *routes;       /*!< Route table, required when route_count > 0 */
    int route_count;                     /*!< Number of entries in routes */
    qm_http_error_handler_t error_handler; /*!< Optional error response callback */
    uint32_t timeout_ms;                 /*!< Receive/send timeout in milliseconds */
    uint32_t max_header_len;             /*!< Maximum request header bytes, 0 uses component default */
    uint32_t max_body_len;               /*!< Maximum request body length, 0 uses component default */
    int task_stack;                      /*!< Server task stack size */
    int task_prio;                       /*!< Server task priority */
    qm_bool keep_alive_enable;           /*!< Enable HTTP keep-alive */
    qm_http_server_scheme_t scheme;       /*!< Transport scheme, default HTTP when zero-initialized */
    const uint8_t *https_cert_pem;        /*!< HTTPS server certificate in PEM/DER buffer */
    size_t https_cert_len;             /*!< HTTPS server certificate buffer length */
    const uint8_t *https_key_pem;         /*!< HTTPS private key in PEM/DER buffer */
    qm_size_t https_key_len;              /*!< HTTPS private key buffer length */
    const uint8_t *https_client_ca_pem;   /*!< Optional client CA/client cert for mTLS verification */
    qm_size_t https_client_ca_len;        /*!< Optional client CA/client cert buffer length */
} qm_http_server_config_t;

/**
 * @brief Parsed HTTP request metadata.
 *
 * String pointers are owned by the component and remain valid only while the
 * route handler is running.
 */
struct qm_http_request {
    qm_http_method_t method;     /*!< Request method */
    const char *path;            /*!< Request path without query string */
    const char *query;           /*!< Query string without leading '?', or NULL */
    const char *uri;             /*!< Original request URI */
    int content_length;          /*!< Request Content-Length value, or 0 */
};

/**
 * @brief Create an HTTP server handle.
 *
 * The returned handle must be started with qm_http_server_start() and released
 * with qm_http_server_destroy().
 *
 * @param[in] config HTTP server configuration
 * @return HTTP server handle on success
 * @return NULL on invalid configuration or allocation failure
 */
qm_http_server_t qm_http_server_create(const qm_http_server_config_t *config);

/**
 * @brief Start listening and register all configured routes.
 *
 * @param[in] server HTTP server handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_server_start(qm_http_server_t server);

/**
 * @brief Stop the HTTP server if it is running.
 *
 * @param[in] server HTTP server handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_server_stop(qm_http_server_t server);

/**
 * @brief Stop and destroy an HTTP server handle.
 *
 * @param[in] server HTTP server handle
 */
void qm_http_server_destroy(qm_http_server_t server);

/**
 * @brief Extract one query parameter value.
 *
 * @param[in] query Query string without leading '?'
 * @param[in] name Parameter name to search for
 * @param[out] value Destination buffer for the decoded value
 * @param[in] value_len Size of value in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_query_get(const char *query, const char *name, char *value, int value_len);

/**
 * @brief Read one request header value by name.
 *
 * @param[in] req HTTP request object
 * @param[in] name Header name to search for
 * @param[out] value Destination buffer for the header value
 * @param[in] value_len Size of value in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_request_get_header(qm_http_request_t *req, const char *name, char *value, int value_len);

/**
 * @brief Read request body bytes.
 *
 * @param[in] req HTTP request object
 * @param[out] buf Destination buffer
 * @param[in] buf_len Maximum bytes to read
 * @return Positive bytes read on success
 * @return 0 when no body bytes remain or buf_len is 0
 * @return Negative error code on failure
 */
int qm_http_request_read(qm_http_request_t *req, char *buf, int buf_len);

/**
 * @brief Read the full request body into a newly allocated buffer.
 *
 * The returned buffer is NUL-terminated for convenience even when the payload
 * is binary. The caller owns the returned buffer and must release it with
 * qm_free().
 *
 * @param[in] req HTTP request object
 * @param[out] body Allocated body buffer output
 * @param[out] body_len Body size in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_request_read_all(qm_http_request_t *req, char **body, int *body_len);

/**
 * @brief Send a complete non-chunked HTTP response.
 *
 * Passing NULL body sends an empty response body. Passing NULL content_type
 * uses "text/plain".
 *
 * @param[in] resp HTTP response object
 * @param[in] status_code HTTP status code
 * @param[in] content_type Response Content-Type, or NULL
 * @param[in] body Response body buffer, or NULL
 * @param[in] body_len Response body length in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_response_send(qm_http_response_t *resp, int status_code,
                          const char *content_type, const char *body, int body_len);

/**
 * @brief Add one HTTP response header.
 *
 * @param[in] resp HTTP response object
 * @param[in] name Header name
 * @param[in] value Header value
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_response_set_header(qm_http_response_t *resp, const char *name, const char *value);

/**
 * @brief Begin a chunked HTTP response.
 *
 * Passing NULL content_type uses "application/octet-stream".
 *
 * @param[in] resp HTTP response object
 * @param[in] status_code HTTP status code
 * @param[in] content_type Response Content-Type, or NULL
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_response_begin_chunked(qm_http_response_t *resp, int status_code,
                                   const char *content_type);

/**
 * @brief Send one response chunk.
 *
 * @param[in] resp HTTP response object
 * @param[in] data Chunk payload, or NULL when len is 0
 * @param[in] len Chunk length in bytes
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_response_write_chunk(qm_http_response_t *resp, const char *data, int len);

/**
 * @brief Finish a chunked HTTP response.
 *
 * @param[in] resp HTTP response object
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_response_end_chunked(qm_http_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif
