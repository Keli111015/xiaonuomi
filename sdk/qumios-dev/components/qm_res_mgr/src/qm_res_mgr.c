#include "qm_res_mgr.h"

#include "cJSON.h"
#include "qm_kernel.h"
#if defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
#include "qm_iot_api.h"
#endif
#include "qm_http_client.h"
#include "qm_res_mgr_log.h"
#include "qm_string.h"

#define LOG_TAG "qm_res_mgr"

#ifndef CONFIG_QM_RES_MGR_SUPPORT
#define CONFIG_QM_RES_MGR_SUPPORT 1
#endif

#if ((CONFIG_QM_RES_MGR_HTTP_SUPPORT) && (CONFIG_QM_RES_MGR_MQTT_SUPPORT)) || \
    (!(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && !(CONFIG_QM_RES_MGR_MQTT_SUPPORT))
#error "qm_res_mgr metadata channel must choose exactly one of CONFIG_QM_RES_MGR_HTTP_SUPPORT or CONFIG_QM_RES_MGR_MQTT_SUPPORT"
#endif

#define QM_RES_MGR_HTTP_TIMEOUT_MS_DEFAULT 15000
#define QM_RES_MGR_HTTP_RX_BUF_SIZE_DEFAULT 1024
#define QM_RES_MGR_HTTP_HEADER_MAX_DEFAULT 2048
#define QM_RES_MGR_API_BODY_MAX_DEFAULT 16384

#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
#ifndef CONFIG_QM_RES_MGR_TIMEOUT_MS
#define CONFIG_QM_RES_MGR_TIMEOUT_MS QM_RES_MGR_HTTP_TIMEOUT_MS_DEFAULT
#endif
#define QM_RES_MGR_HTTP_TIMEOUT_MS CONFIG_QM_RES_MGR_TIMEOUT_MS

#ifndef CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE
#define CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE QM_RES_MGR_HTTP_RX_BUF_SIZE_DEFAULT
#endif
#define QM_RES_MGR_HTTP_RX_BUF_SIZE CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE

#ifndef CONFIG_QM_RES_MGR_HTTP_HEADER_MAX
#define CONFIG_QM_RES_MGR_HTTP_HEADER_MAX QM_RES_MGR_HTTP_HEADER_MAX_DEFAULT
#endif
#define QM_RES_MGR_HTTP_HEADER_MAX CONFIG_QM_RES_MGR_HTTP_HEADER_MAX

#ifndef CONFIG_QM_RES_MGR_API_BODY_MAX
#define CONFIG_QM_RES_MGR_API_BODY_MAX QM_RES_MGR_API_BODY_MAX_DEFAULT
#endif
#define QM_RES_MGR_API_BODY_MAX CONFIG_QM_RES_MGR_API_BODY_MAX
#else
#define QM_RES_MGR_HTTP_TIMEOUT_MS QM_RES_MGR_HTTP_TIMEOUT_MS_DEFAULT
#define QM_RES_MGR_HTTP_RX_BUF_SIZE QM_RES_MGR_HTTP_RX_BUF_SIZE_DEFAULT
#define QM_RES_MGR_HTTP_HEADER_MAX QM_RES_MGR_HTTP_HEADER_MAX_DEFAULT
#endif

#ifndef CONFIG_QM_RES_MGR_SEGMENT_SIZE
#define CONFIG_QM_RES_MGR_SEGMENT_SIZE (1024 * 1024)
#endif

#ifndef CONFIG_QM_RES_MGR_RETRY_MAX
#define CONFIG_QM_RES_MGR_RETRY_MAX 3
#endif

#ifndef CONFIG_QM_RES_MGR_PAGE_SIZE
#define CONFIG_QM_RES_MGR_PAGE_SIZE 10
#endif

#define QM_RES_MGR_API_LIST_PATH   "/agent-device/resource/list"
#define QM_RES_MGR_API_DETAIL_PATH "/agent-device/resource/detail"
#define QM_RES_MGR_API_CATEGORY_PATH "/agent-device/resource/getCategory"
#define QM_RES_MGR_API_CHILDREN_CATEGORY_PATH "/agent-device/resource/getChildrenCategory"
#define QM_RES_MGR_API_BASE_URL_DEV  "https://api-dev.ljyd.cn"
#define QM_RES_MGR_API_BASE_URL_TEST "https://api-test.ljyd.cn"
#define QM_RES_MGR_API_BASE_URL_PROD "https://api.ljyd.cn"
#define QM_RES_MGR_MQTT_METHOD_GET_RESOURCE "getResource"
#define QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_REPLY "getResourceReply"
#define QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_DETAIL "getResourceDetail"
#define QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_DETAIL_REPLY "getResourceDetailReply"
#define QM_RES_MGR_MQTT_METHOD_GET_CATEGORY "getCategory"
#define QM_RES_MGR_MQTT_METHOD_GET_CATEGORY_REPLY "getCategoryReply"
#define QM_RES_MGR_MQTT_METHOD_GET_CHILDREN_CATEGORY "getChildrenCategory"
#define QM_RES_MGR_MQTT_METHOD_GET_CHILDREN_CATEGORY_REPLY "getChildrenCategoryReply"
#ifndef CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT
#define CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT 5000
#endif

#if defined(CONFIG_QM_RES_MGR_ENV_PROD)
#define QM_RES_MGR_DEFAULT_ENV QM_RES_MGR_ENV_PROD
#elif defined(CONFIG_QM_RES_MGR_ENV_TEST)
#define QM_RES_MGR_DEFAULT_ENV QM_RES_MGR_ENV_TEST
#elif defined(CONFIG_QM_RES_MGR_ENV_DEV)
#define QM_RES_MGR_DEFAULT_ENV QM_RES_MGR_ENV_DEV
#else
#define QM_RES_MGR_DEFAULT_ENV QM_RES_MGR_ENV_PROD
#endif

typedef struct {
    qm_res_mgr_env_t env;
    qm_mutex_t lock;
    qm_bool lock_ready;
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    qm_mutex_t mqtt_lock;
    qm_bool mqtt_lock_ready;
    uint32_t mqtt_msg_id;
#endif
} qm_res_mgr_global_t;

typedef struct {
    char *buf;
    int len;
    int cap;
    int max;
    qm_bool overflow;
} qm_res_mgr_http_body_t;

typedef struct {
    qm_res_mgr_resource_t resource;
    qm_res_mgr_download_data_cb_t on_data;
    void *user;
    int last_error;
    int status_code;
    uint32_t total_size;
    uint32_t downloaded_size;
    qm_bool callback_stopped;
} qm_res_mgr_download_ctx_t;

typedef struct {
    qm_res_mgr_download_ctx_t *download;
    uint32_t content_range_total;
    qm_bool range_requested;
    qm_bool range_honored;
} qm_res_mgr_http_download_ctx_t;

#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
typedef struct qm_res_mgr_mqtt_pending {
    qm_sem_t sem;
    qm_bool sem_ready;
    int ret;
    int biz_code;
    uint32_t msg_id;
    const char *reply_method;
    char *params_json;
    struct qm_res_mgr_mqtt_pending *next;
} qm_res_mgr_mqtt_pending_t;
#endif

static qm_res_mgr_global_t g_res_mgr = {
    .env = QM_RES_MGR_DEFAULT_ENV,
};

static volatile uint32_t g_qm_res_mgr_log_flags = QM_RES_MGR_KCONFIG_LOG_FLAGS;

uint32_t qm_res_mgr_get_log_flags(void)
{
    return g_qm_res_mgr_log_flags;
}

int qm_res_mgr_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_RES_MGR_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_res_mgr_log_flags = log_flags;
    return QM_EOK;
}

static int qm_res_mgr_u32_dec_len(uint32_t value);
static char *qm_res_mgr_range_value_alloc(uint32_t start, uint32_t request_len);
static char *qm_res_mgr_url_for_log_alloc(const char *url);
static char *qm_res_mgr_join_url_alloc(const char *prefix, const char *path);
static unsigned int qm_res_mgr_log_text_len(const char *text);
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static int qm_res_mgr_body_reserve(qm_res_mgr_http_body_t *body, int need);
#endif
static int qm_res_mgr_list_reserve(qm_res_mgr_list_t *out, uint32_t capacity);
static int qm_res_mgr_notify_list_page(const qm_res_mgr_list_req_t *req,
                                       const qm_res_mgr_list_t *out,
                                       int start_index,
                                       uint32_t page_num,
                                       uint32_t page_size,
                                       uint32_t count);
static int qm_res_mgr_set_text(char **dst, const char *src);
static int apply_resource_url_prefix(qm_res_mgr_resource_t *resource, const char *url_prefix);
static int qm_res_mgr_lock_init(void);
static qm_res_mgr_env_t qm_res_mgr_get_env_locked(void);
static int qm_res_mgr_cjson_bool_u8(const cJSON *object,
                                    const char *name,
                                    uint8_t *out);
static int qm_res_mgr_parse_list_container(const cJSON *container,
                                           uint32_t page_num,
                                           uint32_t page_size,
                                           qm_res_mgr_list_t *out,
                                           uint32_t *pages);
static int qm_res_mgr_parse_child_category(const cJSON *object,
                                           qm_res_mgr_child_category_t *out);
static int qm_res_mgr_parse_category(const cJSON *object,
                                     qm_res_mgr_category_t *out);
static int qm_res_mgr_parse_category_list_container(const cJSON *container,
                                                    qm_res_mgr_category_list_t *out);
static int parse_child_category_list_container(const cJSON *container,
                                               qm_res_mgr_child_category_list_t *out);
static const char *qm_res_mgr_get_api_base_url_by_env(qm_res_mgr_env_t env);
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static int parse_result_object(const char *body, cJSON **root_out, cJSON **result_out);
static int qm_res_mgr_result_http_get(const char *url,
                                      const char *token,
                                      qm_res_mgr_env_t env,
                                      qm_http_client_response_t *resp,
                                      cJSON **root_out,
                                      cJSON **result_out);
#endif
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
static qm_res_mgr_mqtt_pending_t *g_res_mgr_mqtt_pending_head = NULL;
static int qm_res_mgr_mqtt_init(void);
static void qm_res_mgr_mqtt_reply_handler(void *handle,
                                          const qm_iot_mqtt_recv_t *packet,
                                          void *userdata);
static uint32_t qm_res_mgr_mqtt_next_msg_id_locked(void);
static int qm_res_mgr_mqtt_register(void *iot_handle);
static int qm_res_mgr_mqtt_register_method(void *iot_handle,
                                           const char *reply_method);
static uint32_t mqtt_pending_add(qm_res_mgr_mqtt_pending_t *pending);
static qm_res_mgr_mqtt_pending_t *mqtt_pending_take(const char *reply_method, uint32_t msg_id);
static qm_bool mqtt_pending_remove(qm_res_mgr_mqtt_pending_t *pending);
static int build_list_mqtt_params(const char *did,
                                  const char *bundle,
                                  uint32_t page_num,
                                  uint32_t page_size,
                                  char **payload_out);
static int build_detail_mqtt_params(const char *did,
                                    uint32_t resource_id,
                                    char **payload_out);
static int build_category_mqtt_params(const char *did, char **payload_out);
static int build_children_category_mqtt_params(const char *did,
                                               uint32_t category_id,
                                               const char *resource_bundle_name,
                                               char **payload_out);
static int qm_res_mgr_mqtt_request(void *iot_handle,
                                   const char *up_method,
                                   const char *reply_method,
                                   const char *payload,
                                   uint32_t timeout_ms,
                                   int *biz_code,
                                   char **params_json_out);
static int qm_res_mgr_result_mqtt_get(void *iot_handle,
                                      const char *up_method,
                                      const char *reply_method,
                                      const char *payload,
                                      uint32_t timeout_ms,
                                      int *biz_code,
                                      cJSON **root_out);
#endif
static const char *qm_res_mgr_env_name(qm_res_mgr_env_t env)
{
    switch (env) {
        case QM_RES_MGR_ENV_DEV:
            return "dev";
        case QM_RES_MGR_ENV_TEST:
            return "test";
        case QM_RES_MGR_ENV_PROD:
            return "prod";
        default:
            return "unknown";
    }
}

static int qm_res_mgr_lock_init(void)
{
    if (g_res_mgr.lock_ready) {
        return QM_EOK;
    }

    if (qm_mutex_new(&g_res_mgr.lock) != QM_EOK) {
        return -QM_ENOMEM;
    }
    g_res_mgr.lock_ready = QM_TRUE;
    return QM_EOK;
}

static qm_res_mgr_env_t qm_res_mgr_get_env_locked(void)
{
    qm_res_mgr_env_t env = g_res_mgr.env;

    if (!g_res_mgr.lock_ready) {
        return env;
    }

    qm_mutex_lock(&g_res_mgr.lock, QM_WAIT_FOREVER);
    env = g_res_mgr.env;
    qm_mutex_unlock(&g_res_mgr.lock);
    return env;
}

