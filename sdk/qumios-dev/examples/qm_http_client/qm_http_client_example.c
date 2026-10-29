#include "qm_http_client.h"

#include "qm.h"
#include "qm_errno.h"
#include "qm_event.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_http_client_example"

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "your_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD "your_password"
#endif

#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10
#define QM_HTTP_CLIENT_GET_URL "http://example.com/"
#define QM_HTTP_CLIENT_RANGE_URL "http://httpbin.org/range/2048"
#define QM_HTTP_CLIENT_POST_URL "http://httpbin.org/post"
#define QM_HTTP_CLIENT_PUT_URL "http://httpbin.org/put"
#define QM_HTTP_CLIENT_DELETE_URL "http://httpbin.org/delete"
#define QM_HTTP_CLIENT_BASIC_URL "http://httpbin.org/basic-auth/user/passwd"
#define QM_HTTP_CLIENT_REDIRECT_URL "http://httpbin.org/redirect-to?url=http://example.com/"
#define QM_HTTP_CLIENT_HTTPS_URL "https://example.com/"
#define QM_HTTP_CLIENT_MTLS_URL "https://example.com/"
#define QM_HTTP_CLIENT_DIGEST_URL "http://httpbin.org/digest-auth/auth/user/passwd"
#define QM_HTTP_CLIENT_DIGEST_USER "user"
#define QM_HTTP_CLIENT_DIGEST_PASS "passwd"
#define QM_HTTP_CLIENT_RESUME_OFFSET 512

#if defined(EXAMPLE_HTTP_CLIENT_CUSTOM_CA_ENABLE)
static const char s_http_client_custom_ca_crt[] =
    "-----BEGIN CERTIFICATE-----\n"
    "replace-with-server-ca-certificate-pem\n"
    "-----END CERTIFICATE-----\n";
#endif

#if defined(EXAMPLE_HTTP_CLIENT_MTLS_ENABLE)
static const char s_http_client_client_crt[] =
    "-----BEGIN CERTIFICATE-----\n"
    "replace-with-client-certificate-pem\n"
    "-----END CERTIFICATE-----\n";

