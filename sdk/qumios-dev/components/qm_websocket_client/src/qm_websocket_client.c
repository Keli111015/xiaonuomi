/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "qm_config.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_sprintf.h"
#include "qm_string.h"
#include "qm_utils_base64.h"
#include "qm_utils_sha1.h"
#include "qm_utils_timer.h"
#include "qm_websocket_client.h"
#include "qm_websocket_client_log.h"
#define QM_WEBSOCKET_CLIENT_INTERNAL
#include "qm_websocket_transport.h"
#undef QM_WEBSOCKET_CLIENT_INTERNAL
#include "backoff_algorithm.h"

static __attribute__((unused)) const char *TAG = "qm_websocket_client";

static volatile uint32_t g_qm_websocket_client_log_flags =
    QM_WEBSOCKET_CLIENT_KCONFIG_LOG_FLAGS;

#define WS_OPCODE_CONT     0x00
#define WS_OPCODE_TEXT     0x01
#define WS_OPCODE_BINARY   0x02
#define WS_OPCODE_CLOSE    0x08
#define WS_OPCODE_PING     0x09
#define WS_OPCODE_PONG     0x0a

#define WS_FIN             0x80
#define WS_MASK            0x80
#define WS_SIZE16          126
#define WS_SIZE64          127
#define WS_MAX_INT         ((int)(~0U >> 1))

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_BUFFER_SIZE
#define CONFIG_QM_WEBSOCKET_CLIENT_BUFFER_SIZE 1024
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_TASK_STACK_SIZE
#define CONFIG_QM_WEBSOCKET_CLIENT_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_TASK_PRIORITY
#define CONFIG_QM_WEBSOCKET_CLIENT_TASK_PRIORITY 20
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_RECONNECT_INTERVAL
#define CONFIG_QM_WEBSOCKET_CLIENT_RECONNECT_INTERVAL 1000
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_KEEPALIVE_INTERVAL
#define CONFIG_QM_WEBSOCKET_CLIENT_KEEPALIVE_INTERVAL 30000
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_PING_TIMEOUT
#define CONFIG_QM_WEBSOCKET_CLIENT_PING_TIMEOUT 30000
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_PING_MAX_RETRY
#define CONFIG_QM_WEBSOCKET_CLIENT_PING_MAX_RETRY 2
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_NETWORK_TIMEOUT
#define CONFIG_QM_WEBSOCKET_CLIENT_NETWORK_TIMEOUT 10000
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_COUNT
#define CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_COUNT -1
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_SEND_QUEUE_SIZE
#define CONFIG_QM_WEBSOCKET_CLIENT_SEND_QUEUE_SIZE 10
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT
#define CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT 60000
#endif

#ifndef CONFIG_QM_WEBSOCKET_CLIENT_STOP_WAIT_MS
#define CONFIG_QM_WEBSOCKET_CLIENT_STOP_WAIT_MS 2000
#endif


#define WS_DEFAULT_RX_BUFFER       CONFIG_QM_WEBSOCKET_CLIENT_BUFFER_SIZE
#define WS_DEFAULT_TASK_STACK      CONFIG_QM_WEBSOCKET_CLIENT_TASK_STACK_SIZE
#define WS_DEFAULT_TASK_PRIORITY   CONFIG_QM_WEBSOCKET_CLIENT_TASK_PRIORITY
#define WS_DEFAULT_QUEUE_SIZE      CONFIG_QM_WEBSOCKET_CLIENT_SEND_QUEUE_SIZE
#define WS_DEFAULT_RECONNECT_MS    CONFIG_QM_WEBSOCKET_CLIENT_RECONNECT_INTERVAL
#define WS_MAX_RECONNECT_MS        CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT
#define WS_DEFAULT_PING_MS         CONFIG_QM_WEBSOCKET_CLIENT_KEEPALIVE_INTERVAL
#define WS_DEFAULT_PONG_TIMEOUT_MS CONFIG_QM_WEBSOCKET_CLIENT_PING_TIMEOUT
#define WS_DEFAULT_PING_MAX_RETRY  CONFIG_QM_WEBSOCKET_CLIENT_PING_MAX_RETRY
#define WS_DEFAULT_NETWORK_TIMEOUT_MS CONFIG_QM_WEBSOCKET_CLIENT_NETWORK_TIMEOUT
#define WS_STOP_WAIT_MS            CONFIG_QM_WEBSOCKET_CLIENT_STOP_WAIT_MS
#define WS_CLOSE_STATUS_SIZE       2
#define WS_HANDSHAKE_KEY_LEN       32
#define WS_HANDSHAKE_ACCEPT_LEN    32
#define WS_HANDSHAKE_REQUEST_SIZE  1024
#define WS_HANDSHAKE_RESPONSE_SIZE 1024
#define WS_HANDSHAKE_HOST_SIZE     256
#define WS_HANDSHAKE_SEED_SIZE     96
#define WS_CONTROL_PAYLOAD_MAX     125
#define WS_CLOSE_REASON_MAX        123

typedef struct {
    char *data;
    int len;
    int opcode;
    qm_bool fin;
} qm_websocket_message_t;

typedef struct {
    qm_websocket_client_config_t config;
    qm_websocket_tls_config_t tls_config_storage;
    qm_websocket_transport_t *transport;
    void *transport_handle;
    qm_bool transport_owned;

    qm_task_t task_handle;
    qm_queue_t send_queue;
    qm_mutex_t mutex;
    qm_mutex_t tx_mutex;
    /** @brief Queue admission state, protected by tx_mutex. */
    uint32_t tx_count;
    uint32_t tx_generation;
    qm_bool tx_open;
    qm_sem_t tx_space;
    qm_sem_t network_wakeup;
    qm_bool network_wakeup_ready;

    qm_websocket_state_t state;
    qm_bool stop_requested;
    volatile qm_websocket_network_state_t network_state;
    qm_bool reconnect_requested;
    qm_bool close_sent;
    int reconnect_count;
    backoff_algorithm_ctx_t reconnect_backoff;
    qm_utils_time_t ping_timer;
    qm_utils_time_t pong_timer;
    qm_bool ping_outstanding;
    int ping_timeout_count;
    int ping_timeout_ms;

    void *user_context;
    char *uri;
    char *host;
    char *path;
    int port;
    qm_bool is_secure;
    qm_bool headers_owned;
    uint8_t *rx_buffer;
    int rx_buffer_size;
    qm_websocket_error_info_t error_info;

    int frag_opcode;
    int tx_frag_opcode;
    qm_websocket_event_handler_t event_handlers[WEBSOCKET_EVENT_MAX];
    void *event_contexts[WEBSOCKET_EVENT_MAX];
} qm_websocket_client_t;

static void ws_free_config_strings(qm_websocket_client_t *client)
{
    if (!client) {
        return;
    }

    qm_free((void *)client->config.subprotocol);
    qm_free((void *)client->config.user_agent);
    qm_free((void *)client->config.auth);
    if (client->headers_owned) {
        qm_free((void *)client->config.headers);
    }
    client->config.subprotocol = NULL;
    client->config.user_agent = NULL;
    client->config.auth = NULL;
    client->config.headers = NULL;
    client->headers_owned = QM_FALSE;
}

static int ws_copy_config_strings(qm_websocket_client_t *client,
                                  const qm_websocket_client_config_t *config)
{
    client->config.subprotocol = NULL;
    client->config.user_agent = NULL;
    client->config.headers = NULL;
    client->config.auth = NULL;

    if (config->subprotocol) {
        client->config.subprotocol = qm_strdup(config->subprotocol);
        if (!client->config.subprotocol) {
            goto error;
        }
    }
    if (config->user_agent) {
        client->config.user_agent = qm_strdup(config->user_agent);
        if (!client->config.user_agent) {
            goto error;
        }
    }
    if (config->headers) {
        client->config.headers = qm_strdup(config->headers);
        if (!client->config.headers) {
            goto error;
        }
        client->headers_owned = QM_TRUE;
    }
    if (config->auth) {
        client->config.auth = qm_strdup(config->auth);
        if (!client->config.auth) {
            goto error;
        }
    }

    return QM_EOK;

error:
    ws_free_config_strings(client);
    return -QM_ENOMEM;
}

static void ws_set_state(qm_websocket_client_t *client, qm_websocket_state_t state)
{
    qm_websocket_message_t *message;
    unsigned int size = sizeof(message);

    /* Invalidate admission before publishing disconnect to callbacks. */
    qm_mutex_lock(&client->tx_mutex, QM_WAIT_FOREVER);
    if (state != WEBSOCKET_STATE_CONNECTED && client->tx_open) {
        client->tx_open = QM_FALSE;
        client->tx_generation++;
        client->tx_frag_opcode = 0;
        while (qm_queue_recv(&client->send_queue, &message, &size, QM_NO_WAIT) == QM_EOK) {
            qm_free(message->data);
            qm_free(message);
            client->tx_count--;
            (void)qm_sem_signal(&client->tx_space);
        }
    } else if (state == WEBSOCKET_STATE_CONNECTED) {
        client->tx_open = QM_TRUE;
    }
    qm_mutex_lock(&client->mutex, QM_WAIT_FOREVER);
    client->state = state;
    qm_mutex_unlock(&client->mutex);
    qm_mutex_unlock(&client->tx_mutex);
}

static qm_websocket_state_t ws_get_state(qm_websocket_client_t *client)
{
    qm_websocket_state_t state;

    qm_mutex_lock(&client->mutex, QM_WAIT_FOREVER);
    state = client->state;
    qm_mutex_unlock(&client->mutex);

    return state;
}

static uint16_t ws_get_backoff_base_ms(qm_websocket_client_t *client)
{
    int base_ms = client->config.reconnect_interval_ms;

    if (base_ms <= 0) {
        base_ms = WS_DEFAULT_RECONNECT_MS;
    }
    if (base_ms > WS_MAX_RECONNECT_MS) {
        base_ms = WS_MAX_RECONNECT_MS;
    }
    return (uint16_t)base_ms;
}

static void ws_reset_reconnect_backoff(qm_websocket_client_t *client)
{
    uint32_t max_attempts = client->config.max_reconnect_count > 0 ?
                            (uint32_t)client->config.max_reconnect_count :
                            BACKOFF_ALGORITHM_RETRY_FOREVER;
    int max_timeout_ms = client->config.max_reconnect_timeout_ms;

    if (max_timeout_ms <= 0) {
        max_timeout_ms = WS_MAX_RECONNECT_MS;
    }
    backoff_algorithm_init(&client->reconnect_backoff, ws_get_backoff_base_ms(client),
                           (uint16_t)max_timeout_ms, max_attempts);
}