static unsigned int qm_res_mgr_log_text_len(const char *text)
{
    return text ? (unsigned int)qm_strlen(text) : 0U;
}

static void qm_res_mgr_url_for_log(const char *url, char *out, int out_len)
{
    const char *scheme;
    const char *host;
    const char *end;
    int len;

    if (!out || out_len <= 0) return;
    out[0] = '\0';
    if (!url) return;

    scheme = qm_strstr(url, "://");
    if (scheme) {
        host = scheme + 3;
        end = host;
        while (*end && *end != '/' && *end != '?' && *end != '#') end++;
        len = (int)(end - url);
        if (len >= out_len) len = out_len - 1;
        qm_memcpy(out, url, (uint32_t)len);
        out[len] = '\0';
        if (*end == '/' && len + 4 < out_len) {
            out[len++] = '/';
            out[len++] = '.';
            out[len++] = '.';
            out[len++] = '.';
            out[len] = '\0';
        }
        return;
    }

    end = url;
    while (*end && *end != '?' && *end != '#') end++;
    len = (int)(end - url);
    if (len >= out_len) len = out_len - 1;
    qm_memcpy(out, url, (uint32_t)len);
    out[len] = '\0';
    if ((*end == '?' || *end == '#') && len + 4 < out_len) {
        out[len++] = *end;
        out[len++] = '.';
        out[len++] = '.';
        out[len++] = '.';
        out[len] = '\0';
    }
}

static char *qm_res_mgr_url_for_log_alloc(const char *url)
{
    const char *scheme;
    const char *host;
    const char *end;
    int len;
    int suffix_len = 0;
    char *out;

    if (!url) return qm_strdup("");

    scheme = qm_strstr(url, "://");
    if (scheme) {
        host = scheme + 3;
        end = host;
        while (*end && *end != '/' && *end != '?' && *end != '#') end++;
        len = (int)(end - url);
        if (*end == '/') suffix_len = 4;
    } else {
        end = url;
        while (*end && *end != '?' && *end != '#') end++;
        len = (int)(end - url);
        if (*end == '?' || *end == '#') suffix_len = 4;
    }

    out = (char *)qm_malloc((uint32_t)(len + suffix_len + 1));
    if (!out) return NULL;
    qm_res_mgr_url_for_log(url, out, len + suffix_len + 1);
    return out;
}

static int qm_res_mgr_parse_content_range_total(const char *value, uint32_t *total)
{
    const char *slash;
    uint32_t parsed = 0;
    int len;

    if (!value || !total) return -QM_EINVAL;
    slash = qm_strrchr(value, '/');
    if (!slash || slash[1] == '\0' || slash[1] == '*') return -QM_ENOENT;
    slash++;
    len = (int)qm_strlen(slash);
    if (len <= 0 || len > 10) return -QM_EINVAL;
    if (qm_str2uint((char *)slash, (uint8_t)len, &parsed) != QM_EOK) return -QM_EINVAL;
    *total = parsed;
    return QM_EOK;
}

static void qm_res_mgr_log_http_error_body(qm_http_client_handle_t client,
                                           uint32_t resource_id)
{
    enum { max_log = 256 };
    char body[max_log + 1];
    int used = 0;
    int ret;

    if (!client) return;

    while (used < max_log) {
        ret = qm_http_client_read(client, body + used, max_log - used);
        if (ret > 0) {
            used += ret;
            continue;
        }
        if (ret < 0) {
            QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download error body read failed resource_id=%u ret=%d",
                    (unsigned int)resource_id, ret);
        }
        break;
    }

    if (used > 0) {
        body[used] = '\0';
        QM_RES_MGR_LOGE(LOG_TAG, "download error body resource_id=%u body_len=%d",
                (unsigned int)resource_id, used);
    }
}

static int qm_res_mgr_download_event_cb(const qm_http_client_event_t *event)
{
    qm_res_mgr_http_download_ctx_t *ctx;
    qm_res_mgr_download_ctx_t *download;
    uint32_t total = 0;

    if (!event || !event->user) return QM_EOK;
    ctx = (qm_res_mgr_http_download_ctx_t *)event->user;
    download = ctx->download;
    if (!download) return QM_EOK;

    if (event->event_id == QM_HTTP_CLIENT_EVENT_HEADER &&
        event->header_name && event->header_value) {
        if (qm_strcasecmp(event->header_name, "ETag") == 0) {
            QM_RES_MGR_LOGD(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download header etag received resource_id=%u etag_len=%u",
                    (unsigned int)download->resource.id,
                    qm_res_mgr_log_text_len(event->header_value));
        } else if (qm_strcasecmp(event->header_name, "Content-Range") == 0) {
            ctx->range_honored = QM_TRUE;
            if (qm_res_mgr_parse_content_range_total(event->header_value, &total) == QM_EOK) {
                ctx->content_range_total = total;
                if (download->total_size && download->total_size != total) {
                    QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD,
                                    "download total size update resource_id=%u old=%u content_range_total=%u",
                                    (unsigned int)download->resource.id,
                                    (unsigned int)download->total_size,
                                    (unsigned int)total);
                }
                download->total_size = total;
                download->resource.file_size = total;
                QM_RES_MGR_LOGD(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download content range total resource_id=%u total=%u",
                        (unsigned int)download->resource.id, (unsigned int)total);
            }
        }
    }

    return QM_EOK;
}

static int qm_res_mgr_download_segment(qm_res_mgr_download_ctx_t *download,
                                       uint32_t start,
                                       uint32_t request_len,
                                       uint32_t *received)
{
    qm_http_header_t *headers = NULL;
    char *range_value = NULL;
    qm_http_client_config_t config;
    qm_http_client_handle_t client = NULL;
    qm_res_mgr_http_download_ctx_t ctx;
    uint8_t *buf = NULL;
    int header_count = 0;
    int status;
    int ret;
    uint32_t before;
    uint32_t max_body_len;
    char *log_url = NULL;

    if (!download || !received || request_len == 0) return -QM_EINVAL;
    *received = 0;
    log_url = qm_res_mgr_url_for_log_alloc(download->resource.url);

    headers = (qm_http_header_t *)qm_malloc(sizeof(qm_http_header_t));
    if (!headers) {
        if (log_url) qm_free(log_url);
        return -QM_ENOMEM;
    }
    range_value = qm_res_mgr_range_value_alloc(start, request_len);
    if (!range_value) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    headers[header_count].name = "Range";
    headers[header_count].value = range_value;
    header_count++;

    qm_memset(&ctx, 0, sizeof(ctx));
    ctx.download = download;
    ctx.range_requested = QM_TRUE;

    max_body_len = download->total_size ? download->total_size : 0xffffffffu;
    if (max_body_len < request_len) max_body_len = request_len;

    qm_memset(&config, 0, sizeof(config));
    config.method = QM_HTTP_GET;
    config.url = download->resource.url;
    config.headers = headers;
    config.header_count = header_count;
    config.timeout_ms = QM_RES_MGR_HTTP_TIMEOUT_MS;
    config.max_header_len = QM_RES_MGR_HTTP_HEADER_MAX;
    config.max_body_len = max_body_len;
    config.rx_buf_size = QM_RES_MGR_HTTP_RX_BUF_SIZE;
    config.tls = NULL;
    config.user = &ctx;
    config.on_event = qm_res_mgr_download_event_cb;

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD,
                    "file download segment start resource_id=%u url=%s range=%s headers=%d total=%u",
                    (unsigned int)download->resource.id,
                    log_url ? log_url : "",
                    range_value,
                    header_count,
                    (unsigned int)download->total_size);

    client = qm_http_client_init(&config);
    if (!client) {
        ret = -QM_ENOMEM;
        QM_RES_MGR_LOGE(LOG_TAG, "download client init failed resource_id=%u ret=%d",
                (unsigned int)download->resource.id, ret);
        goto cleanup;
    }

    ret = qm_http_client_open(client, 0);
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "download open failed resource_id=%u ret=%d",
                (unsigned int)download->resource.id, ret);
        goto cleanup;
    }

    ret = qm_http_client_fetch_headers(client);
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "download fetch headers failed resource_id=%u ret=%d",
                (unsigned int)download->resource.id, ret);
        goto cleanup;
    }

    status = qm_http_client_get_status_code(client);
    download->status_code = status;

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download response resource_id=%u status=%d",
            (unsigned int)download->resource.id, status);

    if (status != 200 && status != 206) {
        ret = -QM_EIO;
        QM_RES_MGR_LOGE(LOG_TAG, "download unexpected status resource_id=%u status=%d",
                (unsigned int)download->resource.id, status);
        qm_res_mgr_log_http_error_body(client, download->resource.id);
        goto cleanup;
    }

    if (status == 200 && start > 0) {
        QM_RES_MGR_LOGE(LOG_TAG, "download server ignored range resource_id=%u start=%u",
                (unsigned int)download->resource.id, (unsigned int)start);
        ret = -QM_EIO;
        goto cleanup;
    }

    if (status == 206) {
        ctx.range_honored = QM_TRUE;
    }

    if (status == 200 && qm_http_client_has_content_length(client)) {
        uint32_t content_length = qm_http_client_get_content_length(client);
        if (download->total_size && download->total_size != content_length) {
            QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD,
                            "download total size update resource_id=%u old=%u content_length=%u",
                            (unsigned int)download->resource.id,
                            (unsigned int)download->total_size,
                            (unsigned int)content_length);
        }
        download->total_size = content_length;
        download->resource.file_size = content_length;
        QM_RES_MGR_LOGD(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download content length resource_id=%u len=%u",
                (unsigned int)download->resource.id, (unsigned int)content_length);
    }

    buf = (uint8_t *)qm_malloc(QM_RES_MGR_HTTP_RX_BUF_SIZE);
    if (!buf) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    before = download->downloaded_size;
    while (1) {
        ret = qm_http_client_read(client, (char *)buf, QM_RES_MGR_HTTP_RX_BUF_SIZE);
        if (ret > 0) {
            int read_len = ret;
            uint32_t offset = download->downloaded_size;
            int data_ret = download->on_data(&download->resource, buf, (uint32_t)read_len,
                                             offset, download->total_size, download->user);
            if (data_ret != QM_EOK) {
                download->callback_stopped = QM_TRUE;
                ret = data_ret < 0 ? data_ret : -QM_EIO;
                QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD,
                                "download data callback stopped resource_id=%u offset=%u len=%d ret=%d",
                                (unsigned int)download->resource.id,
                                (unsigned int)offset,
                                read_len,
                                data_ret);
                goto cleanup;
            }
            download->downloaded_size += (uint32_t)read_len;
            continue;
        }
        if (ret == 0) {
            ret = QM_EOK;
            break;
        }
        goto cleanup;
    }

    *received = download->downloaded_size - before;
    if (status == 200 && start == 0 && !qm_http_client_has_content_length(client) &&
        ctx.range_requested && !ctx.range_honored) {
        download->total_size = download->downloaded_size;
        download->resource.file_size = download->downloaded_size;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download segment done resource_id=%u received=%u",
            (unsigned int)download->resource.id, (unsigned int)*received);
    ret = QM_EOK;

cleanup:
    if (ret == -QM_EINTR) {
        QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download segment interrupted resource_id=%u start=%u len=%u",
                (unsigned int)download->resource.id,
                (unsigned int)start,
                (unsigned int)request_len);
    } else if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "download segment failed resource_id=%u start=%u len=%u ret=%d",
                (unsigned int)download->resource.id,
                (unsigned int)start,
                (unsigned int)request_len,
                ret);
    }
    if (buf) qm_free(buf);
    if (client) qm_http_client_cleanup(client);
    if (range_value) qm_free(range_value);
    if (headers) qm_free(headers);
    if (log_url) qm_free(log_url);
    return ret;
}

static int qm_res_mgr_set_text(char **dst, const char *src)
{
    char *copy = NULL;

    if (!dst) return -QM_EINVAL;
    if (src && src[0]) {
        copy = qm_strdup(src);
        if (!copy) return -QM_ENOMEM;
    }
    if (*dst) qm_free(*dst);
    *dst = copy;
    return QM_EOK;
}