static const char s_http_client_client_key[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "replace-with-client-private-key-pem\n"
    "-----END PRIVATE KEY-----\n";
#endif

typedef struct {
    int total;
    int body_total;
    uint32_t resume_offset;
    uint32_t resume_total;
    qm_bool content_range_seen;
    qm_bool content_range_valid;
} qm_http_client_example_ctx_t;

typedef struct {
    qm_http_client_example_ctx_t base;
    const char *data;
    int total_len;
    int offset;
} qm_http_client_example_upload_ctx_t;

static int s_wifi_retry_count;
static qm_bool s_http_demo_started;

static int qm_http_client_example_event_cb(const qm_http_client_event_t *event);
static void qm_http_client_example_wifi_event_handler(void *event_handler_arg,
                                                     qm_event_base_t event_base,
                                                     int32_t event_id,
                                                     void *event_data,
                                                     size_t event_data_size);

static void qm_http_client_example_init_request(qm_http_client_request_t *req, void *user)
{
    qm_memset(req, 0, sizeof(*req));
    req->timeout_ms = CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS;
    req->max_header_len = CONFIG_QM_HTTP_CLIENT_HEADER_MAX;
    req->max_body_len = CONFIG_QM_HTTP_CLIENT_BODY_MAX;
    req->user = user;
    req->on_event = qm_http_client_example_event_cb;
}

static void qm_http_client_example_init_config(qm_http_client_config_t *cfg, void *user)
{
    qm_memset(cfg, 0, sizeof(*cfg));
    cfg->timeout_ms = CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS;
    cfg->max_header_len = CONFIG_QM_HTTP_CLIENT_HEADER_MAX;
    cfg->max_body_len = CONFIG_QM_HTTP_CLIENT_BODY_MAX;
    cfg->user = user;
    cfg->on_event = qm_http_client_example_event_cb;
}

static int qm_http_client_example_parse_uint32(const char **cursor, uint32_t *value)
{
    const char *p;
    uint32_t parsed = 0;
    qm_bool has_digit = QM_FALSE;

    if (!cursor || !*cursor || !value) return -QM_EINVAL;

    p = *cursor;
    while (*p >= '0' && *p <= '9') {
        parsed = parsed * 10 + (uint32_t)(*p - '0');
        has_digit = QM_TRUE;
        p++;
    }
    if (!has_digit) return -QM_EINVAL;

    *cursor = p;
    *value = parsed;
    return QM_EOK;
}

static int qm_http_client_example_parse_content_range(const char *value,
                                                      uint32_t expected_start,
                                                      uint32_t *total_size)
{
    const char *p;
    uint32_t start;
    uint32_t end;
    uint32_t total = 0;
    int ret;

    if (!value) return -QM_EINVAL;
    if (qm_strncasecmp(value, "bytes ", qm_strlen("bytes ")) != 0) {
        return -QM_EINVAL;
    }

    p = value + qm_strlen("bytes ");
    ret = qm_http_client_example_parse_uint32(&p, &start);
    if (ret != QM_EOK || *p != '-') return -QM_EINVAL;
    p++;

    ret = qm_http_client_example_parse_uint32(&p, &end);
    if (ret != QM_EOK || *p != '/') return -QM_EINVAL;
    p++;

    if (*p == '*') {
        p++;
    } else {
        ret = qm_http_client_example_parse_uint32(&p, &total);
        if (ret != QM_EOK) return -QM_EINVAL;
    }

    if (start != expected_start || end < start) return -QM_EINVAL;
    if (total_size) *total_size = total;
    return QM_EOK;
}

static int qm_http_client_example_event_cb(const qm_http_client_event_t *event)
{
    qm_http_client_example_ctx_t *ctx;

    if (!event) return QM_EINVAL;
    ctx = (qm_http_client_example_ctx_t *)event->user;

    switch (event->event_id) {
        case QM_HTTP_CLIENT_EVENT_CONNECTED:
            QM_LOGI(LOG_TAG, "connected");
            break;
        case QM_HTTP_CLIENT_EVENT_HEADER:
            QM_LOGI(LOG_TAG, "header: %s: %s",
                    event->header_name ? event->header_name : "",
                    event->header_value ? event->header_value : "");
            if (ctx && event->header_name && event->header_value &&
                qm_strcasecmp(event->header_name, "Content-Range") == 0) {
                ctx->content_range_seen = QM_TRUE;
                if (qm_http_client_example_parse_content_range(event->header_value,
                                                               ctx->resume_offset,
                                                               &ctx->resume_total) == QM_EOK) {
                    ctx->content_range_valid = QM_TRUE;
                }
                QM_LOGI(LOG_TAG, "content range valid=%d total=%u",
                        ctx->content_range_valid, (unsigned int)ctx->resume_total);
            }
            break;
        case QM_HTTP_CLIENT_EVENT_DATA:
            if (ctx) {
                ctx->total += event->data_len;
            }
            QM_LOGI(LOG_TAG, "data chunk len=%d", event->data_len);
            if (event->data && event->data_len > 0) {
                QM_LOGI(LOG_TAG, "data: %.*s",
                        event->data_len > 96 ? 96 : event->data_len, event->data);
            }
            break;
        case QM_HTTP_CLIENT_EVENT_FINISH:
            QM_LOGI(LOG_TAG, "finish total=%d", ctx ? ctx->total : 0);
            break;
        case QM_HTTP_CLIENT_EVENT_DISCONNECTED:
            QM_LOGI(LOG_TAG, "disconnected");
            break;
        case QM_HTTP_CLIENT_EVENT_ERROR:
            QM_LOGE(LOG_TAG, "error code=%d status=%d",
                    event->error_code, event->status_code);
            break;
        case QM_HTTP_CLIENT_EVENT_REDIRECT:
            QM_LOGI(LOG_TAG, "redirect -> %s",
                    event->redirect_url ? event->redirect_url : "");
            break;
        default:
            break;
    }

    return QM_EOK;
}

static int qm_http_client_example_body_cb(const char *data, int len, void *user)
{
    qm_http_client_example_ctx_t *ctx = (qm_http_client_example_ctx_t *)user;

    if (!data || len <= 0) return QM_EOK;
    if (ctx) {
        ctx->body_total += len;
    }
    QM_LOGI(LOG_TAG, "body callback len=%d", len);
    return QM_EOK;
}

static int qm_http_client_example_upload_body_cb(char *buffer, int max_len, void *user)
{
    qm_http_client_example_upload_ctx_t *ctx = (qm_http_client_example_upload_ctx_t *)user;
    int left;
    int n;

    if (!buffer || max_len <= 0 || !ctx || !ctx->data) return QM_EINVAL;

    left = ctx->total_len - ctx->offset;
    n = left < max_len ? left : max_len;
    if (n <= 0) return 0;

    qm_memcpy(buffer, ctx->data + ctx->offset, n);
    ctx->offset += n;
    return n;
}

static int qm_http_client_example_upload_body_reset(void *user)
{
    qm_http_client_example_upload_ctx_t *ctx = (qm_http_client_example_upload_ctx_t *)user;

    if (!ctx) return QM_EINVAL;
    ctx->offset = 0;
    return QM_EOK;
}

static void qm_http_client_example_log_response(const char *name, int ret,
                                                const qm_http_client_response_t *resp,
                                                const qm_http_client_example_ctx_t *ctx)
{
    if (!name || !resp) return;

    QM_LOGI(LOG_TAG,
            "%s ret=%d status=%d total=%d on_body=%d len_known=%d content_len=%u chunked=%d keep_alive=%d",
            name, ret, resp->status_code,
            ctx ? ctx->total : 0,
            ctx ? ctx->body_total : 0,
            resp->content_length_known,
            (unsigned int)resp->content_length,
            resp->chunked,
            resp->keep_alive);
}

static void qm_http_client_example_log_client(const char *name,
                                              qm_http_client_handle_t client,
                                              const qm_http_client_example_ctx_t *ctx)
{
    const char *location;

    if (!name || !client) return;
    location = qm_http_client_get_location(client);

    QM_LOGI(LOG_TAG,
            "%s status=%d total=%d on_body=%d len_known=%d content_len=%u chunked=%d complete=%d user=%p location=%s",
            name,
            qm_http_client_get_status_code(client),
            ctx ? ctx->total : 0,
            ctx ? ctx->body_total : 0,
            qm_http_client_has_content_length(client),
            (unsigned int)qm_http_client_get_content_length(client),
            qm_http_client_is_chunked_response(client),
            qm_http_client_is_complete_data_received(client),
            qm_http_client_get_user_data(client),
            location ? location : "");
}

void qm_http_client_example_start(void)
{
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_GET;
    req.url = QM_HTTP_CLIENT_GET_URL;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute get", ret, &resp, &ctx);
}

void qm_http_client_post_example_start(void)
{
    static const char body[] = "{\"device\":\"esp32\",\"mode\":\"fixed-body\"}";
    static const qm_http_header_t headers[] = {
        {"X-Example-Mode", "execute-fixed-body"},
    };
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_POST;
    req.url = QM_HTTP_CLIENT_POST_URL;
    req.body = body;
    req.body_len = (int)qm_strlen(body);
    req.content_type = "application/json";
    req.user_agent = "qumi-http-client-example/1.0";
    req.headers = headers;
    req.header_count = sizeof(headers) / sizeof(headers[0]);
    req.on_body = qm_http_client_example_body_cb;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute post", ret, &resp, &ctx);
}

void qm_http_client_stream_body_example_start(void)
{
    static const char body[] =
        "{\"device\":\"esp32\",\"mode\":\"stream-body\",\"payload\":\"abcdefghijklmnopqrstuvwxyz0123456789\"}";
    static const qm_http_header_t headers[] = {
        {"X-Example-Mode", "execute-stream-body"},
    };
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_upload_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    ctx.data = body;
    ctx.total_len = (int)qm_strlen(body);

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_POST;
    req.url = QM_HTTP_CLIENT_POST_URL;
    req.body_len = ctx.total_len;
    req.body_cb = qm_http_client_example_upload_body_cb;
    req.body_reset = qm_http_client_example_upload_body_reset;
    req.content_type = "application/json";
    req.headers = headers;
    req.header_count = sizeof(headers) / sizeof(headers[0]);
    req.on_body = qm_http_client_example_body_cb;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute stream body", ret, &resp, &ctx.base);
}

void qm_http_client_handle_perform_example_start(void)
{
    static const qm_http_header_t headers[] = {
        {"X-Example-Mode", "handle-perform"},
    };
    qm_http_client_config_t cfg;
    qm_http_client_handle_t client;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&ctx, 0, sizeof(ctx));
    qm_http_client_example_init_config(&cfg, &ctx);
    cfg.url = QM_HTTP_CLIENT_BASIC_URL;
    cfg.headers = headers;
    cfg.header_count = sizeof(headers) / sizeof(headers[0]);
    cfg.keep_alive = QM_TRUE;
    cfg.user_agent = "qumi-http-client-example/1.0";

    client = qm_http_client_init(&cfg);
    if (!client) {
        QM_LOGE(LOG_TAG, "handle perform init failed");
        return;
    }

    ret = qm_http_client_set_user_data(client, &ctx);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_method(client, QM_HTTP_GET);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_username(client, QM_HTTP_CLIENT_DIGEST_USER);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_password(client, QM_HTTP_CLIENT_DIGEST_PASS);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_auth_type(client, QM_HTTP_AUTH_TYPE_BASIC);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "X-Trace-Id", "perform-example");
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "X-Remove-Me", "1");
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_delete_header(client, "X-Remove-Me");
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_perform(client, NULL, 0, qm_http_client_example_body_cb);
    qm_http_client_example_log_client("handle perform", client, &ctx);

