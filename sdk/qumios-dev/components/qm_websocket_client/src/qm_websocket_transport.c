/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_tcp.h"
#include "qm_tls.h"
#define QM_WEBSOCKET_CLIENT_INTERNAL
#include "qm_websocket_transport.h"
#undef QM_WEBSOCKET_CLIENT_INTERNAL
#include "qm_websocket_client_log.h"

static __attribute__((unused)) const char *TAG = "qm_websocket_transport";

/**
 * @brief Internal TCP transport adapter object.
 *
 * This structure only extends the generic WebSocket transport vtable and does
 * not need extra per-transport state beyond the owned base object.
 */
typedef struct {
    qm_websocket_transport_t base;
} qm_websocket_transport_tcp_t;

typedef struct {
    int socket;
} qm_websocket_tcp_handle_t;

/**
 * @brief Internal TLS transport adapter object.
 *
 * In addition to the generic WebSocket transport vtable, this adapter stores a
 * translated `qm_tls_cfg_t` instance used when the secure transport connects.
 */
typedef struct {
    qm_websocket_transport_t base;
    qm_tls_cfg_t tls_config;
} qm_websocket_transport_tls_t;

typedef struct {
    void *tls_handle;
} qm_websocket_tls_handle_t;

/**
 * @brief Connect through the qumi TCP HAL.
 *
 * The WebSocket transport layer uses opaque pointer handles, while the qumi TCP
 * HAL returns an integer socket. This adapter allocates a tiny wrapper object so
 * the upper layer can manage TCP connections through the same generic contract
 * used by the TLS transport.
 *
 * @param[in] host Server host name or IP address
 * @param[in] port Server port
 * @param[in] config Internal TCP transport object
 * @return Transport handle on success, or NULL on failure
 */
static void *qm_websocket_tcp_connect(const char *host, uint16_t port, void *config)
{
    QM_UNUSED_ARG(config);

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TCP connect start: %s:%u", host, port);
    int sock = qm_tcp_establish(host, port);
    if (sock < 0) {
        QM_WEBSOCKET_LOGE(TAG, "TCP connect failed: %s:%u ret=%d", host, port, sock);
        return NULL;
    }

    qm_websocket_tcp_handle_t *handle = qm_malloc(sizeof(qm_websocket_tcp_handle_t));
    if (!handle) {
        QM_WEBSOCKET_LOGE(TAG, "TCP handle allocation failed: socket=%d", sock);
        qm_tcp_destroy(sock);
        return NULL;
    }
    handle->socket = sock;
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TCP connected: %s:%u socket=%d", host, port, sock);
    return handle;
}

/**
 * @brief TCP transport write function
 *
 * @param handle Transport handle
 * @param buf Data buffer to write
 * @param len Data length
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes written on success, -1 on error
 */
static int qm_websocket_tcp_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    qm_websocket_tcp_handle_t *tcp_handle = (qm_websocket_tcp_handle_t *)handle;
    int ret;
    if (!tcp_handle) {
        return -QM_EINVAL;
    }
    ret = qm_tcp_write(tcp_handle->socket, buf, len, timeout_ms);
    QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_TX, "TCP TX: socket=%d len=%d ret=%d timeout=%ums",
                 tcp_handle->socket, len, ret, timeout_ms);
    return ret;
}

/**
 * @brief TCP transport read function
 *
 * @param handle Transport handle
 * @param buf Buffer to store read data
 * @param len Maximum data length to read
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read on success, -1 on error
 */
static int qm_websocket_tcp_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    qm_websocket_tcp_handle_t *tcp_handle = (qm_websocket_tcp_handle_t *)handle;
    int ret;
    if (!tcp_handle) {
        return -QM_EINVAL;
    }
    ret = qm_tcp_read(tcp_handle->socket, buf, len, timeout_ms);
    QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_RX, "TCP RX: socket=%d len=%d ret=%d timeout=%ums",
                 tcp_handle->socket, len, ret, timeout_ms);
    return ret;
}

/**
 * @brief TCP transport close function
 *
 * @param handle Transport handle
 * @return QM_EOK on success, error code on error
 */
static int qm_websocket_tcp_close(void *handle)
{
    qm_websocket_tcp_handle_t *tcp_handle = (qm_websocket_tcp_handle_t *)handle;
    int ret;

    if (!tcp_handle) {
        return -QM_EINVAL;
    }

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TCP close: socket=%d", tcp_handle->socket);
    ret = qm_tcp_destroy(tcp_handle->socket);
    if (ret != QM_EOK) {
        QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_ERROR, "TCP close returned: ret=%d", ret);
    }
    qm_free(tcp_handle);
    return ret;
}

/**
 * @brief Connect through the qumi TLS HAL.
 *
 * The `config` pointer references the internal TLS transport object, which owns
 * the translated `qm_tls_cfg_t` used to establish the secure connection.
 *
 * @param[in] host Server host name or IP address
 * @param[in] port Server port
 * @param[in] config Transport configuration pointer
 * @return Transport handle on success, or NULL on failure
 */
static void *qm_websocket_tls_connect(const char *host, uint16_t port, void *config)
{
    qm_websocket_transport_tls_t *transport = (qm_websocket_transport_tls_t *)config;
    qm_tls_cfg_t *tls_config = transport ? &transport->tls_config : NULL;
    qm_websocket_tls_handle_t *handle;

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TLS connect start: %s:%u ca=%d client_cert=%d", host, port,
            tls_config && tls_config->ca_crt ? 1 : 0,
            tls_config && tls_config->client_crt && tls_config->client_key ? 1 : 0);
    void *tls_handle = qm_tls_establish(host, port, tls_config);
    if (!tls_handle) {
        QM_WEBSOCKET_LOGE(TAG, "TLS connect failed: %s:%u", host, port);
        return NULL;
    }
    handle = qm_malloc(sizeof(*handle));
    if (!handle) {
        QM_WEBSOCKET_LOGE(TAG, "TLS handle allocation failed");
        qm_tls_destroy(tls_handle);
        return NULL;
    }
    handle->tls_handle = tls_handle;
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TLS connected: %s:%u", host, port);
    return handle;
}