static char *qm_res_mgr_join_url_alloc(const char *prefix, const char *path)
{
    const char *path_part;
    int prefix_len;
    int path_len;
    int need_slash;
    int len;
    int ret;
    char *joined;

    if (prefix == NULL || prefix[0] == '\0' || path == NULL || path[0] == '\0') {
        return NULL;
    }

    prefix_len = (int)qm_strlen(prefix);
    path_part = path;
    if (qm_strstr(path, "://") != NULL) {
        return qm_strdup(path);
    }

    if (prefix[prefix_len - 1] == '/' && path[0] == '/') {
        path_part = path + 1;
    }

    need_slash = (prefix[prefix_len - 1] != '/' && path_part[0] != '/') ? 1 : 0;
    path_len = (int)qm_strlen(path_part);
    len = prefix_len + need_slash + path_len + 1;

    joined = (char *)qm_malloc((uint32_t)len);
    if (joined == NULL) {
        return NULL;
    }

    ret = qm_snprintf(joined, len, "%s%s%s", prefix, need_slash ? "/" : "", path_part);
    if (ret < 0 || ret >= len) {
        qm_free(joined);
        return NULL;
    }
    return joined;
}

static int apply_resource_url_prefix(qm_res_mgr_resource_t *resource, const char *url_prefix)
{
    char *full_url;

    if (resource == NULL || resource->url == NULL || resource->url[0] == '\0' ||
        url_prefix == NULL || url_prefix[0] == '\0') {
        return QM_EOK;
    }

    if (qm_strstr(resource->url, "://") != NULL) {
        return QM_EOK;
    }

    full_url = qm_res_mgr_join_url_alloc(url_prefix, resource->url);
    if (full_url == NULL) {
        return -QM_ENOMEM;
    }

    qm_free(resource->url);
    resource->url = full_url;
    return QM_EOK;
}

void qm_res_mgr_resource_free(qm_res_mgr_resource_t *resource)
{
    if (!resource) return;
    if (resource->title) qm_free(resource->title);
    if (resource->description) qm_free(resource->description);
    if (resource->language) qm_free(resource->language);
    if (resource->type) qm_free(resource->type);
    if (resource->audio_type) qm_free(resource->audio_type);
    if (resource->url) qm_free(resource->url);
    if (resource->file_sign) qm_free(resource->file_sign);
    if (resource->etag) qm_free(resource->etag);
    if (resource->create_time) qm_free(resource->create_time);
    if (resource->update_time) qm_free(resource->update_time);
    qm_memset(resource, 0, sizeof(*resource));
}

void qm_res_mgr_child_category_free(qm_res_mgr_child_category_t *category)
{
    if (!category) return;
    if (category->name) qm_free(category->name);
    if (category->resource_bundle_name) qm_free(category->resource_bundle_name);
    if (category->create_time) qm_free(category->create_time);
    if (category->description) qm_free(category->description);
    if (category->icon) qm_free(category->icon);
    if (category->background_image) qm_free(category->background_image);
    qm_memset(category, 0, sizeof(*category));
}

void qm_res_mgr_category_free(qm_res_mgr_category_t *category)
{
    uint32_t i;

    if (!category) return;
    if (category->children) {
        for (i = 0; i < category->child_count; i++) {
            qm_res_mgr_child_category_free(&category->children[i]);
        }
        qm_free(category->children);
    }
    if (category->name) qm_free(category->name);
    if (category->resource_bundle_name) qm_free(category->resource_bundle_name);
    if (category->create_time) qm_free(category->create_time);
    if (category->description) qm_free(category->description);
    if (category->icon) qm_free(category->icon);
    if (category->background_image) qm_free(category->background_image);
    qm_memset(category, 0, sizeof(*category));
}

void qm_res_mgr_category_list_free(qm_res_mgr_category_list_t *list)
{
    uint32_t i;

    if (!list) return;
    if (list->items) {
        for (i = 0; i < list->count; i++) {
            qm_res_mgr_category_free(&list->items[i]);
        }
        qm_free(list->items);
    }
    qm_memset(list, 0, sizeof(*list));
}

void qm_res_mgr_child_category_list_free(qm_res_mgr_child_category_list_t *list)
{
    uint32_t i;

    if (!list) return;
    if (list->items) {
        for (i = 0; i < list->count; i++) {
            qm_res_mgr_child_category_free(&list->items[i]);
        }
        qm_free(list->items);
    }
    qm_memset(list, 0, sizeof(*list));
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static qm_bool qm_res_mgr_is_query_safe(char c)
{
    if (c >= 'A' && c <= 'Z') return QM_TRUE;
    if (c >= 'a' && c <= 'z') return QM_TRUE;
    if (c >= '0' && c <= '9') return QM_TRUE;
    return (c == '-' || c == '_' || c == '.' || c == '~') ? QM_TRUE : QM_FALSE;
}
#endif

static int qm_res_mgr_u32_dec_len(uint32_t value)
{
    int len = 1;

    while (value >= 10) {
        value /= 10;
        len++;
    }
    return len;
}

static char *qm_res_mgr_range_value_alloc(uint32_t start, uint32_t request_len)
{
    char *range_value;
    uint32_t end;
    int len;
    int ret;

    if (request_len == 0 || start > 0xffffffffu - (request_len - 1)) return NULL;
    end = start + request_len - 1;
    len = (int)qm_strlen("bytes=-") +
          qm_res_mgr_u32_dec_len(start) +
          qm_res_mgr_u32_dec_len(end) +
          1;

    range_value = (char *)qm_malloc((uint32_t)len);
    if (!range_value) return NULL;

    ret = qm_snprintf(range_value, len, "bytes=%u-%u",
                      (unsigned int)start,
                      (unsigned int)end);
    if (ret < 0 || ret >= len) {
        qm_free(range_value);
        return NULL;
    }
    return range_value;
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static char *qm_res_mgr_url_encode_alloc(const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    char *dst;
    int used = 0;

    if (!src) return NULL;
    while (src[used]) {
        used++;
    }

    dst = (char *)qm_malloc((uint32_t)used * 3 + 1);
    if (!dst) return NULL;

    used = 0;
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (qm_res_mgr_is_query_safe((char)c)) {
            dst[used++] = (char)c;
        } else {
            dst[used++] = '%';
            dst[used++] = hex[(c >> 4) & 0x0f];
            dst[used++] = hex[c & 0x0f];
        }
    }

    dst[used] = '\0';
    return dst;
}

static char *qm_res_mgr_build_list_query_alloc(uint32_t page_num,
                                               uint32_t page_size,
                                               const char *did,
                                               const char *bundle)
{
    char *query;
    int len;
    int ret;

    if (!did || !bundle) return NULL;
    len = (int)qm_strlen("pageNum=&pageSize=&did=&resourceBundleName=") +
          qm_res_mgr_u32_dec_len(page_num) +
          qm_res_mgr_u32_dec_len(page_size) +
          (int)qm_strlen(did) +
          (int)qm_strlen(bundle) +
          1;

    query = (char *)qm_malloc((uint32_t)len);
    if (!query) return NULL;

    ret = qm_snprintf(query, len,
                      "pageNum=%u&pageSize=%u&did=%s&resourceBundleName=%s",
                      (unsigned int)page_num,
                      (unsigned int)page_size,
                      did,
                      bundle);
    if (ret < 0 || ret >= len) {
        qm_free(query);
        return NULL;
    }
    return query;
}

static char *qm_res_mgr_build_detail_query_alloc(const char *did,
                                                 uint32_t resource_id)
{
    char *query;
    int len;
    int ret;

    if (!did) return NULL;
    len = (int)qm_strlen("did=&resourceId=") +
          (int)qm_strlen(did) +
          qm_res_mgr_u32_dec_len(resource_id) +
          1;

    query = (char *)qm_malloc((uint32_t)len);
    if (!query) return NULL;

    ret = qm_snprintf(query, len, "did=%s&resourceId=%u",
                      did, (unsigned int)resource_id);
    if (ret < 0 || ret >= len) {
        qm_free(query);
        return NULL;
    }
    return query;
}

static char *qm_res_mgr_build_category_query_alloc(uint32_t product_id,
                                                   const char *did)
{
    char *query;
    int len;
    int ret;

    if (product_id == 0U || !did) return NULL;
    len = (int)qm_strlen("pid=&did=") +
          qm_res_mgr_u32_dec_len(product_id) +
          (int)qm_strlen(did) +
          1;

    query = (char *)qm_malloc((uint32_t)len);
    if (!query) return NULL;

    ret = qm_snprintf(query, len, "pid=%u&did=%s",
                      (unsigned int)product_id, did);
    if (ret < 0 || ret >= len) {
        qm_free(query);
        return NULL;
    }
    return query;
}

static char *qm_res_mgr_build_children_category_query_alloc(uint32_t product_id,
                                                            const char *did,
                                                            uint32_t category_id,
                                                            const char *resource_bundle_name)
{
    char *query;
    int len;
    int ret;
    int used;

    if (product_id == 0U || !did) return NULL;
    len = (int)qm_strlen("pid=&did=") +
          qm_res_mgr_u32_dec_len(product_id) +
          (int)qm_strlen(did) +
          1;
    if (category_id != 0U) {
        len += (int)qm_strlen("&categoryId=") + qm_res_mgr_u32_dec_len(category_id);
    }
    if (resource_bundle_name != NULL && resource_bundle_name[0] != '\0') {
        len += (int)qm_strlen("&resourceBundleName=") + (int)qm_strlen(resource_bundle_name);
    }

    query = (char *)qm_malloc((uint32_t)len);
    if (!query) return NULL;

    ret = qm_snprintf(query, len, "pid=%u&did=%s",
                      (unsigned int)product_id, did);
    if (ret < 0 || ret >= len) {
        qm_free(query);
        return NULL;
    }

    if (category_id != 0U) {
        used = (int)qm_strlen(query);
        ret = qm_snprintf(query + used,
                          len - used,
                          "&categoryId=%u",
                          (unsigned int)category_id);
        if (ret < 0 || ret >= len - used) {
            qm_free(query);
            return NULL;
        }
    }

    if (resource_bundle_name != NULL && resource_bundle_name[0] != '\0') {
        used = (int)qm_strlen(query);
        ret = qm_snprintf(query + used,
                          len - used,
                          "&resourceBundleName=%s",
                          resource_bundle_name);
        if (ret < 0 || ret >= len - used) {
            qm_free(query);
            return NULL;
        }
    }

    return query;
}

#endif

static int qm_res_mgr_dup_cjson_string(char **dst, const cJSON *object,
                                       const char *name)
{
    cJSON *item;

    if (!dst || !object || !name) return -QM_EINVAL;
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || !item->valuestring) return -QM_ENOENT;
    return qm_res_mgr_set_text(dst, item->valuestring);
}

static int qm_res_mgr_cjson_u32(const cJSON *object, const char *name, uint32_t *out)
{
    cJSON *item;
    double value;
    const char *text;
    uint32_t parsed = 0;
    int len;

    if (!object || !name || !out) return -QM_EINVAL;
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring) {
        text = item->valuestring;
        len = (int)qm_strlen(text);
        if (len <= 0 || len > 10) return -QM_EINVAL;
        if (qm_str2uint((char *)text, (uint8_t)len, &parsed) != QM_EOK) {
            return -QM_EINVAL;
        }
        *out = parsed;
        return QM_EOK;
    }
    if (!cJSON_IsNumber(item)) return -QM_ENOENT;

    value = item->valuedouble;
    if (value < 0.0 || value > 4294967295.0) return -QM_EINVAL;
    *out = (uint32_t)value;
    return QM_EOK;
}

static int qm_res_mgr_cjson_bool_u8(const cJSON *object,
                                    const char *name,
                                    uint8_t *out)
{
    cJSON *item;
    uint32_t value = 0;
    int ret;

    if (!object || !name || !out) return -QM_EINVAL;

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? 1U : 0U;
        return QM_EOK;
    }

    ret = qm_res_mgr_cjson_u32(object, name, &value);
    if (ret != QM_EOK) {
        return ret;
    }
    if (value > 1U) {
        return -QM_EINVAL;
    }
    *out = (uint8_t)value;
    return QM_EOK;
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static char *qm_res_mgr_build_url_alloc(const char *base, const char *path,
                                        const char *query)
{
    const char *slash;
    char *out;
    int base_len;
    int path_len;
    int query_len;
    int total_len;
    int ret;

    if (!base || !path) return NULL;

    base_len = (int)qm_strlen(base);
    slash = (base_len > 0 && base[base_len - 1] == '/') ? "" : "/";
    path = (*path == '/') ? path + 1 : path;
    path_len = (int)qm_strlen(path);
    query_len = query ? (int)qm_strlen(query) : 0;
    total_len = base_len + (int)qm_strlen(slash) + path_len +
                (query ? 1 + query_len : 0) + 1;

    out = (char *)qm_malloc((uint32_t)total_len);
    if (!out) return NULL;

    ret = qm_snprintf(out, total_len, "%s%s%s%s%s",
                      base, slash, path, query ? "?" : "", query ? query : "");
    if (ret < 0 || ret >= total_len) {
        qm_free(out);
        return NULL;
    }
    return out;
}
#endif