static void ws_set_error(qm_websocket_client_t *client, qm_websocket_error_type_t type,
                         int error_code, int transport_error, int handshake_status_code)
{
    if (!client) {
        return;
    }
    qm_mutex_lock(&client->mutex, QM_WAIT_FOREVER);
    client->error_info.error_type = type;
    client->error_info.transport_error = transport_error;
    client->error_info.handshake_status_code = handshake_status_code;
    qm_mutex_unlock(&client->mutex);
    (void)error_code;
}

static qm_bool ws_transport_is_valid(const qm_websocket_transport_t *transport)
{
    return transport && transport->connect && transport->read && transport->write && transport->close;
}

static int ws_uri_is_secure(const char *uri, qm_bool *is_secure)
{
    if (!uri || !is_secure) {
        return -QM_EINVAL;
    }

    if (qm_strncmp(uri, "wss://", qm_strlen("wss://")) == 0) {
        *is_secure = QM_TRUE;
        return QM_EOK;
    }
    if (qm_strncmp(uri, "ws://", qm_strlen("ws://")) == 0) {
        *is_secure = QM_FALSE;
        return QM_EOK;
    }

    return -QM_EINVAL;
}

static int ws_parse_port(const char *start, const char *end, int *port)
{
    int value = 0;

    if (!start || !end || !port || start >= end) {
        return -QM_EINVAL;
    }

    for (const char *p = start; p < end; p++) {
        if (*p < '0' || *p > '9') {
            return -QM_EINVAL;
        }
        value = value * 10 + (*p - '0');
        if (value > 65535) {
            return -QM_EINVAL;
        }
    }
    if (value <= 0) {
        return -QM_EINVAL;
    }

    *port = value;
    return QM_EOK;
}

static qm_bool ws_uri_has_invalid_path_query_chars(const char *start)
{
    for (const char *p = start; p && *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c >= 0x7f || *p == '#') {
            return QM_TRUE;
        }
    }
    return QM_FALSE;
}

static qm_bool ws_host_needs_brackets(const char *host)
{
    return host && qm_strchr(host, ':') != NULL;
}

static void ws_dispatch(qm_websocket_client_t *client, qm_websocket_event_id_t event_id,
                        const void *data, int data_len, int opcode, qm_bool fin,
                        int payload_len, int payload_offset, int error_code)
{
    if (!client || event_id >= WEBSOCKET_EVENT_MAX) {
        return;
    }

    qm_websocket_event_data_t event_data;
    qm_websocket_event_handler_t event_handler;
    void *event_context;
    void *user_context;
    qm_websocket_error_info_t error_info;

    /* Take a snapshot under the component lock, then invoke the callback
     * outside the lock so handlers can safely re-register themselves. */
    qm_mutex_lock(&client->mutex, QM_WAIT_FOREVER);
    event_handler = client->event_handlers[event_id];
    event_context = client->event_contexts[event_id];
    user_context = client->user_context;
    error_info = client->error_info;
    qm_mutex_unlock(&client->mutex);

    if (!event_handler) {
        return;
    }

    qm_memset(&event_data, 0, sizeof(event_data));
    event_data.client = client;
    event_data.user_context = user_context;
    event_data.error_info = error_info;
    event_data.data_ptr = (const char *)data;
    event_data.data_len = data_len;
    event_data.opcode = opcode;
    event_data.fin = fin;
    event_data.payload_len = payload_len;
    event_data.payload_offset = payload_offset;
    event_data.error_code = error_code;
    event_handler(event_id, &event_data, event_context);
}

static int ws_parse_uri(qm_websocket_client_t *client, const char *uri)
{
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *query_start;
    const char *port_start;
    const char *authority_end;
    const char *uri_end;
    size_t host_len;
    size_t path_len;
    char *new_uri = NULL;
    char *new_host = NULL;
    char *new_path = NULL;
    int new_port;
    qm_bool new_is_secure;

    if (!client || !uri) {
        return -QM_EINVAL;
    }

    if (ws_uri_is_secure(uri, &new_is_secure) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (new_is_secure) {
        new_port = 443;
        host_start = uri + qm_strlen("wss://");
    } else {
        new_port = 80;
        host_start = uri + qm_strlen("ws://");
    }

    if (qm_strchr(host_start, '#') || qm_strchr(host_start, '@')) {
        return -QM_EINVAL;
    }

    uri_end = uri + qm_strlen(uri);
    path_start = qm_strchr(host_start, '/');
    query_start = qm_strchr(host_start, '?');
    if (path_start && query_start && query_start < path_start) {
        path_start = NULL;
    }
    authority_end = path_start ? path_start : query_start ? query_start : uri_end;
    if (host_start >= authority_end) {
        return -QM_EINVAL;
    }

    port_start = NULL;
    if (*host_start == '[') {
        const char *literal_end = qm_strchr(host_start, ']');
        if (!literal_end || literal_end >= authority_end || literal_end == host_start + 1) {
            return -QM_EINVAL;
        }
        host_start++;
        host_end = literal_end;
        if (literal_end + 1 < authority_end) {
            if (*(literal_end + 1) != ':') {
                return -QM_EINVAL;
            }
            port_start = literal_end + 1;
        }
    } else {
        host_end = authority_end;
        for (const char *p = host_start; p < authority_end; p++) {
            if (*p == ':') {
                port_start = p;
                host_end = p;
                break;
            }
        }
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0) {
        return -QM_EINVAL;
    }
    for (const char *p = host_start; p < host_end; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c >= 0x7f || *p == '/' || *p == '?' || *p == '#') {
            return -QM_EINVAL;
        }
    }

    path_len = path_start ? qm_strlen(path_start) : query_start ? qm_strlen(query_start) + 1 : 1;
    if ((path_start && ws_uri_has_invalid_path_query_chars(path_start)) ||
        (!path_start && query_start && ws_uri_has_invalid_path_query_chars(query_start))) {
        return -QM_EINVAL;
    }

    if (port_start) {
        if (ws_parse_port(port_start + 1, authority_end, &new_port) != QM_EOK) {
            return -QM_EINVAL;
        }
    }

    new_uri = qm_strdup(uri);
    new_host = qm_malloc(host_len + 1);
    new_path = qm_malloc(path_len + 1);
    if (!new_uri || !new_host || !new_path) {
        qm_free(new_uri);
        qm_free(new_host);
        qm_free(new_path);
        return -QM_ENOMEM;
    }

    qm_memcpy(new_host, host_start, host_len);
    new_host[host_len] = '\0';
    if (path_start) {
        qm_memcpy(new_path, path_start, path_len + 1);
    } else if (query_start) {
        new_path[0] = '/';
        qm_memcpy(new_path + 1, query_start, path_len);
    } else {
        qm_strcpy(new_path, "/");
    }

    qm_free(client->uri);
    qm_free(client->host);
    qm_free(client->path);
    client->uri = new_uri;
    client->host = new_host;
    client->path = new_path;
    client->port = new_port;
    client->is_secure = new_is_secure;

    return QM_EOK;
}

static int ws_set_endpoint(qm_websocket_client_t *client, const char *host, int port,
                           const char *path, qm_bool is_secure)
{
    size_t host_len;
    size_t path_len;
    size_t uri_len;
    char *new_uri = NULL;
    char *new_host = NULL;
    char *new_path = NULL;
    int new_port = port;
    const char *host_src = host;
    const char *new_path_src = path;

    if (!client || !host || !host[0] || port < 0 || port > 65535) {
        return -QM_EINVAL;
    }

    if (new_port == 0) {
        new_port = is_secure ? 443 : 80;
    }
    if (!new_path_src || new_path_src[0] == '\0') {
        new_path_src = "/";
    }

    host_len = qm_strlen(host_src);
    if (host_src[0] == '[') {
        if (host_len < 3 || host_src[host_len - 1] != ']') {
            return -QM_EINVAL;
        }
        host_src++;
        host_len -= 2;
    }
    path_len = qm_strlen(new_path_src);
    if (path_len == 0 || new_path_src[0] != '/') {
        path_len++;
    }

    uri_len = qm_strlen(is_secure ? "wss://" : "ws://") + host_len + 8 + path_len + 1;
    if (qm_strchr(host_src, ':')) {
        uri_len += 2;
    }
    new_uri = qm_malloc(uri_len);
    new_host = qm_malloc(host_len + 1);
    new_path = qm_malloc(path_len + 1);
    if (!new_uri || !new_host || !new_path) {
        qm_free(new_uri);
        qm_free(new_host);
        qm_free(new_path);
        return -QM_ENOMEM;
    }

    qm_memcpy(new_host, host_src, host_len);
    new_host[host_len] = '\0';
    if (new_path_src[0] == '/') {
        qm_strcpy(new_path, new_path_src);
    } else {
        new_path[0] = '/';
        qm_strcpy(new_path + 1, new_path_src);
    }

    if (ws_host_needs_brackets(new_host)) {
        qm_snprintf(new_uri, uri_len, "%s://[%s]:%d%s", is_secure ? "wss" : "ws",
                    new_host, new_port, new_path);
    } else {
        qm_snprintf(new_uri, uri_len, "%s://%s:%d%s", is_secure ? "wss" : "ws",
                    new_host, new_port, new_path);
    }

    qm_free(client->uri);
    qm_free(client->host);
    qm_free(client->path);
    client->uri = new_uri;
    client->host = new_host;
    client->path = new_path;
    client->port = new_port;
    client->is_secure = is_secure;

    return QM_EOK;
}

static int ws_read_exact(qm_websocket_client_t *client, uint8_t *buf, int len, uint32_t timeout_ms)
{
    int done = 0;
    while (done < len && !client->stop_requested) {
        int ret = client->transport->read(client->transport_handle, (char *)buf + done, len - done, timeout_ms);
        if (ret == 0) {
            return done == 0 ? 0 : -QM_ETIMEOUT;
        }
        if (ret < 0) {
            return ret;
        }
        done += ret;
    }
    return done;
}

