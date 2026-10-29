#define QM_HTTP_SERVER_INTERNAL
#include "qm_http_server_transport.h"
#undef QM_HTTP_SERVER_INTERNAL

#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_tcp.h"
#include "qm_tls.h"

#define LOG_TAG "qm_http_server_transport"

/**
 * @brief qumi-only HTTP server transport backend.
 *
 * The HTTP server works with qm_http_server_connection_t function pointers and
 * never sees the platform socket/TLS implementation directly. HTTPS is handled
 * by qm_tls_accept() here; any IDF/mbedTLS details must remain inside the
 * platform HAL that implements qm_tls.
 */
struct qm_http_server_listener {
    int socket;                         /*!< Listening socket returned by qm_tcp_server_creat() */
    qm_http_server_scheme_t scheme;      /*!< HTTP or HTTPS listener mode */
    qm_tls_server_cfg_t tls_cfg;         /*!< qumi TLS server config used by HTTPS accept */
};

/**
 * @brief Plain TCP connection handle used behind qm_http_server_connection_t.
 */
typedef struct {
    int socket; /*!< Accepted TCP socket */
} qm_http_server_tcp_handle_t;

/**
 * @brief Write data through a plain qumi TCP connection.
 */
static int qm_http_server_tcp_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    qm_http_server_tcp_handle_t *tcp = (qm_http_server_tcp_handle_t *)handle;
    int ret;

    if (!tcp || !buf || len < 0) {
        return -QM_EINVAL;
    }
    ret = qm_tcp_write(tcp->socket, buf, (uint32_t)len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Read data from a plain qumi TCP connection.
 */
static int qm_http_server_tcp_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    qm_http_server_tcp_handle_t *tcp = (qm_http_server_tcp_handle_t *)handle;
    int ret;

    if (!tcp || !buf || len <= 0) {
        return -QM_EINVAL;
    }
    ret = qm_tcp_read(tcp->socket, buf, (uint32_t)len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Close a plain qumi TCP connection and free its wrapper.
 */
static int qm_http_server_tcp_close(void *handle)
{
    qm_http_server_tcp_handle_t *tcp = (qm_http_server_tcp_handle_t *)handle;
    int ret;

    if (!tcp) {
        return -QM_EINVAL;
    }
    ret = qm_tcp_destroy(tcp->socket);
    qm_free(tcp);
    return ret < 0 ? -QM_EIO : QM_EOK;
}

/**
 * @brief Write to a TLS connection.
 *
 * TLS connections already own their accepted socket through qm_tls_accept().
 */
static int qm_http_server_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    int ret;

    if (!handle || !buf || len < 0) {
        return -QM_EINVAL;
    }
    ret = qm_tls_write(handle, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Read data from a TLS connection.
 */
static int qm_http_server_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret;

    if (!handle || !buf || len <= 0) {
        return -QM_EINVAL;
    }
    ret = qm_tls_read(handle, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Close a TLS connection.
 */
static int qm_http_server_tls_close(void *handle)
{
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }
    ret = qm_tls_destroy(handle);
    return ret < 0 ? -QM_EIO : QM_EOK;
}

int qm_http_server_transport_listener_open(const qm_http_server_config_t *config,
                                           qm_http_server_listener_t **listener)
{
    qm_http_server_listener_t *item;
    int socket;

    if (!config || !listener || !config->port) {
        return -QM_EINVAL;
    }

    *listener = NULL;
    socket = qm_tcp_server_creat(config->port);
    if (socket <= 0) {
        return -QM_EIO;
    }

    item = (qm_http_server_listener_t *)qm_calloc(1, sizeof(*item));
    if (!item) {
        qm_tcp_destroy(socket);
        return -QM_ENOMEM;
    }

    item->socket = socket;
    item->scheme = config->scheme;

    /* Translate public HTTPS config into the qumi TLS server config. */
    if (config->scheme == QM_HTTP_SERVER_SCHEME_HTTPS) {
        item->tls_cfg.server_crt = (const char *)config->https_cert_pem;
        item->tls_cfg.server_crt_len = config->https_cert_len;
        item->tls_cfg.server_key = (const char *)config->https_key_pem;
        item->tls_cfg.server_key_len = config->https_key_len;
        item->tls_cfg.client_ca = (const char *)config->https_client_ca_pem;
        item->tls_cfg.client_ca_len = config->https_client_ca_len;
        item->tls_cfg.verify_client = config->https_client_ca_pem ? QM_TRUE : QM_FALSE;
    }

    *listener = item;
    return QM_EOK;
}

int qm_http_server_transport_listener_accept(qm_http_server_listener_t *listener,
                                             qm_http_server_connection_t *connection,
                                             uint32_t timeout_ms)
{
    char ip[32] = {0};
    int socket;

    if (!listener || !connection) {
        return -QM_EINVAL;
    }

    qm_memset(connection, 0, sizeof(*connection));
    connection->sockfd = -1;

    socket = qm_tcp_accept(listener->socket, ip, (int)timeout_ms);
    if (socket <= 0) {
        return -QM_ETIMEOUT;
    }

    connection->sockfd = socket;
    qm_strncpy(connection->peer_ip, ip, sizeof(connection->peer_ip) - 1);

    if (listener->scheme == QM_HTTP_SERVER_SCHEME_HTTPS) {
        /* Server-side TLS accept is intentionally hidden behind qm_tls. */
        connection->handle = qm_tls_accept(socket, &listener->tls_cfg, timeout_ms);
        if (!connection->handle) {
            qm_tcp_destroy(socket);
            connection->sockfd = -1;
            return -QM_EIO;
        }
        connection->write = qm_http_server_tls_write;
        connection->read = qm_http_server_tls_read;
        connection->close = qm_http_server_tls_close;
    } else {
        /* Plain HTTP keeps only a small qumi TCP handle behind the vtable. */
        qm_http_server_tcp_handle_t *handle = (qm_http_server_tcp_handle_t *)qm_calloc(1, sizeof(*handle));
        if (!handle) {
            qm_tcp_destroy(socket);
            connection->sockfd = -1;
            return -QM_ENOMEM;
        }
        handle->socket = socket;
        connection->handle = handle;
        connection->write = qm_http_server_tcp_write;
        connection->read = qm_http_server_tcp_read;
        connection->close = qm_http_server_tcp_close;
    }

    return QM_EOK;
}

int qm_http_server_transport_listener_close(qm_http_server_listener_t *listener)
{
    int ret;

    if (!listener) {
        return QM_EOK;
    }

    ret = qm_tcp_destroy(listener->socket);
    qm_free(listener);
    return ret < 0 ? -QM_EIO : QM_EOK;
}

void qm_http_server_transport_connection_close(qm_http_server_connection_t *connection)
{
    if (!connection || !connection->handle) {
        return;
    }

    connection->close(connection->handle);
    qm_memset(connection, 0, sizeof(*connection));
    connection->sockfd = -1;
}