static const char *qm_res_mgr_get_api_base_url_by_env(qm_res_mgr_env_t env)
{
    switch (env) {
        case QM_RES_MGR_ENV_DEV:
            return QM_RES_MGR_API_BASE_URL_DEV;
        case QM_RES_MGR_ENV_TEST:
            return QM_RES_MGR_API_BASE_URL_TEST;
        case QM_RES_MGR_ENV_PROD:
            return QM_RES_MGR_API_BASE_URL_PROD;
        default:
            return QM_RES_MGR_API_BASE_URL_PROD;
    }
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static int qm_res_mgr_body_cb(const char *data, int len, void *user)
{
    qm_res_mgr_http_body_t *body = (qm_res_mgr_http_body_t *)user;
    int need;
    int ret;

    if (!body || (!data && len > 0) || len < 0) return -QM_EINVAL;
    if (len == 0) return QM_EOK;
    if (body->max <= 0 || len > body->max || body->len > body->max - len) {
        body->overflow = QM_TRUE;
        return -QM_EFULL;
    }

    need = body->len + len + 1;
    ret = qm_res_mgr_body_reserve(body, need);
    if (ret == -QM_EFULL) body->overflow = QM_TRUE;
    if (ret != QM_EOK) return ret;

    qm_memcpy(body->buf + body->len, data, (uint32_t)len);
    body->len += len;
    body->buf[body->len] = '\0';
    return QM_EOK;
}

static int qm_res_mgr_body_reserve(qm_res_mgr_http_body_t *body, int need)
{
    char *new_buf;
    int new_cap;
    int max_cap;

    if (!body || need <= 0) return -QM_EINVAL;
    if (need <= body->cap) return QM_EOK;
    if (body->max <= 0 || need > body->max + 1) return -QM_EFULL;

    max_cap = body->max + 1;
    new_cap = body->cap ? body->cap : 256;
    if (new_cap > max_cap) new_cap = max_cap;
    while (new_cap < need) {
        if (new_cap > max_cap / 2) {
            new_cap = max_cap;
        } else {
            new_cap *= 2;
        }
    }

    new_buf = (char *)qm_malloc((uint32_t)new_cap);
    if (!new_buf) return -QM_ENOMEM;
    if (body->buf && body->len > 0) {
        qm_memcpy(new_buf, body->buf, (uint32_t)body->len);
    }
    if (body->buf) qm_free(body->buf);
    body->buf = new_buf;
    body->cap = new_cap;
    return QM_EOK;
}

static int qm_res_mgr_api_get(const char *url, const char *token,
                              qm_res_mgr_env_t env,
                              char **body_out,
                              qm_http_client_response_t *resp)
{
    qm_http_header_t header;
    qm_res_mgr_http_body_t body;
    qm_http_client_request_t request;
    int header_count = 0;
    int ret;
    char *log_url = NULL;

    if (!url || !body_out || !resp) return -QM_EINVAL;
    *body_out = NULL;
    if (QM_RES_MGR_API_BODY_MAX <= 0) {
        QM_RES_MGR_LOGE(LOG_TAG, "api body max invalid max=%d", QM_RES_MGR_API_BODY_MAX);
        return -QM_EINVAL;
    }
    log_url = qm_res_mgr_url_for_log_alloc(url);

    qm_memset(&body, 0, sizeof(body));
    body.max = QM_RES_MGR_API_BODY_MAX;

    if (token && token[0]) {
        header.name = "authorization";
        header.value = token;
        header_count++;
    }

    qm_memset(&request, 0, sizeof(request));
    request.method = QM_HTTP_GET;
    request.url = url;
    request.headers = header_count ? &header : NULL;
    request.header_count = header_count;
    request.timeout_ms = QM_RES_MGR_HTTP_TIMEOUT_MS;
    request.max_header_len = QM_RES_MGR_HTTP_HEADER_MAX;
    request.max_body_len = (uint32_t)QM_RES_MGR_API_BODY_MAX;
    request.rx_buf_size = QM_RES_MGR_HTTP_RX_BUF_SIZE;
    request.tls = NULL;
    request.user = &body;
    request.on_body = qm_res_mgr_body_cb;

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_METADATA, "api get start env=%s url=%s auth=%d token_len=%u",
            qm_res_mgr_env_name(env),
            log_url ? log_url : "",
            token && token[0] ? 1 : 0,
            token ? (unsigned int)qm_strlen(token) : 0);
    ret = qm_http_client_execute(&request, resp);
    if (ret != QM_EOK) {
        if (body.overflow) {
            QM_RES_MGR_LOGE(LOG_TAG, "api get body exceeds max url=%s body_len=%d max=%d",
                    log_url ? log_url : "", body.len, body.max);
        }
        QM_RES_MGR_LOGE(LOG_TAG, "api get failed url=%s ret=%d", log_url ? log_url : "", ret);
        goto cleanup;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_METADATA, "api get done url=%s status=%d body_len=%d",
            log_url ? log_url : "", resp->status_code, body.len);
    if ((resp->status_code < 200 || resp->status_code >= 300) && body.len > 0) {
        QM_RES_MGR_LOGE(LOG_TAG, "api get error response url=%s status=%d body_len=%d",
                        log_url ? log_url : "", resp->status_code, body.len);
    }
    if (!body.buf) {
        body.buf = qm_strdup("");
        if (!body.buf) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        body.cap = 1;
    }
    *body_out = body.buf;
    body.buf = NULL;
    ret = QM_EOK;

cleanup:
    if (body.buf) qm_free(body.buf);
    if (log_url) qm_free(log_url);
    return ret;
}
#endif

static int qm_res_mgr_parse_resource(const cJSON *object, qm_res_mgr_resource_t *out)
{
    cJSON *url_prefix_item;
    const char *url_prefix = NULL;
    uint32_t file_size = 0;
    uint32_t id = 0;
    uint32_t total_duration = 0;
    int ret;

    if (!cJSON_IsObject(object) || !out) return -QM_EINVAL;
    qm_memset(out, 0, sizeof(*out));

    ret = qm_res_mgr_cjson_u32(object, "id", &id);
    if (ret == QM_EOK) out->id = id;

    ret = qm_res_mgr_dup_cjson_string(&out->title, object, "title");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->description, object, "description");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->language, object, "language");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->type, object, "type");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->audio_type, object, "audioType");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->url, object, "url");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->file_sign, object, "fileSign");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->etag, object, "etag");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->create_time, object, "createTime");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->update_time, object, "updateTime");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;

    ret = qm_res_mgr_cjson_u32(object, "fileSize", &file_size);
    if (ret == QM_EOK) out->file_size = file_size;
    ret = qm_res_mgr_cjson_u32(object, "totalDuration", &total_duration);
    if (ret == QM_EOK) out->total_duration = total_duration;

    url_prefix_item = cJSON_GetObjectItemCaseSensitive(object, "urlPrefix");
    if (cJSON_IsString(url_prefix_item) && url_prefix_item->valuestring != NULL) {
        url_prefix = url_prefix_item->valuestring;
    }
    ret = apply_resource_url_prefix(out, url_prefix);
    if (ret != QM_EOK) goto fail;

    if (out->id || (out->url && out->url[0])) return QM_EOK;

    ret = -QM_EINVAL;

fail:
    qm_res_mgr_resource_free(out);
    return ret;
}

static int qm_res_mgr_parse_child_category(const cJSON *object,
                                           qm_res_mgr_child_category_t *out)
{
    int ret;

    if (!cJSON_IsObject(object) || !out) return -QM_EINVAL;
    qm_memset(out, 0, sizeof(*out));

    ret = qm_res_mgr_cjson_u32(object, "id", &out->id);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->name, object, "name");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->resource_bundle_name,
                                      object,
                                      "resourceBundleName");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "parentId", &out->parent_id);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "level", &out->level);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_bool_u8(object, "isLeaf", &out->is_leaf);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "sortOrder", &out->sort_order);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->create_time, object, "createTime");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->description, object, "description");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->icon, object, "icon");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->background_image, object, "backgroundImage");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    return QM_EOK;

fail:
    qm_res_mgr_child_category_free(out);
    return ret;
}

static int qm_res_mgr_parse_category(const cJSON *object,
                                     qm_res_mgr_category_t *out)
{
    cJSON *children = NULL;
    int child_count;
    int i;
    int ret;

    if (!cJSON_IsObject(object) || !out) return -QM_EINVAL;
    qm_memset(out, 0, sizeof(*out));

    ret = qm_res_mgr_cjson_u32(object, "id", &out->id);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->name, object, "name");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->resource_bundle_name,
                                      object,
                                      "resourceBundleName");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "parentId", &out->parent_id);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "level", &out->level);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_bool_u8(object, "isLeaf", &out->is_leaf);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_cjson_u32(object, "sortOrder", &out->sort_order);
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->create_time, object, "createTime");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->description, object, "description");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->icon, object, "icon");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;
    ret = qm_res_mgr_dup_cjson_string(&out->background_image, object, "backgroundImage");
    if (ret != QM_EOK && ret != -QM_ENOENT) goto fail;

    children = cJSON_GetObjectItemCaseSensitive(object, "children");
    if (!cJSON_IsArray(children)) {
        return QM_EOK;
    }

    child_count = cJSON_GetArraySize(children);
    if (child_count <= 0) {
        return QM_EOK;
    }

    out->children = (qm_res_mgr_child_category_t *)qm_malloc(sizeof(*out->children) *
                                                             (uint32_t)child_count);
    if (!out->children) {
        ret = -QM_ENOMEM;
        goto fail;
    }
    qm_memset(out->children, 0, sizeof(*out->children) * (uint32_t)child_count);

    for (i = 0; i < child_count; i++) {
        cJSON *child = cJSON_GetArrayItem(children, i);

        ret = qm_res_mgr_parse_child_category(child, &out->children[i]);
        if (ret != QM_EOK) {
            out->child_count = (uint32_t)i;
            goto fail;
        }
    }
    out->child_count = (uint32_t)child_count;
    return QM_EOK;

fail:
    qm_res_mgr_category_free(out);
    return ret;
}

static void qm_res_mgr_log_list_resource(uint32_t page_num, int index,
                                         const qm_res_mgr_resource_t *resource)
{
    char *log_url;

    if (!resource) return;
    log_url = qm_res_mgr_url_for_log_alloc(resource->url);

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_PAYLOAD,
                    "list item page=%u index=%d id=%u title_len=%u description_len=%u language_len=%u type=%s audio_type=%s total_duration=%u url=%s file_sign_len=%u etag_len=%u create_time_len=%u update_time_len=%u file_size=%u",
                    (unsigned int)page_num,
                    index,
                    (unsigned int)resource->id,
                    qm_res_mgr_log_text_len(resource->title),
                    qm_res_mgr_log_text_len(resource->description),
                    qm_res_mgr_log_text_len(resource->language),
                    resource->type ? resource->type : "",
                    resource->audio_type ? resource->audio_type : "",
                    (unsigned int)resource->total_duration,
                    log_url ? log_url : "",
                    qm_res_mgr_log_text_len(resource->file_sign),
                    qm_res_mgr_log_text_len(resource->etag),
                    qm_res_mgr_log_text_len(resource->create_time),
                    qm_res_mgr_log_text_len(resource->update_time),
                    (unsigned int)resource->file_size);
    if (log_url) qm_free(log_url);
}

static void qm_res_mgr_log_category_node(const qm_res_mgr_category_t *category,
                                         uint32_t depth)
{
    uint32_t i;

    if (!category) return;

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_PAYLOAD,
                    "category depth=%u id=%u parent_id=%u level=%u is_leaf=%u sort_order=%u bundle_len=%u name=%s children=%u",
                    (unsigned int)depth,
                    (unsigned int)category->id,
                    (unsigned int)category->parent_id,
                    (unsigned int)category->level,
                    (unsigned int)category->is_leaf,
                    (unsigned int)category->sort_order,
                    qm_res_mgr_log_text_len(category->resource_bundle_name),
                    category->name ? category->name : "",
                    (unsigned int)category->child_count);

    for (i = 0; i < category->child_count; i++) {
        qm_res_mgr_child_category_t *child = &category->children[i];

        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_PAYLOAD,
                        "category depth=%u id=%u parent_id=%u level=%u is_leaf=%u sort_order=%u bundle_len=%u name=%s",
                        (unsigned int)(depth + 1U),
                        (unsigned int)child->id,
                        (unsigned int)child->parent_id,
                        (unsigned int)child->level,
                        (unsigned int)child->is_leaf,
                        (unsigned int)child->sort_order,
                        qm_res_mgr_log_text_len(child->resource_bundle_name),
                        child->name ? child->name : "");
    }
}