cleanup:
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "handle perform ret=%d", ret);
    }
    qm_http_client_close(client);
    qm_http_client_cleanup(client);
}

void qm_http_client_manual_flow_example_start(void)
{
    static const char body[] = "{\"device\":\"esp32\",\"mode\":\"manual-flow\"}";
    qm_http_client_config_t cfg;
    qm_http_client_handle_t client;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&ctx, 0, sizeof(ctx));
    qm_http_client_example_init_config(&cfg, &ctx);
    cfg.keep_alive = QM_TRUE;

    client = qm_http_client_init(&cfg);
    if (!client) {
        QM_LOGE(LOG_TAG, "manual flow init failed");
        return;
    }

    ret = qm_http_client_set_url(client, QM_HTTP_CLIENT_POST_URL);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_method(client, QM_HTTP_POST);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_user_data(client, &ctx);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "Content-Type", "application/json");
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "X-Example-Mode", "manual-flow");
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "X-Delete-Me", "1");
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_delete_header(client, "X-Delete-Me");
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_open(client, (int)qm_strlen(body));
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_write(client, body, (int)qm_strlen(body));
    if (ret < 0) goto cleanup;
    ret = qm_http_client_fetch_headers(client);
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_drain_response_body(client);
    if (ret != QM_EOK) goto cleanup;

    QM_LOGI(LOG_TAG, "manual flow drained unread response body");
    qm_http_client_example_log_client("manual flow", client, &ctx);
    ret = qm_http_client_close(client);
    if (ret != QM_EOK) goto cleanup;

