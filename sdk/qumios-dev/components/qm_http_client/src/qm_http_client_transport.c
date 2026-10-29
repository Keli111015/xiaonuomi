#define QM_HTTP_CLIENT_INTERNAL
#include "qm_http_client_transport.h"
#include "qm_http_client_log.h"
#undef QM_HTTP_CLIENT_INTERNAL

#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_tcp.h"
#include "qm_tls.h"

#define LOG_TAG "qm_http_client_transport"

/**
 * @brief Opaque TCP transport handle wrapper.
 *
 * The HTTP client transport abstraction expects an owned pointer handle. The
 * qumi TCP HAL returns a socket integer, so this small wrapper bridges the two.
 */
typedef struct {
    int socket;
} qm_http_client_tcp_handle_t;

/**
 * @brief Connect through the qumi TCP HAL.
 */
static void *qm_http_client_tcp_connect(const char *host, uint16_t port, void *config)
{
    qm_http_client_tcp_handle_t *handle;
    int sock;

    (void)config;
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE,
                        "tcp connect start host=%s port=%u", host, port);
    sock = qm_tcp_establish(host, port);
    if (sock < 0) {
        QM_HTTP_CLIENT_LOGE(LOG_TAG, "tcp connect failed host=%s port=%u ret=%d", host, port, sock);
        return NULL;
    }

    handle = (qm_http_client_tcp_handle_t *)qm_calloc(1, sizeof(*handle));
    if (!handle) {
        qm_tcp_destroy(sock);
        return NULL;
    }
    handle->socket = sock;
    return handle;
}

/**
 * @brief Send bytes through the qumi TCP HAL.
 */
static int qm_http_client_tcp_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    qm_http_client_tcp_handle_t *tcp = (qm_http_client_tcp_handle_t *)handle;
    int ret;

    if (!tcp || !buf || len < 0) return -QM_EINVAL;
    ret = qm_tcp_write(tcp->socket, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Receive bytes through the qumi TCP HAL.
 */
static int qm_http_client_tcp_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    qm_http_client_tcp_handle_t *tcp = (qm_http_client_tcp_handle_t *)handle;
    int ret;

    if (!tcp || !buf || len <= 0) return -QM_EINVAL;
    ret = qm_tcp_read(tcp->socket, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Close a TCP transport handle.
 */
static int qm_http_client_tcp_close(void *handle)
{
    qm_http_client_tcp_handle_t *tcp = (qm_http_client_tcp_handle_t *)handle;
    int ret;
    if (!tcp) return -QM_EINVAL;
    ret = qm_tcp_destroy(tcp->socket);
    qm_free(tcp);
    return ret < 0 ? -QM_EIO : QM_EOK;
}

/**
 * @brief Connect through the qumi TLS HAL.
 *
 * This adapter maps the public `qm_http_tls_config_t` structure to the lower
 * `qm_tls_cfg_t` structure expected by `qm_tls_establish()`.
 */
static void *qm_http_client_tls_connect(const char *host, uint16_t port, void *config)
{
    const qm_http_tls_config_t *http_tls = (const qm_http_tls_config_t *)config;
    qm_tls_cfg_t tls;

    qm_memset(&tls, 0, sizeof(tls));
    if (http_tls) {
        tls.ca_crt = http_tls->ca_crt;
        tls.ca_crt_len = http_tls->ca_crt_len;
        tls.client_crt = http_tls->client_crt;
        tls.client_crt_len = http_tls->client_crt_len;
        tls.client_key = http_tls->client_key;
        tls.client_key_len = http_tls->client_key_len;
    }

    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE,
                        "tls connect start host=%s port=%u ca=%d mtls=%d",
                        host, port, http_tls && http_tls->ca_crt ? 1 : 0,
                        http_tls && http_tls->client_crt && http_tls->client_key ? 1 : 0);
    return qm_tls_establish(host, port, &tls);
}

/**
 * @brief Send bytes through the qumi TLS HAL.
 */
static int qm_http_client_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    int ret;

    if (!handle || !buf || len < 0) return -QM_EINVAL;
    ret = qm_tls_write(handle, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Receive bytes through the qumi TLS HAL.
 */
static int qm_http_client_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    int ret;

    if (!handle || !buf || len <= 0) return -QM_EINVAL;
    ret = qm_tls_read(handle, buf, len, timeout_ms);
    return ret < 0 ? -QM_EIO : ret;
}

/**
 * @brief Close a TLS transport handle.
 */
static int qm_http_client_tls_close(void *handle)
{
    int ret;

    if (!handle) return -QM_EINVAL;
    ret = qm_tls_destroy(handle);
    return ret < 0 ? -QM_EIO : QM_EOK;
}

void qm_http_client_transport_tcp_init(qm_http_client_transport_t *transport)
{
    if (!transport) return;
    qm_memset(transport, 0, sizeof(*transport));
    transport->connect = qm_http_client_tcp_connect;
    transport->write = qm_http_client_tcp_write;
    transport->read = qm_http_client_tcp_read;
    transport->close = qm_http_client_tcp_close;
}

void qm_http_client_transport_tls_init(qm_http_client_transport_t *transport, const qm_http_tls_config_t *tls)
{
    if (!transport) return;
    qm_memset(transport, 0, sizeof(*transport));
    transport->connect = qm_http_client_tls_connect;
    transport->write = qm_http_client_tls_write;
    transport->read = qm_http_client_tls_read;
    transport->close = qm_http_client_tls_close;
    transport->config = (void *)tls;
}