static int qm_res_mgr_parse_list_container(const cJSON *container,
                                           uint32_t page_num,
                                           uint32_t page_size,
                                           qm_res_mgr_list_t *out,
                                           uint32_t *pages)
{
    cJSON *list;
    cJSON *url_prefix_item;
    const char *url_prefix = NULL;
    int item_count;
    int out_count_before;
    int i;
    uint32_t response_page_num = 0;
    uint32_t response_page_size = 0;
    uint32_t response_pages = 0;
    int ret;
    int title_ret;

    if (!cJSON_IsObject(container) || !out || !pages) {
        return -QM_EINVAL;
    }

    (void)qm_res_mgr_cjson_u32(container, "total", &out->total);
    (void)qm_res_mgr_cjson_u32(container, "pageNum", &response_page_num);
    (void)qm_res_mgr_cjson_u32(container, "pageSize", &response_page_size);
    (void)qm_res_mgr_cjson_u32(container, "pages", &response_pages);
    title_ret = qm_res_mgr_dup_cjson_string(&out->resource_bundle_title,
                                            container,
                                            "resourceBundleTitle");
    if (title_ret != QM_EOK && title_ret != -QM_ENOENT) {
        return title_ret;
    }
    url_prefix_item = cJSON_GetObjectItemCaseSensitive(container, "urlPrefix");
    if (cJSON_IsString(url_prefix_item) && url_prefix_item->valuestring != NULL) {
        url_prefix = url_prefix_item->valuestring;
    }

    if (out->page_num == 0) out->page_num = response_page_num ? response_page_num : page_num;
    if (out->page_size == 0) out->page_size = response_page_size ? response_page_size : page_size;
    out->pages = response_pages;
    *pages = response_pages ? response_pages : page_num;
    ret = qm_res_mgr_list_reserve(out, out->total);
    if (ret != QM_EOK) {
        return ret;
    }

    list = cJSON_GetObjectItemCaseSensitive(container, "list");
    if (!cJSON_IsArray(list)) {
        return QM_EOK;
    }

    item_count = cJSON_GetArraySize(list);
    out_count_before = (int)out->count;
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS,
                    "list page parsed page=%u response_page=%u page_size=%u pages=%u total=%u items=%d bundle_title_present=%d url_prefix_present=%d",
                    (unsigned int)page_num,
                    (unsigned int)response_page_num,
                    (unsigned int)response_page_size,
                    (unsigned int)response_pages,
                    (unsigned int)out->total,
                    item_count,
                    out->resource_bundle_title ? 1 : 0,
                    url_prefix ? 1 : 0);
    for (i = 0; i < item_count; i++) {
        cJSON *entry = cJSON_GetArrayItem(list, i);
        qm_res_mgr_resource_t resource;
        int parse_ret;

        if (cJSON_IsObject(entry)) {
            parse_ret = qm_res_mgr_parse_resource(entry, &resource);
            if (parse_ret == QM_EOK) {
                ret = apply_resource_url_prefix(&resource, url_prefix);
                if (ret != QM_EOK) {
                    qm_res_mgr_resource_free(&resource);
                    return ret;
                }
                qm_res_mgr_log_list_resource(page_num, i, &resource);
                if (out->count >= out->total) {
                    uint32_t reserve_count = out->total ? (out->total + (uint32_t)item_count)
                                                        : (out->count + (uint32_t)item_count);

                    ret = qm_res_mgr_list_reserve(out, reserve_count);
                    if (ret != QM_EOK) {
                        qm_res_mgr_resource_free(&resource);
                        return ret;
                    }
                    if (out->total < out->count + (uint32_t)item_count) {
                        out->total = out->count + (uint32_t)item_count;
                    }
                }
                if (out->items == NULL) {
                    qm_res_mgr_resource_free(&resource);
                    return -QM_EINVAL;
                }
                out->items[out->count] = resource;
                out->count++;
            } else if (parse_ret == -QM_ENOMEM) {
                return parse_ret;
            }
        }
    }

    if ((int)out->count != out_count_before + item_count) {
        QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_STATS,
                        "list page count mismatch page=%u parsed=%u array_items=%d",
                        (unsigned int)page_num,
                        (unsigned int)(out->count - (uint32_t)out_count_before),
                        item_count);
    }

    return QM_EOK;
}

static int qm_res_mgr_parse_category_list_container(const cJSON *container,
                                                    qm_res_mgr_category_list_t *out)
{
    cJSON *list;
    int item_count;
    int i;
    int ret;

    if (!cJSON_IsObject(container) || !out) {
        return -QM_EINVAL;
    }

    list = cJSON_GetObjectItemCaseSensitive(container, "list");
    if (!cJSON_IsArray(list)) {
        return QM_EOK;
    }

    item_count = cJSON_GetArraySize(list);
    if (item_count <= 0) {
        return QM_EOK;
    }

    out->items = (qm_res_mgr_category_t *)qm_malloc(sizeof(*out->items) *
                                                    (uint32_t)item_count);
    if (!out->items) {
        return -QM_ENOMEM;
    }
    qm_memset(out->items, 0, sizeof(*out->items) * (uint32_t)item_count);

    for (i = 0; i < item_count; i++) {
        cJSON *entry = cJSON_GetArrayItem(list, i);

        ret = qm_res_mgr_parse_category(entry, &out->items[i]);
        if (ret != QM_EOK) {
            out->count = (uint32_t)i;
            qm_res_mgr_category_list_free(out);
            return ret;
        }
        qm_res_mgr_log_category_node(&out->items[i], 0U);
    }

    out->count = (uint32_t)item_count;
    return QM_EOK;
}

static int parse_child_category_list_container(const cJSON *container,
                                               qm_res_mgr_child_category_list_t *out)
{
    cJSON *list;
    int item_count;
    int i;
    int ret;

    if (!cJSON_IsObject(container) || !out) {
        return -QM_EINVAL;
    }

    list = cJSON_GetObjectItemCaseSensitive(container, "list");
    if (!cJSON_IsArray(list)) {
        return QM_EOK;
    }

    item_count = cJSON_GetArraySize(list);
    if (item_count <= 0) {
        return QM_EOK;
    }

    out->items = (qm_res_mgr_child_category_t *)qm_malloc(sizeof(*out->items) *
                                                          (uint32_t)item_count);
    if (!out->items) {
        return -QM_ENOMEM;
    }
    qm_memset(out->items, 0, sizeof(*out->items) * (uint32_t)item_count);

    for (i = 0; i < item_count; i++) {
        cJSON *entry = cJSON_GetArrayItem(list, i);

        ret = qm_res_mgr_parse_child_category(entry, &out->items[i]);
        if (ret != QM_EOK) {
            out->count = (uint32_t)i;
            qm_res_mgr_child_category_list_free(out);
            return ret;
        }
    }

    out->count = (uint32_t)item_count;
    return QM_EOK;
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static int parse_result_object(const char *body, cJSON **root_out, cJSON **result_out)
{
    cJSON *root = NULL;
    cJSON *result = NULL;

    if (body == NULL || root_out == NULL || result_out == NULL) {
        return -QM_EINVAL;
    }

    *root_out = NULL;
    *result_out = NULL;

    root = cJSON_Parse(body);
    if (!root) {
        return -QM_EINVAL;
    }

    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }

    *root_out = root;
    *result_out = result;
    return QM_EOK;
}

static int qm_res_mgr_result_http_get(const char *url,
                                      const char *token,
                                      qm_res_mgr_env_t env,
                                      qm_http_client_response_t *resp,
                                      cJSON **root_out,
                                      cJSON **result_out)
{
    char *body = NULL;
    int ret;

    if (url == NULL || resp == NULL || root_out == NULL || result_out == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(resp, 0, sizeof(*resp));
    ret = qm_res_mgr_api_get(url, token, env, &body, resp);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (resp->status_code < 200 || resp->status_code >= 300) {
        ret = -QM_EIO;
        goto cleanup;
    }

    ret = parse_result_object(body, root_out, result_out);

cleanup:
    if (body != NULL) {
        qm_free(body);
    }
    return ret;
}
#endif

#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
static int qm_res_mgr_result_mqtt_get(void *iot_handle,
                                      const char *up_method,
                                      const char *reply_method,
                                      const char *payload,
                                      uint32_t timeout_ms,
                                      int *biz_code,
                                      cJSON **root_out)
{
    char *params_json = NULL;
    int ret;

    if (iot_handle == NULL || up_method == NULL || reply_method == NULL ||
        payload == NULL || biz_code == NULL || root_out == NULL) {
        return -QM_EINVAL;
    }

    *root_out = NULL;
    *biz_code = 0;

    ret = qm_res_mgr_mqtt_request(iot_handle,
                                  up_method,
                                  reply_method,
                                  payload,
                                  timeout_ms,
                                  biz_code,
                                  &params_json);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (*biz_code != 0) {
        ret = -QM_EIO;
        goto cleanup;
    }
    if (params_json == NULL) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    *root_out = cJSON_Parse(params_json);
    if (*root_out == NULL) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    ret = QM_EOK;

cleanup:
    if (params_json != NULL) {
        cJSON_free(params_json);
    }
    if (ret != QM_EOK && root_out != NULL && *root_out != NULL) {
        cJSON_Delete(*root_out);
        *root_out = NULL;
    }
    return ret;
}

static int qm_res_mgr_mqtt_init(void)
{
    if (g_res_mgr.mqtt_lock_ready) {
        return QM_EOK;
    }

    if (qm_mutex_new(&g_res_mgr.mqtt_lock) != QM_EOK) {
        return -QM_ENOMEM;
    }
    g_res_mgr.mqtt_lock_ready = QM_TRUE;
    return QM_EOK;
}

static uint32_t qm_res_mgr_mqtt_next_msg_id_locked(void)
{
    g_res_mgr.mqtt_msg_id++;
    if (g_res_mgr.mqtt_msg_id == 0U) {
        g_res_mgr.mqtt_msg_id = 1U;
    }
    return g_res_mgr.mqtt_msg_id;
}

static int qm_res_mgr_mqtt_register(void *iot_handle)
{
    int ret;

    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_res_mgr_mqtt_init();
    if (ret != QM_EOK) {
        return ret;
    }

    /* Refresh local down handlers every request; reconnects may replace handles. */
    qm_mutex_lock(&g_res_mgr.mqtt_lock, QM_WAIT_FOREVER);
    ret = qm_res_mgr_mqtt_register_method(iot_handle,
                                          QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_REPLY);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_res_mgr_mqtt_register_method(iot_handle,
                                          QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_DETAIL_REPLY);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_res_mgr_mqtt_register_method(iot_handle,
                                          QM_RES_MGR_MQTT_METHOD_GET_CATEGORY_REPLY);
    if (ret != QM_EOK) goto cleanup;
    ret = qm_res_mgr_mqtt_register_method(iot_handle,
                                          QM_RES_MGR_MQTT_METHOD_GET_CHILDREN_CATEGORY_REPLY);
    if (ret != QM_EOK) goto cleanup;

cleanup:
    qm_mutex_unlock(&g_res_mgr.mqtt_lock);
    return ret;
}

static int qm_res_mgr_mqtt_register_method(void *iot_handle,
                                           const char *reply_method)
{
    int ret;

    ret = qm_iot_down_register(iot_handle,
                               (char *)reply_method,
                               qm_res_mgr_mqtt_reply_handler,
                               iot_handle);
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "mqtt register reply handler failed method=%s ret=%d",
                reply_method ? reply_method : "",
                ret);
    }
    return ret;
}

static uint32_t mqtt_pending_add(qm_res_mgr_mqtt_pending_t *pending)
{
    uint32_t msg_id;

    if (pending == NULL) {
        return 0U;
    }

    qm_mutex_lock(&g_res_mgr.mqtt_lock, QM_WAIT_FOREVER);
    msg_id = qm_res_mgr_mqtt_next_msg_id_locked();
    pending->msg_id = msg_id;
    pending->next = g_res_mgr_mqtt_pending_head;
    g_res_mgr_mqtt_pending_head = pending;
    qm_mutex_unlock(&g_res_mgr.mqtt_lock);

    return msg_id;
}

static qm_res_mgr_mqtt_pending_t *mqtt_pending_take(const char *reply_method, uint32_t msg_id)
{
    qm_res_mgr_mqtt_pending_t **cursor;
    qm_res_mgr_mqtt_pending_t *pending;

    if (reply_method == NULL || msg_id == 0U) {
        return NULL;
    }

    qm_mutex_lock(&g_res_mgr.mqtt_lock, QM_WAIT_FOREVER);
    cursor = &g_res_mgr_mqtt_pending_head;
    while (*cursor != NULL) {
        pending = *cursor;
        if (pending->msg_id == msg_id &&
            pending->reply_method != NULL &&
            qm_strcmp(reply_method, pending->reply_method) == 0) {
            *cursor = pending->next;
            pending->next = NULL;
            qm_mutex_unlock(&g_res_mgr.mqtt_lock);
            return pending;
        }
        cursor = &pending->next;
    }
    qm_mutex_unlock(&g_res_mgr.mqtt_lock);
    return NULL;
}