cleanup:
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "manual flow ret=%d", ret);
    }
    qm_http_client_cleanup(client);
}

void qm_http_client_manual_read_example_start(void)
{
    qm_http_client_config_t cfg;
    qm_http_client_handle_t client;
    qm_http_client_example_ctx_t ctx;
    char buffer[128];
    int ret = QM_EOK;
    int read_len;

    qm_memset(&ctx, 0, sizeof(ctx));
    qm_http_client_example_init_config(&cfg, &ctx);
    cfg.url = QM_HTTP_CLIENT_GET_URL;
    cfg.method = QM_HTTP_GET;
    cfg.keep_alive = QM_TRUE;

    client = qm_http_client_init(&cfg);
    if (!client) {
        QM_LOGE(LOG_TAG, "manual read init failed");
        return;
    }

    ret = qm_http_client_set_header(client, "X-Example-Mode", "manual-read");
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_open(client, 0);
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_fetch_headers(client);
    if (ret != QM_EOK) goto cleanup;

    do {
        read_len = qm_http_client_read(client, buffer, sizeof(buffer));
        if (read_len > 0) {
            ctx.body_total += read_len;
            QM_LOGI(LOG_TAG, "manual read chunk len=%d", read_len);
            QM_LOGI(LOG_TAG, "manual read data: %.*s",
                    read_len > 96 ? 96 : read_len, buffer);
        }
    } while (read_len > 0);

    if (read_len < 0) {
        ret = read_len;
        goto cleanup;
    }

    qm_http_client_example_log_client("manual read", client, &ctx);

cleanup:
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "manual read ret=%d", ret);
    }
    qm_http_client_close(client);
    qm_http_client_cleanup(client);
}