static int ws_write_all(qm_websocket_client_t *client, const uint8_t *buf, int len, uint32_t timeout_ms)
{
    int done = 0;
    while (done < len && !client->stop_requested) {
        int ret = client->transport->write(client->transport_handle, (const char *)buf + done, len - done, timeout_ms);
        if (ret <= 0) {
            return ret == 0 ? -QM_ETIMEOUT : ret;
        }
        done += ret;
    }
    return done;
}

/**
 * @brief Encode and send one client WebSocket frame.
 *
 * This helper builds the RFC6455 frame header, always applies a client-side
 * masking key, and writes both header and payload through the selected
 * transport. It is the common low-level send path for data, ping, pong and
 * close frames.
 *
 * @param[in] client WebSocket client context
 * @param[in] opcode WebSocket opcode
 * @param[in] data Optional payload buffer
 * @param[in] len Payload length in bytes
 * @param[in] fin Whether this frame is the final fragment
 * @param[in] timeout_ms Write timeout in milliseconds
 * @return Number of payload bytes sent on success
 * @return Negative error code on failure
 */
static int ws_send_frame(qm_websocket_client_t *client, int opcode, const uint8_t *data, int len, qm_bool fin, uint32_t timeout_ms)
{
    uint8_t header[14];
    uint8_t mask_key[4];
    int header_len = 0;
    int ret;

    if (!client || !client->transport_handle || len < 0) {
        QM_WEBSOCKET_LOGE(TAG, "Send frame invalid args: client=%p transport=%p opcode=0x%02x len=%d fin=%d",
                client, client ? client->transport_handle : NULL, opcode, len, fin);
        return -QM_EINVAL;
    }

    QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_TX, "TX frame start: opcode=0x%02x len=%d fin=%d timeout=%ums",
                 opcode, len, fin, timeout_ms);
    header[header_len++] = (fin ? WS_FIN : 0) | (opcode & 0x0f);
    if (len <= 125) {
        header[header_len++] = WS_MASK | len;
    } else if (len <= 0xffff) {
        header[header_len++] = WS_MASK | WS_SIZE16;
        header[header_len++] = (uint8_t)(len >> 8);
        header[header_len++] = (uint8_t)len;
    } else {
        header[header_len++] = WS_MASK | WS_SIZE64;
        header[header_len++] = 0;
        header[header_len++] = 0;
        header[header_len++] = 0;
        header[header_len++] = 0;
        header[header_len++] = (uint8_t)(len >> 24);
        header[header_len++] = (uint8_t)(len >> 16);
        header[header_len++] = (uint8_t)(len >> 8);
        header[header_len++] = (uint8_t)len;
    }

    for (int i = 0; i < 4; i++) {
        mask_key[i] = (uint8_t)(qm_random_get(256) & 0xff);
        header[header_len++] = mask_key[i];
    }

    qm_mutex_lock(&client->mutex, QM_WAIT_FOREVER);
    ret = ws_write_all(client, header, header_len, timeout_ms);
    if (ret == header_len && len > 0) {
        uint8_t *masked = qm_malloc(len);
        if (!masked) {
            QM_WEBSOCKET_LOGE(TAG, "Send frame mask buffer alloc failed: opcode=0x%02x len=%d", opcode, len);
            qm_mutex_unlock(&client->mutex);
            return -QM_ENOMEM;
        }
        for (int i = 0; i < len; i++) {
            masked[i] = data[i] ^ mask_key[i % 4];
        }
        ret = ws_write_all(client, masked, len, timeout_ms);
        qm_free(masked);
    }
    qm_mutex_unlock(&client->mutex);

    if (ret < 0) {
        QM_WEBSOCKET_LOGE(TAG, "Send frame failed: opcode=0x%02x len=%d fin=%d ret=%d",
                opcode, len, fin, ret);
        return ret;
    }

    return len;
}

static int ws_send_close_frame(qm_websocket_client_t *client, int code, const char *data, int len, uint32_t timeout_ms)
{
    uint8_t *close_payload = NULL;
    int payload_len = 0;
    int ret;

    if (!client || len < 0 || (!data && len > 0)) {
        return -QM_EINVAL;
    }

    if (code > 0) {
        close_payload = (uint8_t *)qm_malloc(WS_CLOSE_STATUS_SIZE + WS_CLOSE_REASON_MAX);
        if (!close_payload) {
            return -QM_ENOMEM;
        }
        if (len > WS_CLOSE_REASON_MAX) {
            len = WS_CLOSE_REASON_MAX;
        }
        close_payload[0] = (uint8_t)((uint16_t)code >> 8);
        close_payload[1] = (uint8_t)((uint16_t)code & 0xff);
        payload_len = WS_CLOSE_STATUS_SIZE;
        if (len > 0) {
            qm_memcpy(close_payload + WS_CLOSE_STATUS_SIZE, data, len);
            payload_len += len;
        }
    }

    ret = ws_send_frame(client, WS_OPCODE_CLOSE, payload_len ? close_payload : NULL,
                        payload_len, QM_TRUE, timeout_ms);
    qm_free(close_payload);
    if (ret < 0) {
        return -QM_EIO;
    }

    client->close_sent = QM_TRUE;
    return payload_len;
}

static int ws_generate_key(char out[32])
{
    uint8_t key[16];
    uint32_t out_len = 0;
    for (int i = 0; i < (int)sizeof(key); i++) {
        key[i] = (uint8_t)(qm_random_get(256) & 0xff);
    }
    if (qm_utils_base64encode(key, sizeof(key), 32, (uint8_t *)out, &out_len) != 0) {
        return -QM_ERROR;
    }
    out[out_len] = '\0';
    return QM_EOK;
}

static int ws_expected_accept(const char *key, char out[32])
{
    static const char ws_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t sha1[20];
    uint32_t out_len = 0;
    char *seed;

    seed = (char *)qm_malloc(WS_HANDSHAKE_SEED_SIZE);
    if (!seed) {
        return -QM_ENOMEM;
    }

    if (qm_snprintf(seed, WS_HANDSHAKE_SEED_SIZE, "%s%s", key, ws_guid) >=
        WS_HANDSHAKE_SEED_SIZE) {
        qm_free(seed);
        return -QM_ENOMEM;
    }
    qm_utils_sha1((const unsigned char *)seed, qm_strlen(seed), sha1);
    qm_free(seed);
    if (qm_utils_base64encode(sha1, sizeof(sha1), 32, (uint8_t *)out, &out_len) != 0) {
        return -QM_ERROR;
    }
    out[out_len] = '\0';
    return QM_EOK;
}

static char ws_ascii_tolower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int ws_ascii_ncasecmp(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char ca = ws_ascii_tolower(a[i]);
        char cb = ws_ascii_tolower(b[i]);
        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
    return 0;
}

static qm_bool ws_header_contains_exact(const char *headers, const char *name, const char *value)
{
    const char *p = headers;
    size_t name_len = qm_strlen(name);
    size_t value_len = qm_strlen(value);

    while (p && *p) {
        const char *line_end = qm_strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : qm_strlen(p);
        if (line_len > name_len && ws_ascii_ncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
            const char *v = p + name_len + 1;
            const char *value_end;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            if ((size_t)(p + line_len - v) < value_len) {
                goto next_line;
            }
            value_end = v + value_len;
            while (value_end < p + line_len &&
                   (*value_end == ' ' || *value_end == '\t')) {
                value_end++;
            }
            if (value_end == p + line_len &&
                ws_ascii_ncasecmp(v, value, value_len) == 0) {
                return QM_TRUE;
            }
        }
next_line:
        if (!line_end) {
            break;
        }
        p = line_end + 2;
    }
    return QM_FALSE;
}

static qm_bool ws_header_contains_token(const char *headers, const char *name, const char *token)
{
    const char *p = headers;
    size_t name_len = qm_strlen(name);
    size_t token_len = qm_strlen(token);

    while (p && *p) {
        const char *line_end = qm_strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : qm_strlen(p);
        if (line_len > name_len && ws_ascii_ncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
            const char *v = p + name_len + 1;
            const char *line_limit = p + line_len;
            while (v < line_limit) {
                while (v < line_limit && (*v == ' ' || *v == '\t' || *v == ',')) {
                    v++;
                }
                const char *token_start = v;
                while (v < line_limit && *v != ',') {
                    v++;
                }
                const char *token_end = v;
                while (token_end > token_start &&
                       (token_end[-1] == ' ' || token_end[-1] == '\t')) {
                    token_end--;
                }
                if ((size_t)(token_end - token_start) == token_len &&
                    ws_ascii_ncasecmp(token_start, token, token_len) == 0) {
                    return QM_TRUE;
                }
            }
        }
        if (!line_end) {
            break;
        }
        p = line_end + 2;
    }
    return QM_FALSE;
}

static qm_bool ws_has_crlf(const char *value)
{
    return value && (qm_strchr(value, '\r') || qm_strchr(value, '\n'));
}

static qm_bool ws_headers_valid(const char *headers)
{
    const char *line = headers;

    if (!headers || headers[0] == '\0') {
        return QM_TRUE;
    }

    while (*line) {
        const char *p = line;
        const char *colon = NULL;

        while (*p && *p != '\r' && *p != '\n') {
            if (*p == ':' && !colon) {
                colon = p;
            }
            p++;
        }

        if (!colon || colon == line || p[0] != '\r' || p[1] != '\n') {
            return QM_FALSE;
        }
        line = p + 2;
    }

    return QM_TRUE;
}

static int ws_append_request(char *request, size_t request_size, int *len,
                             const char *fmt, ...)
{
    va_list args;
    int written;

    if (!request || !len || *len < 0 || *len >= (int)request_size) {
        return -QM_ENOMEM;
    }

    va_start(args, fmt);
    written = qm_vsnprintf(request + *len, request_size - *len, fmt, args);
    va_end(args);
    if (written < 0 || written >= (int)(request_size - *len)) {
        return -QM_ENOMEM;
    }

    *len += written;
    return QM_EOK;
}