static qm_bool mqtt_pending_remove(qm_res_mgr_mqtt_pending_t *pending)
{
    qm_res_mgr_mqtt_pending_t **cursor;

    if (pending == NULL) {
        return QM_FALSE;
    }

    qm_mutex_lock(&g_res_mgr.mqtt_lock, QM_WAIT_FOREVER);
    cursor = &g_res_mgr_mqtt_pending_head;
    while (*cursor != NULL) {
        if (*cursor == pending) {
            *cursor = pending->next;
            pending->next = NULL;
            qm_mutex_unlock(&g_res_mgr.mqtt_lock);
            return QM_TRUE;
        }
        cursor = &(*cursor)->next;
    }
    qm_mutex_unlock(&g_res_mgr.mqtt_lock);
    return QM_FALSE;
}

static int build_list_mqtt_params(const char *did,
                                  const char *bundle,
                                  uint32_t page_num,
                                  uint32_t page_size,
                                  char **payload_out)
{
    cJSON *params = NULL;
    char *payload = NULL;

    if (did == NULL || bundle == NULL || payload_out == NULL) {
        return -QM_EINVAL;
    }

    params = cJSON_CreateObject();
    if (params == NULL) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(params, "did", did) ||
        !cJSON_AddNumberToObject(params, "pageNum", (double)page_num) ||
        !cJSON_AddNumberToObject(params, "pageSize", (double)page_size) ||
        !cJSON_AddStringToObject(params, "resourceBundleName", bundle)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }

    payload = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *payload_out = payload;
    return QM_EOK;
}

static int build_category_mqtt_params(const char *did, char **payload_out)
{
    cJSON *params = NULL;
    char *payload = NULL;

    if (did == NULL || payload_out == NULL) {
        return -QM_EINVAL;
    }

    params = cJSON_CreateObject();
    if (params == NULL) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(params, "did", did)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }

    payload = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *payload_out = payload;
    return QM_EOK;
}

static int build_detail_mqtt_params(const char *did,
                                    uint32_t resource_id,
                                    char **payload_out)
{
    cJSON *params = NULL;
    char *payload = NULL;

    if (did == NULL || payload_out == NULL || resource_id == 0U) {
        return -QM_EINVAL;
    }

    params = cJSON_CreateObject();
    if (params == NULL) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(params, "did", did) ||
        !cJSON_AddNumberToObject(params, "resourceId", (double)resource_id)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }

    payload = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *payload_out = payload;
    return QM_EOK;
}

static int build_children_category_mqtt_params(const char *did,
                                               uint32_t category_id,
                                               const char *resource_bundle_name,
                                               char **payload_out)
{
    cJSON *params = NULL;
    char *payload = NULL;

    if (did == NULL || payload_out == NULL) {
        return -QM_EINVAL;
    }

    params = cJSON_CreateObject();
    if (params == NULL) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(params, "did", did)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }
    if (category_id != 0U &&
        !cJSON_AddNumberToObject(params, "categoryId", (double)category_id)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }
    if (resource_bundle_name != NULL && resource_bundle_name[0] != '\0' &&
        !cJSON_AddStringToObject(params, "resourceBundleName", resource_bundle_name)) {
        cJSON_Delete(params);
        return -QM_ENOMEM;
    }

    payload = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *payload_out = payload;
    return QM_EOK;
}

static void qm_res_mgr_mqtt_reply_handler(void *handle,
                                          const qm_iot_mqtt_recv_t *packet,
                                          void *userdata)
{
    cJSON *root = NULL;
    cJSON *method = NULL;
    cJSON *code = NULL;
    cJSON *id = NULL;
    cJSON *params = NULL;
    qm_res_mgr_mqtt_pending_t *pending;
    char *params_json = NULL;
    uint32_t msg_id = 0;

    QM_UNUSED_ARG(handle);
    /* qm_iot passes a device sub-handle here; match replies by method + id. */
    QM_UNUSED_ARG(userdata);

    if (packet == NULL ||
        packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL ||
        packet->data.pub.payload_len == 0U) {
        return;
    }

    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        return;
    }

    method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsString(method) || method->valuestring == NULL) {
        goto cleanup;
    }

    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(id) && id->valuedouble >= 0.0 && id->valuedouble <= 4294967295.0) {
        msg_id = (uint32_t)id->valuedouble;
    }

    pending = mqtt_pending_take(method->valuestring, msg_id);
    if (pending == NULL) {
        goto cleanup;
    }

    code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code)) {
        pending->biz_code = (int)code->valuedouble;
    }
    params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (params != NULL) {
        params_json = cJSON_PrintUnformatted(params);
        if (params_json == NULL) {
            pending->ret = -QM_ENOMEM;
        }
    }

    if (pending->ret == 0) {
        pending->params_json = params_json;
        params_json = NULL;
        pending->ret = QM_EOK;
    }
    if (pending->sem_ready) {
        (void)qm_sem_signal(&pending->sem);
    }

cleanup:
    if (params_json != NULL) {
        cJSON_free(params_json);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
}

static int qm_res_mgr_mqtt_request(void *iot_handle,
                                   const char *up_method,
                                   const char *reply_method,
                                   const char *payload,
                                   uint32_t timeout_ms,
                                   int *biz_code,
                                   char **params_json_out)
{
    qm_res_mgr_mqtt_pending_t pending;
    int ret;
    uint32_t msg_id;

    if (iot_handle == NULL || up_method == NULL || reply_method == NULL ||
        payload == NULL || biz_code == NULL || params_json_out == NULL) {
        return -QM_EINVAL;
    }

    *biz_code = 0;
    *params_json_out = NULL;

    ret = qm_res_mgr_mqtt_register(iot_handle);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_memset(&pending, 0, sizeof(pending));
    ret = qm_sem_new(&pending.sem, 0);
    if (ret != QM_EOK) {
        return -QM_ENOMEM;
    }
    pending.sem_ready = QM_TRUE;
    pending.ret = 0;
    pending.reply_method = reply_method;

    msg_id = mqtt_pending_add(&pending);
    if (msg_id == 0U) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_CONTROL, "mqtt request start method=%s reply=%s msg_id=%u payload_len=%u",
            up_method,
            reply_method,
            (unsigned int)msg_id,
            (unsigned int)qm_strlen(payload));

    ret = qm_iot_up_report_ex(iot_handle,
                              (char *)up_method,
                              "",
                              msg_id,
                              (char *)payload,
                              (int)qm_strlen(payload));
    if (ret != QM_EOK) {
        if (!mqtt_pending_remove(&pending)) {
            (void)qm_sem_wait(&pending.sem, QM_WAIT_FOREVER);
        }
        goto cleanup;
    }

    ret = qm_sem_wait(&pending.sem,
                      timeout_ms ? timeout_ms : CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT);
    if (ret != QM_EOK) {
        if (!mqtt_pending_remove(&pending)) {
            (void)qm_sem_wait(&pending.sem, QM_WAIT_FOREVER);
        }
        ret = -QM_ETIMEOUT;
        goto cleanup;
    }

    *biz_code = pending.biz_code;
    if (pending.ret != QM_EOK) {
        ret = pending.ret;
        goto cleanup;
    }

    *params_json_out = pending.params_json;
    pending.params_json = NULL;
    ret = QM_EOK;

cleanup:
    if (pending.params_json != NULL) {
        cJSON_free(pending.params_json);
    }
    if (pending.sem_ready) {
        qm_sem_free(&pending.sem);
    }
    return ret;
}
#endif

int qm_res_mgr_set_env(qm_res_mgr_env_t env)
{
    qm_res_mgr_env_t old_env;
    int ret;

    if (env != QM_RES_MGR_ENV_DEV &&
        env != QM_RES_MGR_ENV_TEST &&
        env != QM_RES_MGR_ENV_PROD) {
        QM_RES_MGR_LOGE(LOG_TAG, "set env invalid env=%d", env);
        return -QM_EINVAL;
    }

#if !CONFIG_QM_RES_MGR_HTTP_SUPPORT
    QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_CONTROL, "set env not supported in mqtt-only build env=%d", env);
    return -QM_ENOSYS;
#endif

    ret = qm_res_mgr_lock_init();
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&g_res_mgr.lock, QM_WAIT_FOREVER);
    old_env = g_res_mgr.env;
    g_res_mgr.env = env;
    qm_mutex_unlock(&g_res_mgr.lock);
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_CONTROL, "set env %s -> %s base=%s",
            qm_res_mgr_env_name(old_env),
            qm_res_mgr_env_name(env),
            qm_res_mgr_get_api_base_url_by_env(env));
    return QM_EOK;
}

void qm_res_mgr_list_free(qm_res_mgr_list_t *list)
{
    uint32_t i;

    if (!list) return;
    if (list->resource_bundle_title) {
        qm_free(list->resource_bundle_title);
    }
    if (list->items) {
        for (i = 0; i < list->count; i++) {
            qm_res_mgr_resource_free(&list->items[i]);
        }
        qm_free(list->items);
    }
    qm_memset(list, 0, sizeof(*list));
}

static int qm_res_mgr_list_reserve(qm_res_mgr_list_t *out, uint32_t capacity)
{
    qm_res_mgr_resource_t *new_items;

    if (out == NULL) {
        return -QM_EINVAL;
    }

    if (capacity <= out->count) {
        return QM_EOK;
    }

    if (out->items == NULL) {
        out->items = (qm_res_mgr_resource_t *)qm_malloc(sizeof(*out->items) * capacity);
        if (out->items == NULL) {
            return -QM_ENOMEM;
        }
        qm_memset(out->items, 0, sizeof(*out->items) * capacity);
        return QM_EOK;
    }

    new_items = (qm_res_mgr_resource_t *)qm_realloc(out->items, sizeof(*out->items) * capacity);
    if (new_items == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(new_items + out->count,
              0,
              sizeof(*new_items) * (capacity - out->count));
    out->items = new_items;
    return QM_EOK;
}

static int qm_res_mgr_notify_list_page(const qm_res_mgr_list_req_t *req,
                                       const qm_res_mgr_list_t *out,
                                       int start_index,
                                       uint32_t page_num,
                                       uint32_t page_size,
                                       uint32_t count)
{
    qm_res_mgr_list_page_t page;
    int cb_ret;

    if (req == NULL || out == NULL || start_index < 0) {
        return -QM_EINVAL;
    }

    if (req->on_page == NULL) {
        return QM_EOK;
    }

    qm_memset(&page, 0, sizeof(page));
    page.total = out->total;
    page.page_num = page_num;
    page.page_size = page_size;
    page.pages = out->pages;
    page.count = count;
    page.status_code = out->status_code;
    page.biz_code = out->biz_code;
    page.resource_bundle_title = out->resource_bundle_title;
    page.items = (count > 0U && out->items != NULL) ? &out->items[start_index] : NULL;

    cb_ret = req->on_page(&page, req->page_user);
    if (cb_ret == QM_EOK) {
        return QM_EOK;
    }
    if (cb_ret < 0) {
        return cb_ret;
    }
    return -QM_EIO;
}

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
static int qm_res_mgr_fetch_list_page_http(const char *token,
                                           qm_res_mgr_env_t env,
                                           const char *did,
                                           const char *bundle,
                                           uint32_t page_num,
                                           uint32_t page_size,
                                           qm_res_mgr_list_t *out,
                                           uint32_t *pages)
{
    char *url = NULL;
    char *query = NULL;
    char *body = NULL;
    cJSON *root = NULL;
    cJSON *result = NULL;
    int ret;
    qm_http_client_response_t resp;

    if (!did || !bundle || !out || !pages) return -QM_EINVAL;

    query = qm_res_mgr_build_list_query_alloc(page_num, page_size, did, bundle);
    if (!query) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_METADATA, "list page fetch start env=%s page=%u page_size=%u",
            qm_res_mgr_env_name(env),
            (unsigned int)page_num,
            (unsigned int)page_size);

    url = qm_res_mgr_build_url_alloc(qm_res_mgr_get_api_base_url_by_env(env),
                                     QM_RES_MGR_API_LIST_PATH, query);
    if (!url) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    qm_memset(&resp, 0, sizeof(resp));

    ret = qm_res_mgr_api_get(url, token, env, &body, &resp);
    if (ret != QM_EOK) goto cleanup;
    out->status_code = resp.status_code;
    if (resp.status_code < 200 || resp.status_code >= 300) {
        QM_RES_MGR_LOGE(LOG_TAG, "list page http status unexpected page=%u status=%d",
                (unsigned int)page_num, resp.status_code);
        ret = -QM_EIO;
        goto cleanup;
    }

    root = cJSON_Parse(body);
    if (!root) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsObject(result)) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    ret = qm_res_mgr_parse_list_container(result,
                                          page_num,
                                          page_size,
                                          out,
                                          pages);