void qm_http_client_resume_download_example_start(void)
{
    qm_http_client_config_t cfg;
    qm_http_client_handle_t client;
    qm_http_client_example_ctx_t ctx;
    char range_value[64];
    char buffer[128];
    uint32_t next_offset;
    int ret = QM_EOK;
    int read_len;

    qm_memset(&ctx, 0, sizeof(ctx));
    ctx.resume_offset = QM_HTTP_CLIENT_RESUME_OFFSET;

    if (qm_snprintf(range_value, sizeof(range_value), "bytes=%u-",
                    (unsigned int)ctx.resume_offset) < 0) {
        QM_LOGE(LOG_TAG, "resume range format failed");
        return;
    }

    qm_http_client_example_init_config(&cfg, &ctx);
    cfg.url = QM_HTTP_CLIENT_RANGE_URL;
    cfg.method = QM_HTTP_GET;
    cfg.keep_alive = QM_TRUE;

    client = qm_http_client_init(&cfg);
    if (!client) {
        QM_LOGE(LOG_TAG, "resume download init failed");
        return;
    }

    ret = qm_http_client_set_header(client, "Range", range_value);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_set_header(client, "X-Example-Mode", "resume-download");
    if (ret != QM_EOK) goto cleanup;

    ret = qm_http_client_open(client, 0);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_http_client_fetch_headers(client);
    if (ret != QM_EOK) goto cleanup;

    do {
        read_len = qm_http_client_read(client, buffer, sizeof(buffer));
        if (read_len > 0) {
            ctx.body_total += read_len;
            QM_LOGI(LOG_TAG, "resume read chunk len=%d", read_len);
        }
    } while (read_len > 0);

    if (read_len < 0) {
        ret = read_len;
        goto cleanup;
    }

    if (qm_http_client_get_status_code(client) == 206) {
        if (!ctx.content_range_seen || !ctx.content_range_valid) {
            QM_LOGE(LOG_TAG, "resume response missing or invalid Content-Range");
            ret = -QM_EINVAL;
            goto cleanup;
        }
        next_offset = ctx.resume_offset + (uint32_t)ctx.total;
        QM_LOGI(LOG_TAG, "resume download ok saved_offset=%u total=%u",
                (unsigned int)next_offset, (unsigned int)ctx.resume_total);
    } else if (qm_http_client_get_status_code(client) == 200) {
        next_offset = (uint32_t)ctx.total;
        QM_LOGW(LOG_TAG, "server ignored Range, restart saved_offset=%u",
                (unsigned int)next_offset);
    } else {
        QM_LOGE(LOG_TAG, "resume download unexpected status=%d",
                qm_http_client_get_status_code(client));
        ret = -QM_EIO;
        goto cleanup;
    }

    qm_http_client_example_log_client("resume download", client, &ctx);

cleanup:
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "resume download ret=%d", ret);
    }
    qm_http_client_close(client);
    qm_http_client_cleanup(client);
}