static int ws_validate_config(const qm_websocket_client_config_t *config)
{
    if (!config || (!config->uri && !config->host)) {
        return -QM_EINVAL;
    }
    if (ws_has_crlf(config->subprotocol) ||
        ws_has_crlf(config->user_agent) ||
        ws_has_crlf(config->auth)) {
        return -QM_EINVAL;
    }
    if (!ws_headers_valid(config->headers)) {
        return -QM_EINVAL;
    }
    if (config->task_priority < 0 ||
        config->task_stack < 0 ||
        config->keepalive_ping_interval_ms < 0 ||
        config->reconnect_interval_ms < 0 ||
        config->max_reconnect_timeout_ms < 0 ||
        config->network_timeout_ms < 0 ||
        config->buffer_size < 0 ||
        config->send_queue_size < 0) {
        return -QM_EINVAL;
    }
    if (config->port < 0 || config->port > 65535) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

static int ws_parse_status_code(const char *response)
{
    const char *p;

    if (!response || qm_strncmp(response, "HTTP/", qm_strlen("HTTP/")) != 0) {
        return 0;
    }

    p = qm_strchr(response, ' ');
    if (!p) {
        return 0;
    }
    while (*p == ' ') {
        p++;
    }

    if (p[0] < '0' || p[0] > '9' ||
        p[1] < '0' || p[1] > '9' ||
        p[2] < '0' || p[2] > '9') {
        return 0;
    }

    return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

static int ws_handshake(qm_websocket_client_t *client)
{
    char key[WS_HANDSHAKE_KEY_LEN];
    char accept[WS_HANDSHAKE_ACCEPT_LEN];
    char *request = NULL;
    char *response = NULL;
    char *host_header = NULL;
    int len;
    int total = 0;
    int status_code;
    int ret = QM_EOK;

    request = (char *)qm_malloc(WS_HANDSHAKE_REQUEST_SIZE);
    response = (char *)qm_malloc(WS_HANDSHAKE_RESPONSE_SIZE);
    host_header = (char *)qm_malloc(WS_HANDSHAKE_HOST_SIZE);
    if (!request || !response || !host_header) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    if (ws_generate_key(key) != QM_EOK || ws_expected_accept(key, accept) != QM_EOK) {
        QM_WEBSOCKET_LOGE(TAG, "Handshake key generation failed");
        ret = -QM_ERROR;
        goto cleanup;
    }

    if (ws_host_needs_brackets(client->host)) {
        len = qm_snprintf(host_header, WS_HANDSHAKE_HOST_SIZE, "[%s]:%d", client->host, client->port);
    } else {
        len = qm_snprintf(host_header, WS_HANDSHAKE_HOST_SIZE, "%s:%d", client->host, client->port);
    }
    if (len <= 0 || len >= WS_HANDSHAKE_HOST_SIZE) {
        QM_WEBSOCKET_LOGE(TAG, "Handshake host header too long: host=%s port=%d", client->host, client->port);
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket handshake start: host=%s path=%s", host_header, client->path);

    len = 0;
    if (ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len,
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: %s\r\n"
                          "Sec-WebSocket-Version: 13\r\n",
                          client->path, host_header, key) != QM_EOK) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    if (client->config.subprotocol &&
        ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len,
                          "Sec-WebSocket-Protocol: %s\r\n",
                          client->config.subprotocol) != QM_EOK) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (client->config.user_agent &&
        ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len,
                          "User-Agent: %s\r\n",
                          client->config.user_agent) != QM_EOK) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (client->config.auth &&
        ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len,
                          "Authorization: %s\r\n",
                          client->config.auth) != QM_EOK) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (client->config.headers) {
        if (ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len, "%s",
                              client->config.headers) != QM_EOK) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        if (len >= 2 && qm_strcmp(request + len - 2, "\r\n") != 0) {
            if (ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len, "\r\n") != QM_EOK) {
                ret = -QM_ENOMEM;
                goto cleanup;
            }
        }
    }
    if (ws_append_request(request, WS_HANDSHAKE_REQUEST_SIZE, &len, "\r\n") != QM_EOK) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    if (ws_write_all(client, (const uint8_t *)request, len,
                     client->config.network_timeout_ms) != len) {
        QM_WEBSOCKET_LOGE(TAG, "Handshake request write failed");
        ret = -QM_EIO;
        goto cleanup;
    }

    while (total < WS_HANDSHAKE_RESPONSE_SIZE - 1) {
        int read_ret = client->transport->read(client->transport_handle, response + total, 1,
                                               client->config.network_timeout_ms);
        if (read_ret <= 0) {
            QM_WEBSOCKET_LOGE(TAG, "Handshake response read failed: ret=%d received=%d", read_ret, total);
            ret = -QM_EIO;
            goto cleanup;
        }
        total += read_ret;
        response[total] = '\0';
        if (qm_strstr(response, "\r\n\r\n")) {
            break;
        }
    }

    status_code = ws_parse_status_code(response);
    if (status_code != 101) {
        QM_WEBSOCKET_LOGE(TAG, "Handshake status error: status=%d", status_code);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_HANDSHAKE, QM_ERROR, 0, status_code);
        ret = -QM_ERROR;
        goto cleanup;
    }
    if (!ws_header_contains_exact(response, "Upgrade", "websocket") ||
        !ws_header_contains_token(response, "Connection", "Upgrade") ||
        !ws_header_contains_exact(response, "Sec-WebSocket-Accept", accept)) {
        QM_WEBSOCKET_LOGE(TAG, "Handshake header validation failed: status=%d", status_code);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_HANDSHAKE, QM_ERROR, 0, status_code);
        ret = -QM_ERROR;
        goto cleanup;
    }

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket handshake success: status=%d", status_code);

cleanup:
    qm_free(request);
    qm_free(response);
    qm_free(host_header);
    return ret;
}