cleanup:
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "list page fetch failed page=%u ret=%d",
                (unsigned int)page_num, ret);
    }
    if (root) cJSON_Delete(root);
    if (body) qm_free(body);
    if (query) qm_free(query);
    if (url) qm_free(url);
    return ret;
}

static int qm_res_mgr_fetch_category_http(const char *token,
                                          qm_res_mgr_env_t env,
                                          uint32_t product_id,
                                          const char *did,
                                          qm_res_mgr_category_list_t *out)
{
    char *url = NULL;
    char *query = NULL;
    char *body = NULL;
    cJSON *root = NULL;
    cJSON *result = NULL;
    int ret;
    qm_http_client_response_t resp;

    if (product_id == 0U || did == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    query = qm_res_mgr_build_category_query_alloc(product_id, did);
    if (!query) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    url = qm_res_mgr_build_url_alloc(qm_res_mgr_get_api_base_url_by_env(env),
                                     QM_RES_MGR_API_CATEGORY_PATH,
                                     query);
    if (!url) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    qm_memset(&resp, 0, sizeof(resp));
    ret = qm_res_mgr_api_get(url, token, env, &body, &resp);
    if (ret != QM_EOK) goto cleanup;
    if (resp.status_code < 200 || resp.status_code >= 300) {
        ret = -QM_EIO;
        goto cleanup;
    }

    ret = parse_result_object(body, &root, &result);
    if (ret != QM_EOK) goto cleanup;

    ret = qm_res_mgr_parse_category_list_container(result, out);

cleanup:
    if (root) cJSON_Delete(root);
    if (body) qm_free(body);
    if (query) qm_free(query);
    if (url) qm_free(url);
    return ret;
}

static int qm_res_mgr_fetch_children_category_http(const char *token,
                                                   qm_res_mgr_env_t env,
                                                   uint32_t product_id,
                                                   const char *did,
                                                   uint32_t category_id,
                                                   const char *resource_bundle_name,
                                                   qm_res_mgr_child_category_list_t *out)
{
    char *url = NULL;
    char *query = NULL;
    char *body = NULL;
    cJSON *root = NULL;
    cJSON *result = NULL;
    int ret;
    qm_http_client_response_t resp;

    if (product_id == 0U || did == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    query = qm_res_mgr_build_children_category_query_alloc(product_id,
                                                           did,
                                                           category_id,
                                                           resource_bundle_name);
    if (!query) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    url = qm_res_mgr_build_url_alloc(qm_res_mgr_get_api_base_url_by_env(env),
                                     QM_RES_MGR_API_CHILDREN_CATEGORY_PATH,
                                     query);
    if (!url) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    qm_memset(&resp, 0, sizeof(resp));
    ret = qm_res_mgr_api_get(url, token, env, &body, &resp);
    if (ret != QM_EOK) goto cleanup;
    if (resp.status_code < 200 || resp.status_code >= 300) {
        ret = -QM_EIO;
        goto cleanup;
    }

    ret = parse_result_object(body, &root, &result);
    if (ret != QM_EOK) goto cleanup;

    ret = parse_child_category_list_container(result, out);

cleanup:
    if (root) cJSON_Delete(root);
    if (body) qm_free(body);
    if (query) qm_free(query);
    if (url) qm_free(url);
    return ret;
}
#endif

#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
static int qm_res_mgr_fetch_list_page_mqtt(void *iot_handle,
                                           const char *did,
                                           const char *bundle,
                                           uint32_t page_num,
                                           uint32_t page_size,
                                           uint32_t timeout_ms,
                                           qm_res_mgr_list_t *out,
                                           uint32_t *pages)
{
    cJSON *root = NULL;
    char *payload = NULL;
    char *params_json = NULL;
    int ret;
    int biz_code = 0;

    if (iot_handle == NULL || did == NULL || bundle == NULL ||
        out == NULL || pages == NULL) {
        return -QM_EINVAL;
    }

    ret = build_list_mqtt_params(did, bundle, page_num, page_size, &payload);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_METADATA, "mqtt list page fetch start page=%u page_size=%u did_len=%u bundle_len=%u",
            (unsigned int)page_num,
            (unsigned int)page_size,
            qm_res_mgr_log_text_len(did),
            qm_res_mgr_log_text_len(bundle));

    ret = qm_res_mgr_mqtt_request(iot_handle,
                                  QM_RES_MGR_MQTT_METHOD_GET_RESOURCE,
                                  QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_REPLY,
                                  payload,
                                  timeout_ms,
                                  &biz_code,
                                  &params_json);
    out->biz_code = biz_code;
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (biz_code != 0) {
        ret = -QM_EIO;
        goto cleanup;
    }
    if (params_json == NULL) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    root = cJSON_Parse(params_json);
    if (root == NULL) {
        ret = -QM_EINVAL;
        goto cleanup;
    }

    ret = qm_res_mgr_parse_list_container(root,
                                          page_num,
                                          page_size,
                                          out,
                                          pages);

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (params_json != NULL) {
        cJSON_free(params_json);
    }
    if (payload != NULL) {
        cJSON_free(payload);
    }
    return ret;
}
#endif

int qm_res_mgr_get_list(const qm_res_mgr_list_req_t *req, qm_res_mgr_list_t *out)
{
    char *did = NULL;
    char *bundle = NULL;
    qm_res_mgr_env_t env;
    uint32_t page_num;
    uint32_t page_size;
    uint32_t pages = 0;
    int ret = -QM_EINVAL;

    if (!req || !out || !req->did || !req->resource_bundle_name) {
        QM_RES_MGR_LOGE(LOG_TAG, "get list invalid args");
        return -QM_EINVAL;
    }
    qm_memset(out, 0, sizeof(*out));

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (!req->token || !req->token[0]) {
        QM_RES_MGR_LOGE(LOG_TAG, "get list http token required");
        return -QM_EINVAL;
    }
#else
    if (req->iot_handle == NULL) {
        QM_RES_MGR_LOGE(LOG_TAG, "get list mqtt iot_handle required");
        return -QM_EINVAL;
    }
#endif

    if (req->page_size == 0 && CONFIG_QM_RES_MGR_PAGE_SIZE <= 0) {
        QM_RES_MGR_LOGE(LOG_TAG, "get list invalid configured page_size=%d",
                CONFIG_QM_RES_MGR_PAGE_SIZE);
        return -QM_EINVAL;
    }

    env = qm_res_mgr_get_env_locked();
    page_num = req->page_num ? req->page_num : 1;
    page_size = req->page_size ? req->page_size : (uint32_t)CONFIG_QM_RES_MGR_PAGE_SIZE;
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE, "get list start env=%s channel=%s page=%u page_size=%u did_len=%u bundle_len=%u",
            qm_res_mgr_env_name(env),
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
            "http",
#else
            "mqtt",
#endif
            (unsigned int)page_num,
            (unsigned int)page_size,
            qm_res_mgr_log_text_len(req->did),
            qm_res_mgr_log_text_len(req->resource_bundle_name));

    do {
        uint32_t page_start_index = out->count;

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
        if (did == NULL) {
            did = qm_res_mgr_url_encode_alloc(req->did);
        }
        if (bundle == NULL) {
            bundle = qm_res_mgr_url_encode_alloc(req->resource_bundle_name);
        }
        if (!did || !bundle) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        ret = qm_res_mgr_fetch_list_page_http(req->token,
                                              env,
                                              did,
                                              bundle,
                                              page_num,
                                              page_size,
                                              out,
                                              &pages);
#else
        ret = qm_res_mgr_fetch_list_page_mqtt(req->iot_handle,
                                              req->did,
                                              req->resource_bundle_name,
                                              page_num,
                                              page_size,
                                              req->timeout_ms,
                                              out,
                                              &pages);
#endif
        if (ret != QM_EOK) goto cleanup;
        ret = qm_res_mgr_notify_list_page(req,
                                          out,
                                          (int)page_start_index,
                                          page_num,
                                          page_size,
                                          out->count - page_start_index);
        /* A page callback may deliberately stop pagination after copying the
         * page it needs. Preserve that page as a successful result instead of
         * freeing the accumulated list as an error. */
        if (ret == -QM_EINTR) {
            ret = QM_EOK;
            break;
        }
        if (ret != QM_EOK) {
            QM_RES_MGR_LOGE(LOG_TAG, "list page callback failed page=%u ret=%d",
                    (unsigned int)page_num, ret);
            goto cleanup;
        }
        page_num++;
    } while (pages > 0 && page_num <= pages);

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS, "get list done count=%u total=%u pages=%u",
            (unsigned int)out->count,
            (unsigned int)out->total,
            (unsigned int)out->pages);
    ret = QM_EOK;

cleanup:
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "get list failed ret=%d", ret);
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE, "get list exit ret=%d count=%u",
            ret, (unsigned int)out->count);
    if (ret != QM_EOK) {
        int status_code = out->status_code;
        int biz_code = out->biz_code;
        qm_res_mgr_list_free(out);
        out->status_code = status_code;
        out->biz_code = biz_code;
    }
    if (bundle) qm_free(bundle);
    if (did) qm_free(did);
    return ret;
}

int qm_res_mgr_get_detail(const qm_res_mgr_detail_req_t *req, qm_res_mgr_resource_t *out)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    char *payload = NULL;
    int biz_code = 0;
#endif
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    char *url = NULL;
    char *did = NULL;
    char *query = NULL;
    cJSON *result = NULL;
    qm_http_client_response_t resp;
    qm_res_mgr_env_t env;
#endif
    cJSON *root = NULL;
    int ret;
    char *log_url = NULL;

    if (!req || !out || !req->did || req->resource_id == 0) {
        QM_RES_MGR_LOGE(LOG_TAG, "get detail invalid args resource_id=%u",
                req ? (unsigned int)req->resource_id : 0);
        return -QM_EINVAL;
    }
    qm_memset(out, 0, sizeof(*out));
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    env = qm_res_mgr_get_env_locked();
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE, "get detail start env=%s channel=%s resource_id=%u did_len=%u",
            qm_res_mgr_env_name(env),
            "http",
            (unsigned int)req->resource_id,
            qm_res_mgr_log_text_len(req->did));
#else
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE, "get detail start channel=%s resource_id=%u did_len=%u",
            "mqtt",
            (unsigned int)req->resource_id,
            qm_res_mgr_log_text_len(req->did));
#endif

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    did = qm_res_mgr_url_encode_alloc(req->did);
    if (!did) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    query = qm_res_mgr_build_detail_query_alloc(did, req->resource_id);
    if (!query) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    url = qm_res_mgr_build_url_alloc(qm_res_mgr_get_api_base_url_by_env(env),
                                     QM_RES_MGR_API_DETAIL_PATH, query);
    if (!url) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = qm_res_mgr_result_http_get(url, req->token, env, &resp, &root, &result);
    if (ret != QM_EOK) {
        if (resp.status_code < 200 || resp.status_code >= 300) {
            QM_RES_MGR_LOGE(LOG_TAG, "get detail http status unexpected resource_id=%u status=%d",
                    (unsigned int)req->resource_id, resp.status_code);
        }
        goto cleanup;
    }

    ret = qm_res_mgr_parse_resource(result, out);
    if (ret == QM_EOK && out->id == 0) {
        out->id = req->resource_id;
    }
    if (ret == QM_EOK) {
        log_url = qm_res_mgr_url_for_log_alloc(out->url);
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_PAYLOAD,
                "get detail done resource_id=%u audio_type=%s total_duration=%u url=%s file_size=%u etag_len=%u",
                (unsigned int)out->id,
                out->audio_type ? out->audio_type : "",
                (unsigned int)out->total_duration,
                log_url ? log_url : "",
                (unsigned int)out->file_size,
                qm_res_mgr_log_text_len(out->etag));
    }
#else
    if (req->iot_handle == NULL) {
        QM_RES_MGR_LOGE(LOG_TAG, "get detail mqtt iot_handle required");
        ret = -QM_EINVAL;
        goto cleanup;
    }

    ret = build_detail_mqtt_params(req->did, req->resource_id, &payload);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = qm_res_mgr_result_mqtt_get(req->iot_handle,
                                     QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_DETAIL,
                                     QM_RES_MGR_MQTT_METHOD_GET_RESOURCE_DETAIL_REPLY,
                                     payload,
                                     req->timeout_ms,
                                     &biz_code,
                                     &root);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = qm_res_mgr_parse_resource(root, out);
    if (ret == QM_EOK && out->id == 0) {
        out->id = req->resource_id;
    }
    if (ret == QM_EOK) {
        log_url = qm_res_mgr_url_for_log_alloc(out->url);
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_PAYLOAD,
                "get detail done resource_id=%u audio_type=%s total_duration=%u url=%s file_size=%u etag_len=%u",
                (unsigned int)out->id,
                out->audio_type ? out->audio_type : "",
                (unsigned int)out->total_duration,
                log_url ? log_url : "",
                (unsigned int)out->file_size,
                qm_res_mgr_log_text_len(out->etag));
    }
