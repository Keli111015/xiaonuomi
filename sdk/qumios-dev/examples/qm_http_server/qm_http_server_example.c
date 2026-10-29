#include "qm_http_server.h"

#include "qm_event.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_http_server_example"
#define QM_HTTP_SERVER_EXAMPLE_USE_HTTPS 0

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "your_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD "your_password"
#endif

#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10

static int s_wifi_retry_count;
static qm_http_server_t s_http_server;

#if QM_HTTP_SERVER_EXAMPLE_USE_HTTPS
extern const unsigned char servercert_start[] asm("_binary_servercert_pem_start");
extern const unsigned char servercert_end[] asm("_binary_servercert_pem_end");
extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
extern const unsigned char prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");
#endif

static const char *qm_http_server_example_method_name(qm_http_method_t method)
{
    switch (method) {
    case QM_HTTP_GET: return "GET";
    case QM_HTTP_POST: return "POST";
    case QM_HTTP_PUT: return "PUT";
    case QM_HTTP_DELETE: return "DELETE";
    case QM_HTTP_HEAD: return "HEAD";
    case QM_HTTP_ANY: return "ANY";
    default: return "UNKNOWN";
    }
}

static int qm_http_server_example_ping(qm_http_request_t *req, qm_http_response_t *resp)
{
    char id[24] = {0};
    const char *body = "{\"ok\":true}";
    int ret;

    if (req && req->query &&
        qm_http_query_get(req->query, "id", id, sizeof(id)) == QM_EOK) {
        QM_LOGI(LOG_TAG, "query id=%s", id);
    }

    ret = qm_http_response_set_header(resp, "Cache-Control", "no-store");
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_http_response_send(resp, 200, "application/json", body, (int)qm_strlen(body));
}

static int qm_http_server_example_head(qm_http_request_t *req, qm_http_response_t *resp)
{
    int ret;

    (void)req;

    ret = qm_http_response_set_header(resp, "X-Qumi-Example", "head");
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_http_response_send(resp, 204, NULL, NULL, 0);
}

static int qm_http_server_example_echo(qm_http_request_t *req, qm_http_response_t *resp)
{
    char content_type[64] = {0};
    const char *response_type = "application/octet-stream";
    char *body = NULL;
    int body_len = 0;
    int ret;

    if (qm_http_request_get_header(req, "Content-Type", content_type,
                                   sizeof(content_type)) == QM_EOK) {
        response_type = content_type;
    }

    ret = qm_http_request_read_all(req, &body, &body_len);
    if (ret != QM_EOK) {
        const char *error_body = "{\"ok\":false,\"error\":\"read body failed\"}";
        return qm_http_response_send(resp, 400, "application/json",
                                     error_body, (int)qm_strlen(error_body));
    }

    ret = qm_http_response_set_header(resp, "X-Qumi-Body-Mode", "read_all");
    if (ret == QM_EOK) {
        ret = qm_http_response_send(resp, 200, response_type, body, body_len);
    }
    qm_free(body);
    return ret;
}

static int qm_http_server_example_upload(qm_http_request_t *req, qm_http_response_t *resp)
{
    char chunk[64];
    char body[96];
    int total = 0;
    int chunks = 0;
    int len;
    int ret;

    for (;;) {
        ret = qm_http_request_read(req, chunk, sizeof(chunk));
        if (ret < 0) {
            const char *error_body = "{\"ok\":false,\"error\":\"stream read failed\"}";
            return qm_http_response_send(resp, 400, "application/json",
                                         error_body, (int)qm_strlen(error_body));
        }
        if (ret == 0) {
            break;
        }
        total += ret;
        chunks++;
    }

    ret = qm_http_response_set_header(resp, "X-Qumi-Body-Mode", "stream");
    if (ret != QM_EOK) {
        return ret;
    }
    len = qm_snprintf(body, sizeof(body),
                      "{\"ok\":true,\"content_length\":%d,\"read\":%d,\"chunks\":%d}",
                      req ? req->content_length : 0, total, chunks);
    if (len <= 0 || len >= (int)sizeof(body)) {
        return -QM_EIO;
    }
    return qm_http_response_send(resp, 200, "application/json", body, len);
}

static int qm_http_server_example_stream(qm_http_request_t *req, qm_http_response_t *resp)
{
    int ret;

    (void)req;

    ret = qm_http_response_set_header(resp, "X-Qumi-Body-Mode", "chunked");
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_begin_chunked(resp, 200, "text/plain");
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_write_chunk(resp, "hello ", 6);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_write_chunk(resp, "from qumi http server\n", 22);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_http_response_end_chunked(resp);
}

static int qm_http_server_example_resource(qm_http_request_t *req, qm_http_response_t *resp)
{
    char body[96];
    const char *action = "handled";
    uint32_t path_len = req && req->path ? qm_strlen(req->path) : 0;
    int len;
    int ret;

    if (req && req->method == QM_HTTP_PUT) {
        action = "updated";
    } else if (req && req->method == QM_HTTP_DELETE) {
        action = "deleted";
    }

    ret = qm_http_response_set_header(resp, "X-Qumi-Route", "resource");
    if (ret != QM_EOK) {
        return ret;
    }
    len = qm_snprintf(body, sizeof(body),
                      "{\"ok\":true,\"action\":\"%s\",\"path_len\":%u}",
                      action, (unsigned int)path_len);
    if (len <= 0 || len >= (int)sizeof(body)) {
        return -QM_EIO;
    }
    return qm_http_response_send(resp, 200, "application/json", body, len);
}