/**
 * @brief TLS transport write function
 *
 * @param handle Transport handle
 * @param buf Data buffer to write
 * @param len Data length
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes written on success, -1 on error
 */
static int qm_websocket_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms)
{
    qm_websocket_tls_handle_t *tls_handle = (qm_websocket_tls_handle_t *)handle;
    int ret;

    if (!tls_handle) {
        return -QM_EINVAL;
    }
    ret = qm_tls_write(tls_handle->tls_handle, buf, len, timeout_ms);
    return ret;
}

/**
 * @brief TLS transport read function
 *
 * @param handle Transport handle
 * @param buf Buffer to store read data
 * @param len Maximum data length to read
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read on success, -1 on error
 */
static int qm_websocket_tls_read(void *handle, char *buf, int len, uint32_t timeout_ms)
{
    qm_websocket_tls_handle_t *tls_handle = (qm_websocket_tls_handle_t *)handle;
    int ret;

    if (!tls_handle) {
        return -QM_EINVAL;
    }
    ret = qm_tls_read(tls_handle->tls_handle, buf, len, timeout_ms);
    return ret;
}

/**
 * @brief TLS transport close function
 *
 * @param handle Transport handle
 * @return QM_EOK on success, error code on error
 */
static int qm_websocket_tls_close(void *handle)
{
    qm_websocket_tls_handle_t *tls_handle = (qm_websocket_tls_handle_t *)handle;
    int ret;

    if (!tls_handle) {
        return -QM_EINVAL;
    }
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "TLS close");
    ret = qm_tls_destroy(tls_handle->tls_handle);
    if (ret != QM_EOK) {
        QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_ERROR, "TLS close returned: ret=%d", ret);
    }
    qm_free(tls_handle);
    return ret;
}

/**
 * @brief Create TCP transport for WebSocket client
 *
 * @return Transport handle on success, NULL on error
 */
qm_websocket_transport_t *qm_websocket_transport_tcp_create(void)
{
    qm_websocket_transport_tcp_t *tcp_transport = qm_calloc(1, sizeof(qm_websocket_transport_tcp_t));
    if (!tcp_transport) {
        return NULL;
    }

    tcp_transport->base.connect = qm_websocket_tcp_connect;
    tcp_transport->base.write = qm_websocket_tcp_write;
    tcp_transport->base.read = qm_websocket_tcp_read;
    tcp_transport->base.close = qm_websocket_tcp_close;
    tcp_transport->base.config = tcp_transport;
    return &tcp_transport->base;
}

int qm_websocket_tls_config_init(qm_websocket_tls_config_t *tls_config,
                                 const char *ca_crt, int ca_crt_len,
                                 const char *client_crt, int client_crt_len,
                                 const char *client_key, int client_key_len)
{
    if (!tls_config) {
        return QM_EINVAL;
    }
    if (ca_crt_len < 0 || client_crt_len < 0 || client_key_len < 0) {
        return QM_EINVAL;
    }
    if ((client_crt && !client_key) || (!client_crt && client_key)) {
        return QM_EINVAL;
    }
    qm_memset(tls_config, 0, sizeof(*tls_config));
    tls_config->ca_crt = ca_crt;
    tls_config->ca_crt_len = ca_crt_len;
    tls_config->client_crt = client_crt;
    tls_config->client_crt_len = client_crt_len;
    tls_config->client_key = client_key;
    tls_config->client_key_len = client_key_len;
    return QM_EOK;
}

/**
 * @brief Create TLS transport for WebSocket client
 *
 * @param tls_config TLS configuration
 * @return Transport handle on success, NULL on error
 */
qm_websocket_transport_t *qm_websocket_transport_tls_create(const qm_websocket_tls_config_t *tls_config)
{
    qm_websocket_transport_tls_t *tls_transport = qm_calloc(1, sizeof(qm_websocket_transport_tls_t));
    if (!tls_transport) {
        return NULL;
    }

    if (tls_config && ((tls_config->client_crt && !tls_config->client_key) ||
                       (!tls_config->client_crt && tls_config->client_key))) {
        qm_free(tls_transport);
        return NULL;
    }

    tls_transport->base.connect = qm_websocket_tls_connect;
    tls_transport->base.write = qm_websocket_tls_write;
    tls_transport->base.read = qm_websocket_tls_read;
    tls_transport->base.close = qm_websocket_tls_close;
    tls_transport->base.config = tls_transport;

    if (tls_config) {
        tls_transport->tls_config.ca_crt = tls_config->ca_crt;
        tls_transport->tls_config.ca_crt_len = tls_config->ca_crt_len;
        tls_transport->tls_config.client_crt = tls_config->client_crt;
        tls_transport->tls_config.client_crt_len = tls_config->client_crt_len;
        tls_transport->tls_config.client_key = tls_config->client_key;
        tls_transport->tls_config.client_key_len = tls_config->client_key_len;
    }

    return &tls_transport->base;
}

/**
 * @brief Destroy WebSocket transport
 *
 * @param transport Transport handle
 */
void qm_websocket_transport_destroy(qm_websocket_transport_t *transport)
{
    if (!transport) {
        return;
    }

    qm_free(transport);
}