#endif

cleanup:
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "get detail failed resource_id=%u ret=%d",
                req ? (unsigned int)req->resource_id : 0, ret);
        qm_res_mgr_resource_free(out);
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE, "get detail exit resource_id=%u ret=%d",
            req ? (unsigned int)req->resource_id : 0, ret);
    if (root) cJSON_Delete(root);
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    if (payload) cJSON_free(payload);
#endif
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (query) qm_free(query);
    if (did) qm_free(did);
    if (url) qm_free(url);
#endif
    if (log_url) qm_free(log_url);
    return ret;
}

int qm_res_mgr_get_category(const qm_res_mgr_category_req_t *req,
                            qm_res_mgr_category_list_t *out)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    char *payload = NULL;
    cJSON *root = NULL;
#endif
    int ret;
    int biz_code = 0;
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    qm_res_mgr_env_t env;
    char *did = NULL;
#endif

    if (!req || !out || !req->did) {
        QM_RES_MGR_LOGE(LOG_TAG, "get category invalid args");
        return -QM_EINVAL;
    }

    qm_memset(out, 0, sizeof(*out));
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    env = qm_res_mgr_get_env_locked();
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get category start env=%s channel=http did_len=%u product_id=%u",
            qm_res_mgr_env_name(env),
            qm_res_mgr_log_text_len(req->did),
            (unsigned int)req->product_id);
#else
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get category start channel=mqtt did_len=%u",
            qm_res_mgr_log_text_len(req->did));
#endif

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (req->product_id == 0U) {
        QM_RES_MGR_LOGE(LOG_TAG, "get category http product_id required");
        return -QM_EINVAL;
    }
    did = qm_res_mgr_url_encode_alloc(req->did);
    if (!did) {
        return -QM_ENOMEM;
    }
    ret = qm_res_mgr_fetch_category_http(req->token,
                                         env,
                                         req->product_id,
                                         did,
                                         out);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS, "get category done channel=http count=%u",
            (unsigned int)out->count);
#else
    if (req->iot_handle == NULL) {
        QM_RES_MGR_LOGE(LOG_TAG, "get category mqtt iot_handle required");
        return -QM_EINVAL;
    }

    ret = build_category_mqtt_params(req->did, &payload);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = qm_res_mgr_result_mqtt_get(req->iot_handle,
                                     QM_RES_MGR_MQTT_METHOD_GET_CATEGORY,
                                     QM_RES_MGR_MQTT_METHOD_GET_CATEGORY_REPLY,
                                     payload,
                                     req->timeout_ms,
                                     &biz_code,
                                     &root);
    out->biz_code = biz_code;
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = qm_res_mgr_parse_category_list_container(root, out);
    if (ret == QM_EOK) {
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS, "get category done channel=mqtt count=%u",
                (unsigned int)out->count);
    }
#endif

    ret = QM_EOK;

cleanup:
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "get category failed ret=%d biz_code=%d", ret, biz_code);
        qm_res_mgr_category_list_free(out);
        out->biz_code = biz_code;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get category exit ret=%d count=%u", ret, (unsigned int)out->count);
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (payload != NULL) {
        cJSON_free(payload);
    }
#endif
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (did != NULL) {
        qm_free(did);
    }
#endif
    return ret;
}

int qm_res_mgr_get_children_category(const qm_res_mgr_children_category_req_t *req,
                                     qm_res_mgr_child_category_list_t *out)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    char *payload = NULL;
    cJSON *root = NULL;
#endif
    int ret;
    int biz_code = 0;
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    qm_res_mgr_env_t env;
    char *did = NULL;
    char *bundle = NULL;
#endif

    if (!req || !out || !req->did) {
        QM_RES_MGR_LOGE(LOG_TAG, "get children category invalid args");
        return -QM_EINVAL;
    }

    qm_memset(out, 0, sizeof(*out));
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    env = qm_res_mgr_get_env_locked();
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get children category start env=%s channel=http did_len=%u category_id=%u bundle_len=%u",
            qm_res_mgr_env_name(env),
            qm_res_mgr_log_text_len(req->did),
            (unsigned int)req->category_id,
            qm_res_mgr_log_text_len(req->resource_bundle_name));
#else
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get children category start channel=mqtt did_len=%u category_id=%u bundle_len=%u",
            qm_res_mgr_log_text_len(req->did),
            (unsigned int)req->category_id,
            qm_res_mgr_log_text_len(req->resource_bundle_name));
#endif

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (req->product_id == 0U) {
        QM_RES_MGR_LOGE(LOG_TAG, "get children category http product_id required");
        return -QM_EINVAL;
    }
    did = qm_res_mgr_url_encode_alloc(req->did);
    if (!did) {
        return -QM_ENOMEM;
    }
    if (req->resource_bundle_name != NULL && req->resource_bundle_name[0] != '\0') {
        bundle = qm_res_mgr_url_encode_alloc(req->resource_bundle_name);
        if (!bundle) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }
    ret = qm_res_mgr_fetch_children_category_http(req->token,
                                                  env,
                                                  req->product_id,
                                                  did,
                                                  req->category_id,
                                                  bundle,
                                                  out);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS, "get children category done channel=http count=%u category_id=%u bundle_len=%u",
            (unsigned int)out->count,
            (unsigned int)req->category_id,
            qm_res_mgr_log_text_len(req->resource_bundle_name));
#else
    if (req->iot_handle == NULL) {
        QM_RES_MGR_LOGE(LOG_TAG, "get children category mqtt iot_handle required");
        return -QM_EINVAL;
    }

    ret = build_children_category_mqtt_params(req->did,
                                              req->category_id,
                                              req->resource_bundle_name,
                                              &payload);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = qm_res_mgr_result_mqtt_get(req->iot_handle,
                                     QM_RES_MGR_MQTT_METHOD_GET_CHILDREN_CATEGORY,
                                     QM_RES_MGR_MQTT_METHOD_GET_CHILDREN_CATEGORY_REPLY,
                                     payload,
                                     req->timeout_ms,
                                     &biz_code,
                                     &root);
    out->biz_code = biz_code;
    if (ret != QM_EOK) {
        goto cleanup;
    }

    ret = parse_child_category_list_container(root, out);
    if (ret == QM_EOK) {
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS, "get children category done channel=mqtt count=%u category_id=%u bundle_len=%u",
                (unsigned int)out->count,
                (unsigned int)req->category_id,
                qm_res_mgr_log_text_len(req->resource_bundle_name));
    }
#endif

    ret = QM_EOK;

cleanup:
    if (ret != QM_EOK) {
        QM_RES_MGR_LOGE(LOG_TAG, "get children category failed ret=%d biz_code=%d", ret, biz_code);
        qm_res_mgr_child_category_list_free(out);
        out->biz_code = biz_code;
    }
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "get children category exit ret=%d count=%u", ret, (unsigned int)out->count);
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (payload != NULL) {
        cJSON_free(payload);
    }
#endif
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (bundle != NULL) {
        qm_free(bundle);
    }
    if (did != NULL) {
        qm_free(did);
    }
#endif
    return ret;
}

int qm_res_mgr_download(const qm_res_mgr_download_req_t *req,
                        qm_res_mgr_download_result_t *result)
{
    qm_res_mgr_download_ctx_t download;
    uint8_t retry = 0;
    int ret = QM_EOK;
    char *log_url = NULL;

    if (!req || !req->resource.url || !req->resource.url[0] || !req->on_data) {
        QM_RES_MGR_LOGE(LOG_TAG, "download invalid args resource_id=%u on_data=%d",
                req ? (unsigned int)req->resource.id : 0,
                req && req->on_data ? 1 : 0);
        return -QM_EINVAL;
    }
    qm_memset(&download, 0, sizeof(download));
    download.resource = req->resource;
    download.on_data = req->on_data;
    download.user = req->user;
    download.total_size = req->resource.file_size;

    if (result) qm_memset(result, 0, sizeof(*result));
    log_url = qm_res_mgr_url_for_log_alloc(req->resource.url);
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "download start resource_id=%u file_size=%u",
            (unsigned int)req->resource.id,
            (unsigned int)req->resource.file_size);
    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "file download start resource_id=%u url=%s file_size=%u",
            (unsigned int)req->resource.id,
            log_url ? log_url : "",
            (unsigned int)req->resource.file_size);

    while (1) {
        uint32_t start = download.downloaded_size;
        uint32_t total = download.total_size;
        uint32_t segment = CONFIG_QM_RES_MGR_SEGMENT_SIZE;
        uint32_t received = 0;

        if (total && start >= total) {
            QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download already complete resource_id=%u start=%u total=%u",
                    (unsigned int)download.resource.id,
                    (unsigned int)start,
                    (unsigned int)total);
            break;
        }

        if (total && start + segment > total) {
            segment = total - start;
        }
        if (segment == 0) segment = CONFIG_QM_RES_MGR_SEGMENT_SIZE;

        ret = qm_res_mgr_download_segment(&download, start, segment, &received);
        if (ret != QM_EOK) {
            if (download.callback_stopped) {
                QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download stopped by data callback resource_id=%u ret=%d",
                        (unsigned int)download.resource.id, ret);
                break;
            }
            retry++;
            if (retry > CONFIG_QM_RES_MGR_RETRY_MAX) {
                QM_RES_MGR_LOGE(LOG_TAG, "download retry exhausted resource_id=%u ret=%d retry=%u",
                        (unsigned int)download.resource.id, ret, (unsigned int)retry);
                break;
            }
            QM_RES_MGR_LOGW(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download retry resource_id=%u ret=%d retry=%u",
                    (unsigned int)download.resource.id, ret, (unsigned int)retry);
            continue;
        }

        retry = 0;

        if (received == 0) {
            if (download.total_size > 0 && download.downloaded_size < download.total_size) {
                ret = -QM_EIO;
                QM_RES_MGR_LOGE(LOG_TAG,
                        "download ended early resource_id=%u start=%u downloaded=%u total=%u",
                        (unsigned int)download.resource.id,
                        (unsigned int)start,
                        (unsigned int)download.downloaded_size,
                        (unsigned int)download.total_size);
            } else {
                QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download no more data resource_id=%u start=%u",
                        (unsigned int)download.resource.id, (unsigned int)start);
            }
            break;
        }
        if (received < segment) {
            if (download.total_size > 0 && download.downloaded_size < download.total_size) {
                ret = -QM_EIO;
                QM_RES_MGR_LOGE(LOG_TAG,
                        "download short segment before completion resource_id=%u received=%u segment=%u downloaded=%u total=%u",
                        (unsigned int)download.resource.id,
                        (unsigned int)received,
                        (unsigned int)segment,
                        (unsigned int)download.downloaded_size,
                        (unsigned int)download.total_size);
            } else {
                QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_DOWNLOAD, "download short segment resource_id=%u received=%u segment=%u",
                        (unsigned int)download.resource.id,
                        (unsigned int)received,
                        (unsigned int)segment);
            }
            break;
        }
    }

    if (ret == QM_EOK && download.total_size > 0 &&
        download.downloaded_size != download.total_size) {
        ret = -QM_EIO;
        QM_RES_MGR_LOGE(LOG_TAG,
                "download size mismatch resource_id=%u downloaded=%u total=%u",
                (unsigned int)download.resource.id,
                (unsigned int)download.downloaded_size,
                (unsigned int)download.total_size);
    }

    if (ret == QM_EOK) {
        download.last_error = QM_EOK;
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS,
                "download done resource_id=%u status=%d downloaded=%u total=%u",
                (unsigned int)download.resource.id,
                download.status_code,
                (unsigned int)download.downloaded_size,
                (unsigned int)download.total_size);
    } else {
        download.last_error = ret;
        QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_STATS,
                "download failed resource_id=%u ret=%d status=%d downloaded=%u total=%u",
                (unsigned int)download.resource.id,
                ret,
                download.status_code,
                (unsigned int)download.downloaded_size,
                (unsigned int)download.total_size);
    }

    QM_RES_MGR_LOGI(LOG_TAG, QM_RES_MGR_LOG_LIFECYCLE,
            "download exit resource_id=%u ret=%d status=%d downloaded=%u total=%u",
            (unsigned int)download.resource.id,
            ret,
            download.status_code,
            (unsigned int)download.downloaded_size,
            (unsigned int)download.total_size);

    if (result) {
        result->status_code = download.status_code;
        result->total_size = download.total_size;
        result->downloaded_size = download.downloaded_size;
    }

    if (log_url) qm_free(log_url);
    return ret;
}