void qm_http_client_redirect_example_start(void)
{
    qm_http_client_config_t cfg;
    qm_http_client_handle_t client;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&ctx, 0, sizeof(ctx));
    qm_http_client_example_init_config(&cfg, &ctx);
    cfg.url = QM_HTTP_CLIENT_REDIRECT_URL;
    cfg.disable_auto_redirect = QM_TRUE;
    cfg.max_redirection_count = 1;
    cfg.keep_alive = QM_TRUE;

    client = qm_http_client_init(&cfg);
    if (!client) {
        QM_LOGE(LOG_TAG, "redirect example init failed");
        return;
    }

    ret = qm_http_client_perform(client, NULL, 0, NULL);
    qm_http_client_example_log_client("redirect", client, &ctx);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "redirect ret=%d", ret);
    }

    qm_http_client_close(client);
    qm_http_client_cleanup(client);
}

static void qm_http_client_https_oneway_request(const char *name,
                                                const char *ca_crt,
                                                int ca_crt_len)
{
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    qm_http_tls_config_t tls;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    ret = qm_http_client_tls_config_init(&tls,
                                         ca_crt,
                                         ca_crt_len,
                                         NULL, 0,
                                         NULL, 0);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s tls init ret=%d", name, ret);
        return;
    }

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_GET;
    req.url = QM_HTTP_CLIENT_HTTPS_URL;
    req.tls = &tls;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response(name, ret, &resp, &ctx);
}

void qm_http_client_https_oneway_default_ca_example_start(void)
{
    qm_http_client_https_oneway_request("execute https oneway default ca", NULL, 0);
}

void qm_http_client_https_oneway_custom_ca_example_start(void)
{
#if defined(EXAMPLE_HTTP_CLIENT_CUSTOM_CA_ENABLE)
    qm_http_client_https_oneway_request("execute https oneway custom ca",
                                        s_http_client_custom_ca_crt,
                                        (int)qm_strlen(s_http_client_custom_ca_crt));
#else
    QM_LOGW(LOG_TAG, "oneway custom CA example disabled");
#endif
}

void qm_http_client_https_oneway_example_start(void)
{
    qm_http_client_https_oneway_default_ca_example_start();
}

void qm_http_client_https_mutual_example_start(void)
{
#if defined(EXAMPLE_HTTP_CLIENT_MTLS_ENABLE)
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    qm_http_tls_config_t tls;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    ret = qm_http_client_tls_config_init(&tls,
#if defined(EXAMPLE_HTTP_CLIENT_CUSTOM_CA_ENABLE)
                                         s_http_client_custom_ca_crt,
                                         (int)qm_strlen(s_http_client_custom_ca_crt),
#else
                                         NULL, 0,
#endif
                                         s_http_client_client_crt,
                                         (int)qm_strlen(s_http_client_client_crt),
                                         s_http_client_client_key,
                                         (int)qm_strlen(s_http_client_client_key));
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "mutual tls init ret=%d", ret);
        return;
    }

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_GET;
    req.url = QM_HTTP_CLIENT_MTLS_URL;
    req.tls = &tls;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute https mutual", ret, &resp, &ctx);