/**
 * @brief Establish transport and complete the WebSocket opening handshake.
 *
 * This function owns one connect attempt end-to-end: it creates the transport
 * connection, sends the HTTP Upgrade request, validates the server handshake
 * response, and updates error details when the attempt fails.
 *
 * @param[in] client WebSocket client context
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
static int ws_connect(qm_websocket_client_t *client)
{
    if (!client->transport) {
        client->transport = client->is_secure ?
                            qm_websocket_transport_tls_create(client->config.tls_config) :
                            qm_websocket_transport_tcp_create();
        client->transport_owned = QM_TRUE;
    }
    if (!client->transport) {
        return -QM_ENOMEM;
    }
    if (!ws_transport_is_valid(client->transport)) {
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_TRANSPORT, -QM_EINVAL, -QM_EINVAL, 0);
        return -QM_EINVAL;
    }
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Connecting to %s:%d, timeout=%dms",
            client->host, client->port, client->config.network_timeout_ms);
    client->transport_handle = client->transport->connect(client->host, (uint16_t)client->port, client->transport->config);
    if (!client->transport_handle) {
        QM_WEBSOCKET_LOGE(TAG, "Transport connect failed: %s://%s:%d", client->is_secure ? "wss" : "ws",
                client->host, client->port);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_TRANSPORT, -QM_EIO, -QM_EIO, 0);
        return -QM_EIO;
    }
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Transport connected: %s:%d", client->host, client->port);

    int ret = ws_handshake(client);
    if (ret != QM_EOK) {
        QM_WEBSOCKET_LOGE(TAG, "WebSocket handshake failed: ret=%d", ret);
        client->transport->close(client->transport_handle);
        client->transport_handle = NULL;
        return ret;
    }
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket connected: %s://%s:%d%s", client->is_secure ? "wss" : "ws",
            client->host, client->port, client->path);
    return QM_EOK;
}

static void ws_disconnect(qm_websocket_client_t *client, qm_bool send_close)
{
    if (!client) {
        return;
    }
    if (client->transport_handle && ws_transport_is_valid(client->transport)) {
        if (send_close && !client->close_sent) {
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Sending close frame before disconnect");
            ws_send_close_frame(client, 0, NULL, 0, 1000);
        }
        QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Closing transport: %s://%s:%d", client->is_secure ? "wss" : "ws",
                client->host ? client->host : "", client->port);
        client->transport->close(client->transport_handle);
        client->transport_handle = NULL;
    }
    client->close_sent = QM_FALSE;
    client->frag_opcode = 0;
    client->ping_outstanding = QM_FALSE;
    client->ping_timeout_count = 0;
}

static qm_bool ws_tx_fragment_is_valid(qm_websocket_client_t *client, int opcode)
{
    if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) {
        return client->tx_frag_opcode == 0;
    }

    if (opcode == WS_OPCODE_CONT) {
        return client->tx_frag_opcode != 0;
    }

    return QM_TRUE;
}

static void ws_tx_fragment_update(qm_websocket_client_t *client, int opcode, qm_bool fin)
{
    if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) {
        client->tx_frag_opcode = fin ? 0 : opcode;
    } else if (opcode == WS_OPCODE_CONT && fin) {
        client->tx_frag_opcode = 0;
    }
}

static int ws_data_opcode(qm_websocket_client_t *client, int opcode)
{
    if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) {
        client->frag_opcode = opcode;
        return opcode;
    }

    if (opcode == WS_OPCODE_CONT) {
        if (!client->frag_opcode) {
            return 0;
        }
        return client->frag_opcode;
    }

    return opcode;
}

static int ws_read_payload_chunk(qm_websocket_client_t *client, uint8_t *buf, int len,
                                 qm_bool masked, uint8_t mask_key[4], int payload_offset)
{
    if (len <= 0) {
        return 0;
    }
    if (ws_read_exact(client, buf, len, client->config.network_timeout_ms) != len) {
        return -QM_EIO;
    }
    if (masked) {
        for (int i = 0; i < len; i++) {
            buf[i] ^= mask_key[(payload_offset + i) % 4];
        }
    }
    return len;
}

static int ws_dispatch_data_payload(qm_websocket_client_t *client, int opcode, qm_bool fin,
                                    qm_bool masked, uint8_t mask_key[4], int payload_len)
{
    int offset = 0;
    int event_opcode = ws_data_opcode(client, opcode);

    if (!event_opcode) {
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if (payload_len == 0) {
        QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_RX, "RX data dispatch: opcode=0x%02x chunk_len=0 offset=0 payload_len=0 fin=%d",
                     event_opcode, fin);
        ws_dispatch(client, WEBSOCKET_EVENT_DATA, NULL, 0, event_opcode, fin, 0, 0, 0);
        if (fin) {
            client->frag_opcode = 0;
        }
        return QM_EOK;
    }

    while (offset < payload_len) {
        int chunk_len = payload_len - offset;
        qm_bool chunk_fin;

        if (chunk_len > client->rx_buffer_size) {
            chunk_len = client->rx_buffer_size;
        }
        if (ws_read_payload_chunk(client, client->rx_buffer, chunk_len, masked, mask_key, offset) != chunk_len) {
            return -QM_EIO;
        }

        chunk_fin = fin && (offset + chunk_len == payload_len);
        QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_RX, "RX data dispatch: opcode=0x%02x chunk_len=%d offset=%d payload_len=%d fin=%d",
                     event_opcode, chunk_len, offset, payload_len, chunk_fin);
        ws_dispatch(client, WEBSOCKET_EVENT_DATA, client->rx_buffer, chunk_len, event_opcode,
                    chunk_fin, payload_len, offset, 0);
        offset += chunk_len;
    }

    if (fin) {
        client->frag_opcode = 0;
    }

    return QM_EOK;
}

static int ws_receive_frame(qm_websocket_client_t *client)
{
    uint8_t hdr[2];
    uint8_t ext[8];
    uint8_t mask_key[4];
    uint8_t *control_payload = NULL;
    int payload_len;
    int opcode;
    qm_bool fin;
    qm_bool masked;
    qm_bool is_control;
    int ret;

    ret = ws_read_exact(client, hdr, 2, 100);
    if (ret == 0) {
        return QM_EOK;
    }
    if (ret != 2) {
        return -QM_EIO;
    }

    fin = (hdr[0] & WS_FIN) != 0;
    opcode = hdr[0] & 0x0f;
    masked = (hdr[1] & WS_MASK) != 0;
    payload_len = hdr[1] & 0x7f;
    is_control = opcode >= 0x08;

    if ((hdr[0] & 0x70) != 0 || masked) {
        QM_WEBSOCKET_LOGE(TAG, "Receive frame protocol error: opcode=0x%02x reserved=0x%02x masked=%d",
                opcode, hdr[0] & 0x70, masked);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if ((opcode >= 0x03 && opcode <= 0x07) || opcode >= 0x0b) {
        QM_WEBSOCKET_LOGE(TAG, "Receive frame unsupported opcode: opcode=0x%02x", opcode);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if (payload_len == WS_SIZE16) {
        if (ws_read_exact(client, ext, 2, client->config.network_timeout_ms) != 2) {
            return -QM_EIO;
        }
        payload_len = ((int)ext[0] << 8) | ext[1];
    } else if (payload_len == WS_SIZE64) {
        if (ws_read_exact(client, ext, 8, client->config.network_timeout_ms) != 8) {
            return -QM_EIO;
        }
        if (ext[0] || ext[1] || ext[2] || ext[3] || (ext[4] & 0x80)) {
            ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
            return -QM_ERROR;
        }
        uint32_t len32 = ((uint32_t)ext[4] << 24) | ((uint32_t)ext[5] << 16) |
                         ((uint32_t)ext[6] << 8) | ext[7];
        if (len32 > WS_MAX_INT) {
            return -QM_ENOMEM;
        }
        payload_len = (int)len32;
    }

    QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_RX, "RX frame: opcode=0x%02x payload_len=%d fin=%d control=%d",
                 opcode, payload_len, fin, is_control);

    if ((payload_len < WS_SIZE16 && (hdr[1] & 0x7f) == WS_SIZE16) ||
        (payload_len <= 0xffff && (hdr[1] & 0x7f) == WS_SIZE64)) {
        QM_WEBSOCKET_LOGE(TAG, "Receive frame non-minimal length: opcode=0x%02x payload_len=%d len_code=%d",
                opcode, payload_len, hdr[1] & 0x7f);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if (is_control && (!fin || payload_len > 125)) {
        QM_WEBSOCKET_LOGE(TAG, "Receive control frame invalid: opcode=0x%02x payload_len=%d fin=%d",
                opcode, payload_len, fin);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if (opcode == WS_OPCODE_CONT && !client->frag_opcode) {
        QM_WEBSOCKET_LOGE(TAG, "Receive continuation without fragmented message: payload_len=%d fin=%d",
                payload_len, fin);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if ((opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) && client->frag_opcode) {
        QM_WEBSOCKET_LOGE(TAG, "Receive new data frame during fragmented message: opcode=0x%02x active_opcode=0x%02x payload_len=%d fin=%d",
                opcode, client->frag_opcode, payload_len, fin);
        ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
        return -QM_ERROR;
    }

    if (masked && ws_read_exact(client, mask_key, 4, client->config.network_timeout_ms) != 4) {
        return -QM_EIO;
    }

    if (is_control && payload_len > 0) {
        control_payload = (uint8_t *)qm_malloc((uint32_t)payload_len);
        if (!control_payload) {
            return -QM_ENOMEM;
        }
    }

    switch (opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
        case WS_OPCODE_CONT:
            ret = ws_dispatch_data_payload(client, opcode, fin, masked, mask_key, payload_len);
            break;
        case WS_OPCODE_PING:
            if (payload_len > WS_CONTROL_PAYLOAD_MAX ||
                ws_read_payload_chunk(client, control_payload, payload_len, masked, mask_key, 0) != payload_len) {
                ret = -QM_EIO;
                break;
            }
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Received PING: payload_len=%d, sending PONG", payload_len);
            ws_send_frame(client, WS_OPCODE_PONG, control_payload, payload_len, QM_TRUE, 1000);
            if (client->config.propagate_control_frames) {
                ws_dispatch(client, WEBSOCKET_EVENT_DATA, control_payload, payload_len, opcode, fin, payload_len, 0, 0);
            }
            ret = QM_EOK;
            break;
        case WS_OPCODE_PONG:
            if (payload_len > WS_CONTROL_PAYLOAD_MAX ||
                ws_read_payload_chunk(client, control_payload, payload_len, masked, mask_key, 0) != payload_len) {
                ret = -QM_EIO;
                break;
            }
            client->ping_outstanding = QM_FALSE;
            client->ping_timeout_count = 0;
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Recv PONG");
            if (client->config.propagate_control_frames) {
                ws_dispatch(client, WEBSOCKET_EVENT_DATA, control_payload, payload_len, opcode, fin, payload_len, 0, 0);
            }
            ret = QM_EOK;
            break;
        case WS_OPCODE_CLOSE:
            if (payload_len > WS_CONTROL_PAYLOAD_MAX ||
                ws_read_payload_chunk(client, control_payload, payload_len, masked, mask_key, 0) != payload_len) {
                ret = -QM_EIO;
                break;
            }
            QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_CONTROL, "Received CLOSE frame: payload_len=%d", payload_len);
            ws_set_error(client, WEBSOCKET_ERROR_TYPE_SERVER_CLOSE, -QM_EIO, 0, 0);
            ws_send_close_frame(client, 0, NULL, 0, 1000);
            ret = -QM_EIO;
            break;
        default:
            if (payload_len > 0) {
                int offset = 0;
                while (offset < payload_len) {
                    int chunk_len = payload_len - offset;
                    if (chunk_len > client->rx_buffer_size) {
                        chunk_len = client->rx_buffer_size;
                    }
                    if (ws_read_payload_chunk(client, client->rx_buffer, chunk_len, masked, mask_key, offset) != chunk_len) {
                        ret = -QM_EIO;
                        break;
                    }
                    offset += chunk_len;
                }
                if (ret == -QM_EIO) {
                    break;
                }
            }
            ws_set_error(client, WEBSOCKET_ERROR_TYPE_PROTOCOL, QM_ERROR, 0, 0);
            ret = QM_ERROR;
            break;
    }

    qm_free(control_payload);
    return ret;
}

static void ws_drain_send_queue(qm_websocket_client_t *client)
{
    qm_websocket_message_t *message = NULL;
    unsigned int size = 0;
    uint32_t budget = (uint32_t)client->config.send_queue_size;

    /* Bound a batch so continuous producers cannot starve receive/keepalive. */
    while (budget-- != 0U) {
        int ret;
        qm_mutex_lock(&client->tx_mutex, QM_WAIT_FOREVER);
        ret = client->tx_open ?
            qm_queue_recv(&client->send_queue, &message, &size, QM_NO_WAIT) : -QM_EEMPTY;
        if (ret == QM_EOK) {
            client->tx_count--;
            (void)qm_sem_signal(&client->tx_space);
        }
        qm_mutex_unlock(&client->tx_mutex);
        if (ret != QM_EOK) break;
        if (message) {
            if (ws_send_frame(client, message->opcode, (const uint8_t *)message->data,
                              message->len, message->fin, client->config.network_timeout_ms) < 0) {
                QM_WEBSOCKET_LOGE(TAG, "Drain send queue failed: opcode=0x%02x len=%d fin=%d",
                        message->opcode, message->len, message->fin);
                ws_set_error(client, WEBSOCKET_ERROR_TYPE_TRANSPORT, -QM_EIO, -QM_EIO, 0);
                ws_set_state(client, WEBSOCKET_STATE_INIT);
                ws_dispatch(client, WEBSOCKET_EVENT_ERROR, NULL, 0, 0, QM_FALSE, 0, 0, -QM_EIO);
            }
            qm_free(message->data);
            qm_free(message);
        }
        message = NULL;
    }
}

/**
 * @brief Background task driving the WebSocket client state machine.
 *
 * The task repeatedly performs connect, handshake, frame receive, send-queue
 * draining, keepalive ping/pong checks, and reconnect backoff handling until
 * stop is requested or reconnect policy is exhausted.
 *
 * @param[in] arg WebSocket client context
 */
