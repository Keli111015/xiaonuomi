/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include "qm.h"
#include "qm_errno.h"
#include "qm_event.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"
#include "qm_websocket_client.h"

#define LOG_TAG "websocket_example"

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID       "your_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD   "your_password"
#endif

#ifndef EXAMPLE_WEBSOCKET_URI
#define EXAMPLE_WEBSOCKET_URI   "ws://echo.websocket.events"
#endif

#ifndef EXAMPLE_WEBSOCKET_USE_HOST
#define EXAMPLE_WEBSOCKET_USE_HOST QM_FALSE
#endif

#ifndef EXAMPLE_WEBSOCKET_HOST
#define EXAMPLE_WEBSOCKET_HOST  "echo.websocket.events"
#endif

#ifndef EXAMPLE_WEBSOCKET_PORT
#define EXAMPLE_WEBSOCKET_PORT  80
#endif

#ifndef EXAMPLE_WEBSOCKET_PATH
#define EXAMPLE_WEBSOCKET_PATH  "/"
#endif

#ifndef EXAMPLE_WEBSOCKET_SSL
#define EXAMPLE_WEBSOCKET_SSL   QM_FALSE
#endif

#ifndef EXAMPLE_TLS_CA_CERT
#define EXAMPLE_TLS_CA_CERT     NULL
#endif

#ifndef EXAMPLE_TLS_CA_CERT_LEN
#define EXAMPLE_TLS_CA_CERT_LEN 0
#endif

#ifndef EXAMPLE_TLS_CLIENT_CERT
#define EXAMPLE_TLS_CLIENT_CERT     NULL
#endif

#ifndef EXAMPLE_TLS_CLIENT_CERT_LEN
#define EXAMPLE_TLS_CLIENT_CERT_LEN 0
#endif

#ifndef EXAMPLE_TLS_CLIENT_KEY
#define EXAMPLE_TLS_CLIENT_KEY      NULL
#endif

#ifndef EXAMPLE_TLS_CLIENT_KEY_LEN
#define EXAMPLE_TLS_CLIENT_KEY_LEN  0
#endif

#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10

typedef struct {
    void *client;
} websocket_client_example_handle_t;

static websocket_client_example_handle_t *s_example_handle;
static int s_wifi_retry_count;

static void websocket_event_handler(qm_websocket_event_id_t event_id,
                                    qm_websocket_event_data_t *event_data,
                                    void *user_context)
{
    const char *message = (const char *)user_context;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            const uint8_t binary_data[] = {0x01, 0x02, 0x03, 0x04};
            const uint8_t binary_part1[] = {0x10, 0x11};
            const uint8_t binary_part2[] = {0x12, 0x13};
            const char *part1 = "fragment-";
            const char *part2 = "message";

            QM_LOGI(LOG_TAG, "connected");
            QM_LOGI(LOG_TAG, "state=%d connected=%d ping=%d reconnect=%d user=%p",
                    qm_websocket_client_get_state(event_data->client),
                    qm_websocket_client_is_connected(event_data->client),
                    qm_websocket_client_get_ping_interval(event_data->client),
                    qm_websocket_client_get_reconnect_timeout(event_data->client),
                    qm_websocket_client_get_user_context(event_data->client));

            if (message) {
                qm_websocket_client_send_text(event_data->client, message, qm_strlen(message), 5000);
            }
            qm_websocket_client_send_bin(event_data->client, (const char *)binary_data, sizeof(binary_data), 5000);
            qm_websocket_client_send_text_partial(event_data->client, part1, qm_strlen(part1), 5000);
            qm_websocket_client_send_continuation_frame(event_data->client, part2, qm_strlen(part2), 5000);
            qm_websocket_client_send_fin(event_data->client, 5000);
            qm_websocket_client_send_bin_partial(event_data->client, (const char *)binary_part1,
                                                 sizeof(binary_part1), 5000);
            qm_websocket_client_send_continuation_frame(event_data->client, (const char *)binary_part2,
                                                        sizeof(binary_part2), 5000);
            qm_websocket_client_send_fin(event_data->client, 5000);
            break;
        }

        case WEBSOCKET_EVENT_DATA:
            QM_LOGI(LOG_TAG, "data opcode=%d len=%d offset=%d total=%d fin=%d",
                    event_data->opcode, event_data->data_len, event_data->payload_offset,
                    event_data->payload_len, event_data->fin);
            if (event_data->data_ptr && event_data->data_len > 0) {
                QM_LOGI(LOG_TAG, "payload: %.*s", event_data->data_len, event_data->data_ptr);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            QM_LOGI(LOG_TAG, "disconnected");
            break;

        case WEBSOCKET_EVENT_ERROR:
            QM_LOGE(LOG_TAG, "error=%d type=%d transport=%d status=%d",
                    event_data->error_code, event_data->error_info.error_type,
                    event_data->error_info.transport_error,
                    event_data->error_info.handshake_status_code);
            break;

        default:
            break;
    }
}