#else
    QM_LOGW(LOG_TAG, "mutual TLS example disabled");
#endif
}

void qm_http_client_https_example_start(void)
{
    qm_http_client_https_oneway_example_start();
}

void qm_http_client_head_example_start(void)
{
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_HEAD;
    req.url = QM_HTTP_CLIENT_GET_URL;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute head", ret, &resp, &ctx);
}

void qm_http_client_put_delete_example_start(void)
{
    static const char put_body[] = "{\"device\":\"esp32\",\"mode\":\"put\"}";
    static const qm_http_header_t headers[] = {
        {"X-Example-Mode", "put-delete"},
    };
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_PUT;
    req.url = QM_HTTP_CLIENT_PUT_URL;
    req.body = put_body;
    req.body_len = (int)qm_strlen(put_body);
    req.content_type = "application/json";
    req.headers = headers;
    req.header_count = sizeof(headers) / sizeof(headers[0]);

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute put", ret, &resp, &ctx);

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));
    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_DELETE;
    req.url = QM_HTTP_CLIENT_DELETE_URL;
    req.headers = headers;
    req.header_count = sizeof(headers) / sizeof(headers[0]);

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute delete", ret, &resp, &ctx);
}

void qm_http_client_digest_example_start(void)
{
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    qm_http_client_example_ctx_t ctx;
    int ret;

    qm_memset(&resp, 0, sizeof(resp));
    qm_memset(&ctx, 0, sizeof(ctx));

    qm_http_client_example_init_request(&req, &ctx);
    req.method = QM_HTTP_GET;
    req.url = QM_HTTP_CLIENT_DIGEST_URL;
    req.username = QM_HTTP_CLIENT_DIGEST_USER;
    req.password = QM_HTTP_CLIENT_DIGEST_PASS;
    req.auth_type = QM_HTTP_AUTH_TYPE_DIGEST;

    ret = qm_http_client_execute(&req, &resp);
    qm_http_client_example_log_response("execute digest", ret, &resp, &ctx);
}

void qm_http_client_all_example_start(void)
{
    qm_http_client_example_start();
    qm_http_client_post_example_start();
    qm_http_client_stream_body_example_start();
    qm_http_client_handle_perform_example_start();
    qm_http_client_manual_flow_example_start();
    qm_http_client_manual_read_example_start();
    qm_http_client_resume_download_example_start();
    qm_http_client_redirect_example_start();
    qm_http_client_https_oneway_default_ca_example_start();
    /*qm_http_client_https_oneway_custom_ca_example_start();
    qm_http_client_https_mutual_example_start();*/
    qm_http_client_head_example_start();
    qm_http_client_put_delete_example_start();
    qm_http_client_digest_example_start();
}

static void qm_http_client_example_wifi_event_handler(void *event_handler_arg,
                                                     qm_event_base_t event_base,
                                                     int32_t event_id,
                                                     void *event_data,
                                                     size_t event_data_size)
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
            if (!event_info) {
                QM_LOGE(LOG_TAG, "wifi got ip event missing info");
                break;
            }
            QM_LOGI(LOG_TAG, "wifi got ip");
            QM_LOGI(LOG_TAG, "ip:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
            QM_LOGI(LOG_TAG, "netmask:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.netmask));
            QM_LOGI(LOG_TAG, "gw:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.gw));
            if (!s_http_demo_started) {
                s_http_demo_started = QM_TRUE;
                qm_http_client_all_example_start();
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            s_http_demo_started = QM_FALSE;
            if (!event_info) {
                QM_LOGW(LOG_TAG, "wifi disconnected");
                break;
            }
            QM_LOGW(LOG_TAG, "wifi disconnected reason=%d", event_info->sta_disconnected.reason);
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

void qm_http_client_wifi_demo_start(void)
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

    QM_LOGI(LOG_TAG, "http client wifi demo start");

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                   qm_http_client_example_wifi_event_handler, NULL);

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