static void ws_task(void *arg)
{
    qm_websocket_client_t *client = (qm_websocket_client_t *)arg;

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket task started");
    while (!client->stop_requested) {
        if (client->network_state == QM_WEBSOCKET_NETWORK_DOWN) {
            (void)qm_sem_wait(&client->network_wakeup, QM_WAIT_FOREVER);
            continue;
        }
        /* A wakeup request is consumed by the next connection attempt. */
        client->reconnect_requested = QM_FALSE;
        /* Disconnects other than an explicit API reconnect request (e.g. server
         * close, receive failure, PONG timeout) are considered passive and must
         * apply the reconnect backoff instead of retrying with zero delay. */
        qm_bool apply_backoff = QM_TRUE;
        uint32_t connected_at_ms = 0;
        ws_set_state(client, WEBSOCKET_STATE_CONNECTING);
        int ret = ws_connect(client);
        if (ret != QM_EOK) {
            QM_WEBSOCKET_LOGE(TAG, "Connect attempt failed: ret=%d reconnect_count=%d", ret, client->reconnect_count);
            ws_dispatch(client, WEBSOCKET_EVENT_ERROR, NULL, 0, 0, QM_FALSE, 0, 0, ret);
            ws_disconnect(client, QM_FALSE);
            if (client->config.disable_auto_reconnect) {
                QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Auto reconnect disabled, stop reconnecting");
                break;
            }
            uint16_t delay = 0;
            if (backoff_algorithm_get_next_delay(&client->reconnect_backoff, &delay) != BACKOFF_ALGORITHM_SUCCESS) {
                QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Reconnect attempts exhausted: reconnect_count=%d", client->reconnect_count);
                break;
            }
            client->reconnect_count++;
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Reconnect scheduled: attempt=%d delay=%ums", client->reconnect_count, delay);
            if (!client->stop_requested && delay > 0U) {
                (void)qm_sem_wait(&client->network_wakeup, delay);
            }
            continue;
        }

        connected_at_ms = qm_now_ms();
        qm_memset(&client->error_info, 0, sizeof(client->error_info));
        if (client->config.keepalive_ping_interval_ms > 0) {
            qm_utils_time_countdown_ms(&client->ping_timer, client->config.keepalive_ping_interval_ms);
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Keepalive enabled: ping_interval=%dms ping_timeout=%dms ping_max_retry=%d",
                    client->config.keepalive_ping_interval_ms, client->ping_timeout_ms,
                    WS_DEFAULT_PING_MAX_RETRY);
        } else {
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Keepalive disabled");
        }
        client->ping_outstanding = QM_FALSE;
        client->ping_timeout_count = 0;
        ws_set_state(client, WEBSOCKET_STATE_CONNECTED);
        QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket state connected");
        ws_dispatch(client, WEBSOCKET_EVENT_CONNECTED, NULL, 0, 0, QM_TRUE, 0, 0, 0);

        while (!client->stop_requested && client->state == WEBSOCKET_STATE_CONNECTED) {
            if (client->network_state == QM_WEBSOCKET_NETWORK_DOWN) {
                break;
            }
            if (client->reconnect_requested) {
                client->reconnect_requested = QM_FALSE;
                apply_backoff = QM_FALSE;
                QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Reconnect requested by API");
                break;
            }
            ws_drain_send_queue(client);
            if (ws_get_state(client) != WEBSOCKET_STATE_CONNECTED) break;
            ret = ws_receive_frame(client);
            if (ret != QM_EOK) {
                if (client->error_info.error_type == WEBSOCKET_ERROR_TYPE_SERVER_CLOSE) {
                    QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Connection closed by server");
                    break;
                }
                if (client->error_info.error_type == WEBSOCKET_ERROR_TYPE_NONE) {
                    ws_set_error(client, WEBSOCKET_ERROR_TYPE_TRANSPORT, ret, ret, 0);
                }
                QM_WEBSOCKET_LOGE(TAG, "Receive frame failed: ret=%d error_type=%d", ret, client->error_info.error_type);
                ws_dispatch(client, WEBSOCKET_EVENT_ERROR, NULL, 0, 0, QM_FALSE, 0, 0, ret);
                break;
            }

            if (client->config.keepalive_ping_interval_ms > 0 &&
                client->ping_outstanding &&
                client->ping_timeout_ms > 0 &&
                qm_utils_time_is_expired(&client->pong_timer)) {
                client->ping_timeout_count++;
                client->ping_outstanding = QM_FALSE;
                QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_CONTROL, "PONG timeout: timeout=%dms count=%d/%d",
                        client->ping_timeout_ms, client->ping_timeout_count,
                        WS_DEFAULT_PING_MAX_RETRY);
                if (client->ping_timeout_count >= WS_DEFAULT_PING_MAX_RETRY) {
                    QM_WEBSOCKET_LOGE(TAG, "PONG timeout retry exhausted: max_retry=%d",
                            WS_DEFAULT_PING_MAX_RETRY);
                    ws_set_error(client, WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT, -QM_ETIMEOUT, 0, 0);
                    ws_dispatch(client, WEBSOCKET_EVENT_ERROR, NULL, 0, 0, QM_FALSE, 0, 0, -QM_ETIMEOUT);
                    break;
                }
                qm_utils_time_countdown_ms(&client->ping_timer, 0);
            }

            if (client->config.keepalive_ping_interval_ms > 0 &&
                !client->ping_outstanding &&
                qm_utils_time_is_expired(&client->ping_timer)) {
                if (ws_send_frame(client, WS_OPCODE_PING, NULL, 0, QM_TRUE, 1000) < 0) {
                    QM_WEBSOCKET_LOGE(TAG, "Send PING failed");
                    break;
                }
                QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_CONTROL, "Sent PING");
                qm_utils_time_countdown_ms(&client->ping_timer, client->config.keepalive_ping_interval_ms);
                if (client->ping_timeout_ms > 0) {
                    qm_utils_time_countdown_ms(&client->pong_timer, client->ping_timeout_ms);
                    client->ping_outstanding = QM_TRUE;
                }
            }

        }

        ws_set_state(client, client->stop_requested ? WEBSOCKET_STATE_CLOSING : WEBSOCKET_STATE_INIT);
        ws_disconnect(client, client->stop_requested);
        QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket disconnected: stop_requested=%d auto_reconnect=%d",
                client->stop_requested, !client->config.disable_auto_reconnect);
        ws_dispatch(client, WEBSOCKET_EVENT_DISCONNECTED, NULL, 0, 0, QM_TRUE, 0, 0, 0);

        if (client->stop_requested || client->config.disable_auto_reconnect) {
            break;
        }

        if (apply_backoff) {
            /* A connection that lasted at least one base backoff interval is
             * treated as stable: reset the backoff so a later transient drop
             * restarts from the base interval. A connection closed immediately
             * after the handshake keeps its backoff, letting repeated
             * server-side kicks grow exponentially instead of hammering at a
             * fixed rate. */
            uint32_t alive_ms = qm_now_ms() - connected_at_ms;
            if (alive_ms >= (uint32_t)client->config.reconnect_interval_ms) {
                client->reconnect_count = 0;
                ws_reset_reconnect_backoff(client);
            }
            uint16_t delay = 0;
            if (backoff_algorithm_get_next_delay(&client->reconnect_backoff, &delay) != BACKOFF_ALGORITHM_SUCCESS) {
                QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Reconnect attempts exhausted: reconnect_count=%d", client->reconnect_count);
                break;
            }
            client->reconnect_count++;
            QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Reconnect scheduled: attempt=%d delay=%ums", client->reconnect_count, delay);
            if (!client->stop_requested && delay > 0U) {
                (void)qm_sem_wait(&client->network_wakeup, delay);
            }
        }
    }

    ws_set_state(client, WEBSOCKET_STATE_CLOSED);
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket task stopped");
    client->task_handle.hdl = NULL;
    qm_task_exit(NULL);
}

