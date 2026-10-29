#ifndef QM_HTTP_SERVER_TRANSPORT_H
#define QM_HTTP_SERVER_TRANSPORT_H

#ifndef QM_HTTP_SERVER_INTERNAL
#error "qm_http_server_transport.h is internal to qm_http_server; include qm_http_server.h instead"
#endif

#include "qm_http_server.h"

/**
 * @brief Internal transport adapter for qm_http_server.
 *
 * This layer is the only bridge between the HTTP parser/response code and the
 * qumi network HAL. It must stay backend-neutral: only qumi interfaces such as
 * qm_tcp, qm_tls, qm_kernel, and qm_log may be used by the implementation.
 * Platform stacks such as IDF, mbedTLS, or lwIP belong under
 * qumi/platform/.../hal and must not leak into qm_http_server sources.
 */
typedef struct {
    void *handle;  /*!< Backend connection handle, either TCP wrapper or TLS handle */
    int sockfd;    /*!< Accepted TCP socket descriptor used for logging/session tracking */
    char peer_ip[32]; /*!< Peer IP string returned by qm_tcp_accept() */
    int (*write)(void *handle, const char *buf, int len, uint32_t timeout_ms); /*!< Backend write function */
    int (*read)(void *handle, char *buf, int len, uint32_t timeout_ms);        /*!< Backend read function */
    int (*close)(void *handle); /*!< Backend close function */
} qm_http_server_connection_t;

/**
 * @brief Opaque listener handle owned by the transport adapter.
 */
typedef struct qm_http_server_listener qm_http_server_listener_t;

/**
 * @brief Open a qumi TCP listener and prepare optional TLS server config.
 *
 * @param[in] config HTTP server configuration.
 * @param[out] listener Created listener handle.
 * @return QM_EOK on success.
 * @return Negative error code on failure.
 */
int qm_http_server_transport_listener_open(const qm_http_server_config_t *config,
                                           qm_http_server_listener_t **listener);

/**
 * @brief Accept one HTTP or HTTPS connection.
 *
 * For HTTPS listeners, this function completes the server-side TLS handshake
 * through qm_tls_accept() before returning the connection.
 *
 * @param[in] listener Listener handle created by qm_http_server_transport_listener_open().
 * @param[out] connection Accepted connection with read/write/close callbacks.
 * @param[in] timeout_ms Accept and TLS handshake timeout in milliseconds.
 * @return QM_EOK on success.
 * @return -QM_ETIMEOUT when no client is accepted before timeout.
 * @return Negative error code on failure.
 */
int qm_http_server_transport_listener_accept(qm_http_server_listener_t *listener,
                                             qm_http_server_connection_t *connection,
                                             uint32_t timeout_ms);

/**
 * @brief Close and free a transport listener.
 *
 * @param[in] listener Listener handle, may be NULL.
 * @return QM_EOK on success or when listener is NULL.
 * @return Negative error code on failure.
 */
int qm_http_server_transport_listener_close(qm_http_server_listener_t *listener);

/**
 * @brief Close one accepted connection and clear its state.
 *
 * @param[in,out] connection Connection returned by qm_http_server_transport_listener_accept().
 */
void qm_http_server_transport_connection_close(qm_http_server_connection_t *connection);

#endif