static int qm_http_server_example_any(qm_http_request_t *req, qm_http_response_t *resp)
{
    char body[128];
    const char *method = req ? qm_http_server_example_method_name(req->method) : "UNKNOWN";
    uint32_t uri_len = req && req->uri ? qm_strlen(req->uri) : 0;
    int len;
    int ret;

    ret = qm_http_response_set_header(resp, "X-Qumi-Method", method);
    if (ret != QM_EOK) {
        return ret;
    }
    len = qm_snprintf(body, sizeof(body),
                      "{\"ok\":true,\"method\":\"%s\",\"uri_len\":%u,\"content_length\":%d}",
                      method, (unsigned int)uri_len, req ? req->content_length : 0);
    if (len <= 0 || len >= (int)sizeof(body)) {
        return -QM_EIO;
    }
    return qm_http_response_send(resp, 200, "application/json", body, len);
}

static int qm_http_server_example_error(qm_http_request_t *req, qm_http_response_t *resp, int status_code)
{
    char body[96];
    int len;
    int ret;

    (void)req;

    ret = qm_http_response_set_header(resp, "X-Qumi-Error", "true");
    if (ret != QM_EOK) {
        return ret;
    }
    len = qm_snprintf(body, sizeof(body), "{\"ok\":false,\"status\":%d}", status_code);
    if (len < 0) {
        return -QM_EIO;
    }
    if (len >= (int)sizeof(body)) {
        len = (int)sizeof(body) - 1;
    }
    return qm_http_response_send(resp, status_code, "application/json", body, len);
}

qm_http_server_t qm_http_server_example_start(void)
{
    static const qm_http_route_t routes[] = {
        {QM_HTTP_GET, "/api/v1/ping", qm_http_server_example_ping},
        {QM_HTTP_HEAD, "/api/v1/ping", qm_http_server_example_head},
        {QM_HTTP_POST, "/api/v1/echo", qm_http_server_example_echo},
        {QM_HTTP_POST, "/api/v1/upload", qm_http_server_example_upload},
        {QM_HTTP_GET, "/api/v1/stream", qm_http_server_example_stream},
        {QM_HTTP_PUT, "/api/v1/resource/*", qm_http_server_example_resource},
        {QM_HTTP_DELETE, "/api/v1/resource/*", qm_http_server_example_resource},
        {QM_HTTP_ANY, "/api/v1/any", qm_http_server_example_any},
    };
    qm_http_server_config_t cfg;
    qm_http_server_t server;

    qm_memset(&cfg, 0, sizeof(cfg));
    cfg.port = 8080;
    cfg.routes = routes;
    cfg.route_count = sizeof(routes) / sizeof(routes[0]);
    cfg.error_handler = qm_http_server_example_error;
    cfg.timeout_ms = 5000;
    cfg.max_header_len = 1024;
    cfg.max_body_len = 4096;
    cfg.keep_alive_enable = QM_TRUE;
#if QM_HTTP_SERVER_EXAMPLE_USE_HTTPS
    cfg.port = 443;
    cfg.scheme = QM_HTTP_SERVER_SCHEME_HTTPS;
    cfg.https_cert_pem = servercert_start;
    cfg.https_cert_len = servercert_end - servercert_start;
    cfg.https_key_pem = prvtkey_pem_start;
    cfg.https_key_len = prvtkey_pem_end - prvtkey_pem_start;
#endif

    server = qm_http_server_create(&cfg);
    if (!server) {
        QM_LOGE(LOG_TAG, "create failed");
        return NULL;
    }

    if (qm_http_server_start(server) != QM_EOK) {
        QM_LOGE(LOG_TAG, "start failed");
        qm_http_server_destroy(server);
        return NULL;
    }

    QM_LOGI(LOG_TAG, "started port=%u", cfg.port);
    return server;
}

void qm_http_server_example_stop(qm_http_server_t server)
{
    if (!server) {
        return;
    }
    qm_http_server_stop(server);
    qm_http_server_destroy(server);
    if (server == s_http_server) {
        s_http_server = NULL;
    }
}

static void qm_http_server_example_wifi_event_handler(void *event_handler_arg,
                                                     qm_event_base_t event_base,
                                                     int32_t event_id,
                                                     void *event_data,
                                                     size_t event_data_size)
{
    qm_wifi_event_info_t *event_info;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    event_info = (qm_wifi_event_info_t *)event_data;

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
        if (!event_info) {
            QM_LOGE(LOG_TAG, "wifi got ip event missing info");
            break;
        }
        QM_LOGI(LOG_TAG, "wifi got ip");
        QM_LOGI(LOG_TAG, "ip:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
        QM_LOGI(LOG_TAG, "netmask:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.netmask));
        QM_LOGI(LOG_TAG, "gw:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.gw));
        if (!s_http_server) {
            s_http_server = qm_http_server_example_start();
        }
        break;

    case QM_WIFI_EVENT_STA_DISCONNECTED:
        QM_LOGW(LOG_TAG, "wifi disconnected reason=%d",
                event_info ? event_info->sta_disconnected.reason : -1);
        if (s_http_server) {
            qm_http_server_example_stop(s_http_server);
        }
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

void qm_http_server_wifi_demo_start(void)
{
    int ret;
    qm_wifi_config_t config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = qm_strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = qm_strlen(EXAMPLE_WIFI_PASSWORD),
        },
    };

    QM_LOGI(LOG_TAG, "http server wifi demo start");

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                   qm_http_server_example_wifi_event_handler, NULL);

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