void *qm_websocket_client_init(const qm_websocket_client_config_t *config)
{
    qm_websocket_client_t *client;

    if (ws_validate_config(config) != QM_EOK) {
        return NULL;
    }

    client = qm_calloc(1, sizeof(qm_websocket_client_t));
    if (!client) {
        return NULL;
    }

    qm_memcpy(&client->config, config, sizeof(*config));
    if (ws_copy_config_strings(client, config) != QM_EOK) {
        qm_free(client);
        return NULL;
    }
    if (config->tls_config) {
        client->tls_config_storage = *config->tls_config;
        client->config.tls_config = &client->tls_config_storage;
    }
    client->config.task_priority = config->task_priority ? config->task_priority : WS_DEFAULT_TASK_PRIORITY;
    client->config.task_stack = config->task_stack ? config->task_stack : WS_DEFAULT_TASK_STACK;
    client->config.keepalive_ping_interval_ms = config->disable_ping ? 0 :
                                                config->keepalive_ping_interval_ms > 0 ?
                                                config->keepalive_ping_interval_ms : WS_DEFAULT_PING_MS;
    client->config.reconnect_interval_ms = config->reconnect_interval_ms ? config->reconnect_interval_ms : WS_DEFAULT_RECONNECT_MS;
    client->config.max_reconnect_timeout_ms = config->max_reconnect_timeout_ms ? config->max_reconnect_timeout_ms : WS_MAX_RECONNECT_MS;
    client->config.max_reconnect_count = config->max_reconnect_count ? config->max_reconnect_count : CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_COUNT;
    client->ping_timeout_ms = WS_DEFAULT_PONG_TIMEOUT_MS;
    client->config.network_timeout_ms = config->network_timeout_ms ? config->network_timeout_ms : WS_DEFAULT_NETWORK_TIMEOUT_MS;
    client->config.buffer_size = config->buffer_size > 0 ? config->buffer_size : WS_DEFAULT_RX_BUFFER;
    client->config.send_queue_size = config->send_queue_size > 0 ? config->send_queue_size : WS_DEFAULT_QUEUE_SIZE;
    client->rx_buffer_size = client->config.buffer_size;
    client->transport = config->transport;
    client->user_context = config->user_context;
    client->state = WEBSOCKET_STATE_INIT;
    ws_reset_reconnect_backoff(client);

    if (qm_mutex_new(&client->mutex) != QM_EOK) {
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    if (qm_mutex_new(&client->tx_mutex) != QM_EOK) {
        qm_mutex_free(&client->mutex);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    if (qm_sem_new(&client->network_wakeup, 0) != QM_EOK) {
        qm_mutex_free(&client->tx_mutex);
        qm_mutex_free(&client->mutex);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    if (qm_sem_new(&client->tx_space, 0) != QM_EOK) {
        qm_sem_free(&client->network_wakeup);
        qm_mutex_free(&client->tx_mutex);
        qm_mutex_free(&client->mutex);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    client->network_wakeup_ready = QM_TRUE;
    client->rx_buffer = qm_malloc(client->rx_buffer_size);
    if (!client->rx_buffer) {
        qm_sem_free(&client->tx_space);
        qm_sem_free(&client->network_wakeup);
        qm_mutex_free(&client->tx_mutex);
        qm_mutex_free(&client->mutex);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    if (qm_queue_new(&client->send_queue, client->config.send_queue_size, sizeof(qm_websocket_message_t *)) != QM_EOK) {
        qm_sem_free(&client->tx_space);
        qm_sem_free(&client->network_wakeup);
        qm_mutex_free(&client->tx_mutex);
        qm_mutex_free(&client->mutex);
        qm_free(client->rx_buffer);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }
    if ((config->uri ? ws_parse_uri(client, config->uri) :
         ws_set_endpoint(client, config->host, config->port, config->path, config->transport_over_ssl)) != QM_EOK) {
        qm_sem_free(&client->tx_space);
        qm_queue_free(&client->send_queue);
        qm_sem_free(&client->network_wakeup);
        qm_mutex_free(&client->tx_mutex);
        qm_mutex_free(&client->mutex);
        qm_free(client->rx_buffer);
        qm_free(client->uri);
        qm_free(client->host);
        qm_free(client->path);
        ws_free_config_strings(client);
        qm_free(client);
        return NULL;
    }

    qm_srandom(qm_now_ms());
    client->network_state = QM_WEBSOCKET_NETWORK_UP;
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket client initialized: uri=%s buffer=%d task_stack=%d task_prio=%d ping_interval=%dms ping_timeout=%dms reconnect_base=%dms max_reconnect=%d auto_reconnect=%d log_flags=0x%02x",
            client->uri, client->rx_buffer_size, client->config.task_stack, client->config.task_priority,
            client->config.keepalive_ping_interval_ms, client->ping_timeout_ms,
            client->config.reconnect_interval_ms, client->config.max_reconnect_count,
            !client->config.disable_auto_reconnect,
            (unsigned int)qm_websocket_client_get_log_flags());
    return client;
}

int qm_websocket_client_start(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    if (!ws_client) {
        return -QM_EINVAL;
    }
    if (ws_client->task_handle.hdl) {
        return -QM_EBUSY;
    }

    ws_client->stop_requested = QM_FALSE;
    ws_client->reconnect_requested = QM_FALSE;
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Starting WebSocket client: uri=%s", ws_client->uri);
    if (qm_task_new(&ws_client->task_handle, "websocket_client", ws_task, ws_client,
                    ws_client->config.task_stack, ws_client->config.task_priority) != QM_EOK) {
        QM_WEBSOCKET_LOGE(TAG, "Failed to create WebSocket task");
        return -QM_ENOMEM;
    }
    return QM_EOK;
}

int qm_websocket_client_stop(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_utils_time_t stop_timer;

    if (!ws_client) {
        return -QM_EINVAL;
    }

    if (!ws_client->task_handle.hdl) {
        ws_set_state(ws_client, WEBSOCKET_STATE_CLOSED);
        QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket client already stopped");
        return QM_EOK;
    }

    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "Stopping WebSocket client");
    ws_client->stop_requested = QM_TRUE;
    if (ws_client->network_wakeup_ready) {
        (void)qm_sem_signal(&ws_client->network_wakeup);
    }
    ws_set_state(ws_client, WEBSOCKET_STATE_CLOSING);
    qm_utils_time_countdown_ms(&stop_timer,
                               ws_client->config.network_timeout_ms > WS_STOP_WAIT_MS ?
                               ws_client->config.network_timeout_ms + 500 : WS_STOP_WAIT_MS);
    while (!qm_utils_time_is_expired(&stop_timer) && ws_client->task_handle.hdl) {
        qm_msleep(50);
    }
    if (ws_client->task_handle.hdl) {
        QM_WEBSOCKET_LOGW(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket client stop timeout");
        return -QM_ETIMEOUT;
    }
    QM_WEBSOCKET_LOGI(TAG, QM_WEBSOCKET_LOG_LIFECYCLE, "WebSocket client stopped");
    return QM_EOK;
}

int qm_websocket_client_destroy(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_message_t *message = NULL;
    unsigned int size = 0;

    if (!ws_client) {
        return -QM_EINVAL;
    }

    if (qm_websocket_client_stop(ws_client) != QM_EOK) {
        return -QM_ETIMEOUT;
    }

    while (qm_queue_recv(&ws_client->send_queue, &message, &size, 0) == QM_EOK) {
        if (message) {
            qm_free(message->data);
            qm_free(message);
        }
    }
    qm_queue_free(&ws_client->send_queue);
    qm_sem_free(&ws_client->tx_space);
    if (ws_client->network_wakeup_ready) {
        qm_sem_free(&ws_client->network_wakeup);
        ws_client->network_wakeup_ready = QM_FALSE;
    }
    qm_mutex_free(&ws_client->tx_mutex);
    qm_mutex_free(&ws_client->mutex);
    if (ws_client->transport_owned) {
        qm_websocket_transport_destroy(ws_client->transport);
    }
    qm_free(ws_client->uri);
    qm_free(ws_client->host);
    qm_free(ws_client->path);
    ws_free_config_strings(ws_client);
    qm_free(ws_client->rx_buffer);
    qm_free(ws_client);
    return QM_EOK;
}

static int ws_queue_send(void *client, const char *data, int len, int opcode, qm_bool fin, uint32_t timeout_ms)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_message_t *message;
    qm_utils_time_t send_timer;
    uint32_t generation;
    uint32_t remaining = timeout_ms;
    int ret;

    if (!ws_client || (!data && len > 0) || len < 0 || timeout_ms >= UINT32_MAX / 2U) {
        return -QM_EINVAL;
    }

    qm_utils_time_countdown_ms(&send_timer, timeout_ms);
    ret = qm_mutex_lock(&ws_client->tx_mutex, remaining);
    if (ret != QM_EOK) return timeout_ms == 0U ? -QM_EBUSY : -QM_ETIMEOUT;
    generation = ws_client->tx_generation;
    while (ws_client->tx_open && ws_client->tx_generation == generation &&
           ws_client->tx_count >= (uint32_t)ws_client->config.send_queue_size) {
        qm_mutex_unlock(&ws_client->tx_mutex);
        if (timeout_ms == 0U) return -QM_EFULL;
        remaining = qm_utils_time_left(&send_timer);
        if (remaining == 0U) return -QM_ETIMEOUT;
        ret = qm_sem_wait(&ws_client->tx_space, remaining);
        if (ret != QM_EOK) return ret;
        remaining = qm_utils_time_left(&send_timer);
        if (remaining == 0U) return -QM_ETIMEOUT;
        ret = qm_mutex_lock(&ws_client->tx_mutex, remaining);
        if (ret != QM_EOK) return ret;
    }
    if (!ws_client->tx_open || ws_client->tx_generation != generation) {
        qm_mutex_unlock(&ws_client->tx_mutex);
        return -QM_EBUSY;
    }
    if (!ws_tx_fragment_is_valid(ws_client, opcode)) {
        QM_WEBSOCKET_LOGE(TAG, "Queue send fragment invalid: opcode=0x%02x len=%d fin=%d active_opcode=0x%02x",
                opcode, len, fin, ws_client->tx_frag_opcode);
        qm_mutex_unlock(&ws_client->tx_mutex);
        return -QM_EINVAL;
    }

    message = qm_calloc(1, sizeof(*message));
    if (!message) {
        qm_mutex_unlock(&ws_client->tx_mutex);
        return -QM_ENOMEM;
    }
    if (len > 0) {
        message->data = qm_malloc(len);
        if (!message->data) {
            qm_free(message);
            qm_mutex_unlock(&ws_client->tx_mutex);
            return -QM_ENOMEM;
        }
        qm_memcpy(message->data, data, len);
    }
    message->len = len;
    message->opcode = opcode;
    message->fin = fin;

    /* All producers/consumers share tx_mutex: capacity cannot disappear. */
    ret = qm_queue_send(&ws_client->send_queue, &message, sizeof(message));
    if (ret != QM_EOK) {
        qm_free(message->data);
        qm_free(message);
        qm_mutex_unlock(&ws_client->tx_mutex);
        return ret;
    }
    ws_client->tx_count++;

    ws_tx_fragment_update(ws_client, opcode, fin);
    QM_WEBSOCKET_LOGD(TAG, QM_WEBSOCKET_LOG_TX, "TX queue: opcode=0x%02x len=%d fin=%d timeout=%ums",
                 opcode, len, fin, timeout_ms);
    qm_mutex_unlock(&ws_client->tx_mutex);
    return len;
}

int qm_websocket_client_send_bin(void *client, const char *data, int len, uint32_t timeout_ms)
{
    return ws_queue_send(client, data, len, WS_OPCODE_BINARY, QM_TRUE, timeout_ms);
}

int qm_websocket_client_send_text(void *client, const char *data, int len, uint32_t timeout_ms)
{
    return ws_queue_send(client, data, len, WS_OPCODE_TEXT, QM_TRUE, timeout_ms);
}

int qm_websocket_client_send_bin_partial(void *client, const char *data, int len, uint32_t timeout_ms)
{
    return ws_queue_send(client, data, len, WS_OPCODE_BINARY, QM_FALSE, timeout_ms);
}

int qm_websocket_client_send_text_partial(void *client, const char *data, int len, uint32_t timeout_ms)
{
    return ws_queue_send(client, data, len, WS_OPCODE_TEXT, QM_FALSE, timeout_ms);
}

int qm_websocket_client_send_continuation_frame(void *client, const char *data, int len, uint32_t timeout_ms)
{
    return ws_queue_send(client, data, len, WS_OPCODE_CONT, QM_FALSE, timeout_ms);
}

int qm_websocket_client_send_fin(void *client, uint32_t timeout_ms)
{
    return ws_queue_send(client, NULL, 0, WS_OPCODE_CONT, QM_TRUE, timeout_ms);
}

int qm_websocket_client_close_with_code(void *client, int code, const char *data, int len, uint32_t timeout_ms)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_utils_time_t close_timer;

    if (!ws_client || len < 0 || (!data && len > 0)) {
        return -QM_EINVAL;
    }
    if (ws_get_state(ws_client) != WEBSOCKET_STATE_CONNECTED) {
        return -QM_EIO;
    }

    if (ws_send_close_frame(ws_client, code, data, len, timeout_ms) < 0) {
        ws_set_error(ws_client, WEBSOCKET_ERROR_TYPE_TRANSPORT, -QM_EIO, -QM_EIO, 0);
        return -QM_EIO;
    }

    ws_client->stop_requested = QM_TRUE;
    if (ws_client->network_wakeup_ready) {
        (void)qm_sem_signal(&ws_client->network_wakeup);
    }
    ws_set_state(ws_client, WEBSOCKET_STATE_CLOSING);
    qm_utils_time_countdown_ms(&close_timer, timeout_ms ? timeout_ms :
                               ws_client->config.network_timeout_ms > WS_STOP_WAIT_MS ?
                               ws_client->config.network_timeout_ms + 500 : WS_STOP_WAIT_MS);
    while (!qm_utils_time_is_expired(&close_timer) && ws_client->task_handle.hdl) {
        qm_msleep(50);
    }

    return ws_client->task_handle.hdl ? -QM_ETIMEOUT : QM_EOK;
}

int qm_websocket_client_close(void *client, uint32_t timeout_ms)
{
    return qm_websocket_client_close_with_code(client, 0, NULL, 0, timeout_ms);
}

int qm_websocket_register_events(void *client, qm_websocket_event_id_t event_id,
                                 qm_websocket_event_handler_t event_handler, void *user_context)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    if (!ws_client || !event_handler || event_id < 0 || event_id > WEBSOCKET_EVENT_MAX) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    if (event_id == WEBSOCKET_EVENT_MAX) {
        for (int i = 0; i < WEBSOCKET_EVENT_MAX; i++) {
            ws_client->event_handlers[i] = event_handler;
            ws_client->event_contexts[i] = user_context;
        }
        qm_mutex_unlock(&ws_client->mutex);
        return QM_EOK;
    }
    ws_client->event_handlers[event_id] = event_handler;
    ws_client->event_contexts[event_id] = user_context;
    qm_mutex_unlock(&ws_client->mutex);
    return QM_EOK;
}

int qm_websocket_unregister_events(void *client, qm_websocket_event_id_t event_id,
                                   qm_websocket_event_handler_t event_handler)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    if (!ws_client || event_id < 0 || event_id > WEBSOCKET_EVENT_MAX) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    if (event_id == WEBSOCKET_EVENT_MAX) {
        for (int i = 0; i < WEBSOCKET_EVENT_MAX; i++) {
            if (!event_handler || ws_client->event_handlers[i] == event_handler) {
                ws_client->event_handlers[i] = NULL;
                ws_client->event_contexts[i] = NULL;
            }
        }
        qm_mutex_unlock(&ws_client->mutex);
        return QM_EOK;
    }
    if (!event_handler || ws_client->event_handlers[event_id] == event_handler) {
        ws_client->event_handlers[event_id] = NULL;
        ws_client->event_contexts[event_id] = NULL;
    }
    qm_mutex_unlock(&ws_client->mutex);
    return QM_EOK;
}