static websocket_client_example_handle_t *websocket_create_and_start(const qm_websocket_client_config_t *config,
                                                                     const char *connect_message)
{
    websocket_client_example_handle_t *handle = qm_calloc(1, sizeof(*handle));
    void *client;

    if (!handle) {
        QM_LOGE(LOG_TAG, "create example handle failed");
        return NULL;
    }

    client = qm_websocket_client_init(config);
    if (!client) {
        QM_LOGE(LOG_TAG, "create client failed");
        qm_free(handle);
        return NULL;
    }

    if (qm_websocket_register_events(client, WEBSOCKET_EVENT_MAX,
                                     websocket_event_handler,
                                     (void *)connect_message) != QM_EOK) {
        QM_LOGE(LOG_TAG, "register events failed");
        qm_websocket_client_destroy(client);
        qm_free(handle);
        return NULL;
    }

    if (config->uri) {
        qm_websocket_client_set_uri(client, config->uri);
    } else if (config->host) {
        qm_websocket_client_set_host(client, config->host, config->port,
                                     config->path, config->transport_over_ssl);
    }
    qm_websocket_client_set_headers(client, "X-Qumi-Example: advanced-websocket-client\r\n");
    qm_websocket_client_append_header(client, "X-Qumi-Mode", "advanced");
    qm_websocket_client_set_ping_interval(client, 30000);
    qm_websocket_client_set_reconnect_timeout(client, 1000);
    qm_websocket_client_set_user_context(client, handle);
    QM_LOGI(LOG_TAG, "pre-start state=%d ping=%d reconnect=%d",
            qm_websocket_client_get_state(client),
            qm_websocket_client_get_ping_interval(client),
            qm_websocket_client_get_reconnect_timeout(client));

    if (qm_websocket_client_start(client) != QM_EOK) {
        QM_LOGE(LOG_TAG, "start client failed");
        qm_websocket_client_destroy(client);
        qm_free(handle);
        return NULL;
    }

    handle->client = client;
    return handle;
}

static qm_bool websocket_uri_is_secure(const char *uri)
{
    return uri && qm_strncmp(uri, "wss://", qm_strlen("wss://")) == 0;
}

static qm_bool websocket_tls_has_mutual_auth(void)
{
    return EXAMPLE_TLS_CLIENT_CERT && EXAMPLE_TLS_CLIENT_CERT_LEN > 0 &&
           EXAMPLE_TLS_CLIENT_KEY && EXAMPLE_TLS_CLIENT_KEY_LEN > 0;
}

void *websocket_client_tcp_example_start(const char *uri)
{
    qm_websocket_client_config_t websocket_cfg = {
        .uri = uri,
    };

    return websocket_create_and_start(&websocket_cfg, "hello over websocket tcp");
}

void *websocket_client_host_example_start(const char *host, int port,
                                          const char *path, qm_bool transport_over_ssl)
{
    qm_websocket_client_config_t websocket_cfg = {
        .host = host,
        .port = port,
        .path = path,
        .transport_over_ssl = transport_over_ssl,
    };
    qm_websocket_tls_config_t tls_config;

    if (transport_over_ssl && EXAMPLE_TLS_CA_CERT) {
        if (websocket_tls_has_mutual_auth()) {
            if (qm_websocket_tls_config_init(&tls_config,
                                             EXAMPLE_TLS_CA_CERT,
                                             EXAMPLE_TLS_CA_CERT_LEN,
                                             EXAMPLE_TLS_CLIENT_CERT,
                                             EXAMPLE_TLS_CLIENT_CERT_LEN,
                                             EXAMPLE_TLS_CLIENT_KEY,
                                             EXAMPLE_TLS_CLIENT_KEY_LEN) != QM_EOK) {
                QM_LOGE(LOG_TAG, "init mutual TLS config failed");
                return NULL;
            }
        } else if (qm_websocket_tls_config_init(&tls_config,
                                                EXAMPLE_TLS_CA_CERT,
                                                EXAMPLE_TLS_CA_CERT_LEN,
                                                NULL, 0, NULL, 0) != QM_EOK) {
            QM_LOGE(LOG_TAG, "init TLS config failed");
            return NULL;
        }
        websocket_cfg.tls_config = &tls_config;
    }

    return websocket_create_and_start(&websocket_cfg,
                                      transport_over_ssl ? "hello over websocket host tls" :
                                                           "hello over websocket host tcp");
}

void *websocket_client_tls_server_auth_example_start(const char *uri,
                                                     const char *ca_crt,
                                                     int ca_crt_len)
{
    qm_websocket_tls_config_t tls_config;

    if (qm_websocket_tls_config_init(&tls_config, ca_crt, ca_crt_len,
                                     NULL, 0, NULL, 0) != QM_EOK) {
        QM_LOGE(LOG_TAG, "init TLS config failed");
        return NULL;
    }

    qm_websocket_client_config_t websocket_cfg = {
        .uri = uri,
        .tls_config = &tls_config,
    };

    return websocket_create_and_start(&websocket_cfg, "hello over websocket tls");
}