int qm_websocket_client_set_uri(void *client, const char *uri)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_state_t state;
    int ret;

    if (!ws_client || !uri) {
        return -QM_EINVAL;
    }

    state = ws_get_state(ws_client);
    if (state != WEBSOCKET_STATE_INIT && state != WEBSOCKET_STATE_CLOSED) {
        return -QM_EBUSY;
    }

    ret = ws_parse_uri(ws_client, uri);
    if (ret == QM_EOK && ws_client->transport_owned) {
        qm_websocket_transport_destroy(ws_client->transport);
        ws_client->transport = NULL;
        ws_client->transport_owned = QM_FALSE;
    }
    return ret;
}

int qm_websocket_client_set_host(void *client, const char *host, int port,
                                 const char *path, qm_bool transport_over_ssl)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_state_t state;
    int ret;

    if (!ws_client || !host) {
        return -QM_EINVAL;
    }

    state = ws_get_state(ws_client);
    if (state != WEBSOCKET_STATE_INIT && state != WEBSOCKET_STATE_CLOSED) {
        return -QM_EBUSY;
    }

    ret = ws_set_endpoint(ws_client, host, port, path, transport_over_ssl);
    if (ret == QM_EOK && ws_client->transport_owned) {
        qm_websocket_transport_destroy(ws_client->transport);
        ws_client->transport = NULL;
        ws_client->transport_owned = QM_FALSE;
    }
    return ret;
}

int qm_websocket_client_set_headers(void *client, const char *headers)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_state_t state;
    char *new_headers = NULL;

    if (!ws_client) {
        return -QM_EINVAL;
    }

    state = ws_get_state(ws_client);
    if (state != WEBSOCKET_STATE_INIT && state != WEBSOCKET_STATE_CLOSED) {
        return -QM_EBUSY;
    }

    if (headers) {
        if (!ws_headers_valid(headers)) {
            return -QM_EINVAL;
        }
        new_headers = qm_strdup(headers);
        if (!new_headers) {
            return -QM_ENOMEM;
        }
    }

    if (ws_client->headers_owned) {
        qm_free((void *)ws_client->config.headers);
    }
    ws_client->config.headers = new_headers;
    ws_client->headers_owned = new_headers != NULL;
    return QM_EOK;
}

int qm_websocket_client_append_header(void *client, const char *key, const char *value)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_state_t state;
    const char *headers;
    char *new_headers;
    size_t old_len;
    size_t key_len;
    size_t value_len;
    size_t new_len;

    if (!ws_client || !key || !value) {
        return -QM_EINVAL;
    }

    state = ws_get_state(ws_client);
    if (state != WEBSOCKET_STATE_INIT && state != WEBSOCKET_STATE_CLOSED) {
        return -QM_EBUSY;
    }

    headers = ws_client->config.headers ? ws_client->config.headers : "";
    old_len = qm_strlen(headers);
    key_len = qm_strlen(key);
    value_len = qm_strlen(value);
    if (key_len == 0 || ws_has_crlf(key) || ws_has_crlf(value)) {
        return -QM_EINVAL;
    }
    if (old_len > SIZE_MAX - key_len ||
        old_len + key_len > SIZE_MAX - value_len ||
        old_len + key_len + value_len > SIZE_MAX - qm_strlen(": \r\n") - 1) {
        return -QM_ENOMEM;
    }

    new_len = old_len + key_len + value_len + qm_strlen(": \r\n") + 1;
    new_headers = qm_malloc(new_len);
    if (!new_headers) {
        return -QM_ENOMEM;
    }

    qm_snprintf(new_headers, new_len, "%s%s: %s\r\n", headers, key, value);

    if (ws_client->headers_owned) {
        qm_free((void *)ws_client->config.headers);
    }
    ws_client->config.headers = new_headers;
    ws_client->headers_owned = QM_TRUE;
    return QM_EOK;
}

int qm_websocket_client_reconnect(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    if (!ws_client) {
        return -QM_EINVAL;
    }
    if (!ws_client->task_handle.hdl) {
        ws_client->reconnect_count = 0;
        ws_reset_reconnect_backoff(ws_client);
        ws_set_state(ws_client, WEBSOCKET_STATE_INIT);
        return QM_EOK;
    }
    ws_client->reconnect_requested = QM_TRUE;
    ws_client->reconnect_count = 0;
    ws_reset_reconnect_backoff(ws_client);
    if (ws_client->network_wakeup_ready) {
        (void)qm_sem_signal(&ws_client->network_wakeup);
    }
    return QM_EOK;
}

int qm_websocket_client_network_update(void *client,
                                       qm_websocket_network_state_t state)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    qm_websocket_network_state_t previous_state;

    if (ws_client == NULL ||
        (state != QM_WEBSOCKET_NETWORK_DOWN &&
         state != QM_WEBSOCKET_NETWORK_UP)) {
        return -QM_EINVAL;
    }
    previous_state = ws_client->network_state;
    ws_client->network_state = state;
    if (previous_state != state && state == QM_WEBSOCKET_NETWORK_UP) {
        ws_client->reconnect_requested = QM_TRUE;
        ws_client->reconnect_count = 0;
        ws_reset_reconnect_backoff(ws_client);
    }
    if (ws_client->network_wakeup_ready && previous_state != state) {
        (void)qm_sem_signal(&ws_client->network_wakeup);
    }
    return QM_EOK;
}

int qm_websocket_client_get_ping_interval(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    int ping_interval_ms;

    if (!ws_client) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    ping_interval_ms = ws_client->config.keepalive_ping_interval_ms;
    qm_mutex_unlock(&ws_client->mutex);
    return ping_interval_ms;
}

int qm_websocket_client_set_ping_interval(void *client, int ping_interval_ms)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;

    if (!ws_client || ping_interval_ms < 0) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    ws_client->config.keepalive_ping_interval_ms = ping_interval_ms;
    if (ping_interval_ms > 0 && ws_client->state == WEBSOCKET_STATE_CONNECTED) {
        qm_utils_time_countdown_ms(&ws_client->ping_timer, ping_interval_ms);
        ws_client->ping_outstanding = QM_FALSE;
    } else if (ping_interval_ms == 0) {
        qm_utils_time_init(&ws_client->ping_timer);
        ws_client->ping_outstanding = QM_FALSE;
    }
    qm_mutex_unlock(&ws_client->mutex);
    return QM_EOK;
}

int qm_websocket_client_get_reconnect_timeout(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    int reconnect_interval_ms;

    if (!ws_client) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    reconnect_interval_ms = ws_client->config.disable_auto_reconnect ? -1 : ws_client->config.reconnect_interval_ms;
    qm_mutex_unlock(&ws_client->mutex);
    return reconnect_interval_ms;
}

int qm_websocket_client_set_reconnect_timeout(void *client, int reconnect_interval_ms)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;

    if (!ws_client || reconnect_interval_ms <= 0) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    ws_client->config.reconnect_interval_ms = reconnect_interval_ms;
    ws_reset_reconnect_backoff(ws_client);
    ws_client->reconnect_count = 0;
    qm_mutex_unlock(&ws_client->mutex);
    return QM_EOK;
}

uint32_t qm_websocket_client_get_log_flags(void)
{
    return g_qm_websocket_client_log_flags;
}

int qm_websocket_client_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_WEBSOCKET_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_websocket_client_log_flags = log_flags;
    return QM_EOK;
}

qm_bool qm_websocket_client_is_connected(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    return ws_client && ws_get_state(ws_client) == WEBSOCKET_STATE_CONNECTED;
}

qm_websocket_state_t qm_websocket_client_get_state(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    return ws_client ? ws_get_state(ws_client) : WEBSOCKET_STATE_CLOSED;
}

int qm_websocket_client_set_user_context(void *client, void *user_context)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    if (!ws_client) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    ws_client->user_context = user_context;
    qm_mutex_unlock(&ws_client->mutex);
    return QM_EOK;
}

void *qm_websocket_client_get_user_context(void *client)
{
    qm_websocket_client_t *ws_client = (qm_websocket_client_t *)client;
    void *user_context = NULL;

    if (!ws_client) {
        return NULL;
    }

    qm_mutex_lock(&ws_client->mutex, QM_WAIT_FOREVER);
    user_context = ws_client->user_context;
    qm_mutex_unlock(&ws_client->mutex);
    return user_context;
}