void *websocket_client_tls_mutual_auth_example_start(const char *uri,
                                                     const char *ca_crt,
                                                     int ca_crt_len,
                                                     const char *client_crt,
                                                     int client_crt_len,
                                                     const char *client_key,
                                                     int client_key_len)
{
    qm_websocket_tls_config_t tls_config;

    if (qm_websocket_tls_config_init(&tls_config, ca_crt, ca_crt_len,
                                     client_crt, client_crt_len,
                                     client_key, client_key_len) != QM_EOK) {
        QM_LOGE(LOG_TAG, "init mutual TLS config failed");
        return NULL;
    }

    qm_websocket_client_config_t websocket_cfg = {
        .uri = uri,
        .tls_config = &tls_config,
    };

    return websocket_create_and_start(&websocket_cfg, "hello over websocket mutual tls");
}

void *websocket_client_example_start(const char *uri)
{
    if (websocket_uri_is_secure(uri)) {
        if (websocket_tls_has_mutual_auth()) {
            return websocket_client_tls_mutual_auth_example_start(uri,
                                                                  EXAMPLE_TLS_CA_CERT,
                                                                  EXAMPLE_TLS_CA_CERT_LEN,
                                                                  EXAMPLE_TLS_CLIENT_CERT,
                                                                  EXAMPLE_TLS_CLIENT_CERT_LEN,
                                                                  EXAMPLE_TLS_CLIENT_KEY,
                                                                  EXAMPLE_TLS_CLIENT_KEY_LEN);
        }
        return websocket_client_tls_server_auth_example_start(uri,
                                                              EXAMPLE_TLS_CA_CERT,
                                                              EXAMPLE_TLS_CA_CERT_LEN);
    }

    return websocket_client_tcp_example_start(uri);
}

int websocket_client_reconnect_example(void *example_handle)
{
    websocket_client_example_handle_t *handle = (websocket_client_example_handle_t *)example_handle;

    if (!handle || !handle->client) {
        return -QM_EINVAL;
    }

    return qm_websocket_client_reconnect(handle->client);
}

int websocket_client_graceful_close_example(void *example_handle)
{
    websocket_client_example_handle_t *handle = (websocket_client_example_handle_t *)example_handle;

    if (!handle || !handle->client) {
        return -QM_EINVAL;
    }

    return qm_websocket_client_close(handle->client, 5000);
}

void websocket_client_example_stop(void *example_handle)
{
    websocket_client_example_handle_t *handle = (websocket_client_example_handle_t *)example_handle;

    if (!handle) {
        return;
    }

    qm_websocket_unregister_events(handle->client, WEBSOCKET_EVENT_MAX, websocket_event_handler);
    qm_websocket_client_close_with_code(handle->client, 1000, "example stop", qm_strlen("example stop"), 5000);
    qm_websocket_client_stop(handle->client);
    qm_websocket_client_destroy(handle->client);
    qm_free(handle);
}

static void wifi_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                               int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t *)event_data;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch (event_id) {
        case QM_WIFI_EVENT_STA_START:
            QM_LOGI(LOG_TAG, "wifi sta start");
            if (qm_wifi_connect() != QM_EOK) {
                QM_LOGE(LOG_TAG, "wifi connect failed");
            }
            break;

        case QM_WIFI_EVENT_STA_CONNECTED:
            QM_LOGI(LOG_TAG, "wifi sta connected");
            s_wifi_retry_count = 0;
            break;

        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGI(LOG_TAG, "wifi got ip");
            if (event_info) {
                QM_LOGI(LOG_TAG, "ip:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
                QM_LOGI(LOG_TAG, "netmask:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.netmask));
                QM_LOGI(LOG_TAG, "gw:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.gw));
            }
            if (!s_example_handle) {
                if (EXAMPLE_WEBSOCKET_USE_HOST) {
                    s_example_handle = websocket_client_host_example_start(EXAMPLE_WEBSOCKET_HOST,
                                                                          EXAMPLE_WEBSOCKET_PORT,
                                                                          EXAMPLE_WEBSOCKET_PATH,
                                                                          EXAMPLE_WEBSOCKET_SSL);
                } else {
                    s_example_handle = websocket_client_example_start(EXAMPLE_WEBSOCKET_URI);
                }
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG, "wifi disconnected reason=%d",
                    event_info ? event_info->sta_disconnected.reason : -1);
            if (s_wifi_retry_count < EXAMPLE_WIFI_RETRY_MAX_COUNT) {
                s_wifi_retry_count++;
                if (qm_wifi_connect() != QM_EOK) {
                    QM_LOGE(LOG_TAG, "wifi reconnect failed");
                    break;
                }
                QM_LOGI(LOG_TAG, "retry wifi connect %d/%d",
                        s_wifi_retry_count, EXAMPLE_WIFI_RETRY_MAX_COUNT);
            }
            break;

        default:
            break;
    }
}

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;
    qm_wifi_config_t config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = qm_strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = qm_strlen(EXAMPLE_WIFI_PASSWORD),
        },
    };

    QM_LOGI(LOG_TAG, "websocket example start");

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, wifi_event_handler, NULL);

    ret = qm_wifi_init();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi init failed: %d", ret);
        return;
    }

    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi set mode failed: %d", ret);
        return;
    }

    ret = qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi set config failed: %d", ret);
        return;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi start failed: %d", ret);
        return;
    }
}
