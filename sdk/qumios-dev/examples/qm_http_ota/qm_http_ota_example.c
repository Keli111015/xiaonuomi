#include "qm_http_ota.h"

#include "qm.h"
#include "qm_errno.h"
#include "qm_event.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_http_ota_example"

#if defined(__GNUC__)
#define QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED __attribute__((unused))
#else
#define QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED
#endif

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "your_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD "your_password"
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_URL
#define QM_HTTP_OTA_EXAMPLE_URL "https://example.com/firmware.bin"
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_IF_RANGE
#define QM_HTTP_OTA_EXAMPLE_IF_RANGE NULL
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_AUTO_RESTART
#define QM_HTTP_OTA_EXAMPLE_AUTO_RESTART QM_FALSE
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_TARGET
#define QM_HTTP_OTA_EXAMPLE_TARGET QM_HTTP_OTA_TARGET_SOC
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET
#define QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET NULL
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_EXPECTED_MD5
#define QM_HTTP_OTA_EXAMPLE_EXPECTED_MD5 NULL
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256
#define QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256 NULL
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_DIGEST_VERIFY_MODE
#define QM_HTTP_OTA_EXAMPLE_DIGEST_VERIFY_MODE QM_HTTP_OTA_DIGEST_VERIFY_STREAM
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE
/* Set this to the exact firmware size before running the OTA example. */
#define QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE 0
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_RX_BUF_SIZE
#if defined(CONFIG_QM_HTTP_OTA_RX_BUFFER_SIZE)
#define QM_HTTP_OTA_EXAMPLE_RX_BUF_SIZE CONFIG_QM_HTTP_OTA_RX_BUFFER_SIZE
#elif defined(CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE)
#define QM_HTTP_OTA_EXAMPLE_RX_BUF_SIZE CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE
#else
#define QM_HTTP_OTA_EXAMPLE_RX_BUF_SIZE 1024
#endif
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_MCU_URL
#define QM_HTTP_OTA_EXAMPLE_MCU_URL QM_HTTP_OTA_EXAMPLE_URL
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE
#define QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE 0
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_MCU_RESUME_ENABLE
#define QM_HTTP_OTA_EXAMPLE_MCU_RESUME_ENABLE QM_FALSE
#endif

#ifndef QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP
#define QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP 5
#endif

#if QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP < 0
#undef QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP
#define QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP 0
#elif QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP > 100
#undef QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP
#define QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP 100
#endif

#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10

typedef struct {
    uint32_t total_size;
    uint32_t written_size;
    uint32_t resume_offset;
    uint8_t *mock_storage;
    uint32_t mock_storage_size;
    qm_bool readback_storage_enabled;
    qm_bool active;
} qm_http_ota_example_mcu_ctx_t;

static int s_wifi_retry_count;
static qm_bool s_ota_demo_started;
static qm_http_ota_example_mcu_ctx_t s_mcu_ota_ctx;

static void qm_http_ota_example_log_throttled_progress(const qm_http_ota_event_t *event)
{
    if (!event) {
        return;
    }

    QM_LOGI(LOG_TAG, "OTA progress %d%% downloaded=%u/%u",
            event->progress_percent,
            (unsigned int)event->downloaded_size,
            (unsigned int)event->total_size);
}

static QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED const char *qm_http_ota_example_error_name(int error_code)
{
    switch (error_code) {
        case -QM_ERR_HTTP_OTA_MD5_MISMATCH:
            return "md5_mismatch";
        case -QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH:
            return "hmac_sha256_mismatch";
        case -QM_EINVAL:
            return "invalid_argument";
        case -QM_EFULL:
            return "size_overflow";
        case -QM_EIO:
            return "io_or_protocol_error";
        case -QM_ENOMEM:
            return "no_memory";
        case -QM_ENOSYS:
            return "not_implemented";
        default:
            return "other";
    }
}

static int qm_http_ota_example_event_cb(const qm_http_ota_event_t *event)
{
    if (!event) {
        return -QM_EINVAL;
    }

    switch (event->event_id) {
        case QM_HTTP_OTA_EVENT_START:
            QM_LOGI(LOG_TAG, "OTA started");
            break;
        case QM_HTTP_OTA_EVENT_CONNECTED:
            QM_LOGI(LOG_TAG, "Connected to server, status=%d", event->status_code);
            break;
        case QM_HTTP_OTA_EVENT_HEADER:
            QM_LOGI(LOG_TAG, "Header: %s: %s",
                    event->header_name ? event->header_name : "",
                    event->header_value ? event->header_value : "");
            break;
        case QM_HTTP_OTA_EVENT_DATA:
            QM_LOGD(LOG_TAG, "Write chunk: downloaded=%u total=%u chunk=%d",
                    (unsigned int)event->downloaded_size,
                    (unsigned int)event->total_size,
                    event->data_len);
            break;
        case QM_HTTP_OTA_EVENT_PROGRESS:
            qm_http_ota_example_log_throttled_progress(event);
            break;
        case QM_HTTP_OTA_EVENT_FINISH:
            QM_LOGI(LOG_TAG, "OTA download finished, downloaded=%u",
                    (unsigned int)event->downloaded_size);
            break;
        case QM_HTTP_OTA_EVENT_ABORT:
            QM_LOGW(LOG_TAG, "OTA aborted, downloaded=%u",
                    (unsigned int)event->downloaded_size);
            break;
        case QM_HTTP_OTA_EVENT_ERROR:
            QM_LOGE(LOG_TAG, "OTA error code=%d reason=%s status=%d downloaded=%u",
                    event->error_code,
                    qm_http_ota_example_error_name(event->error_code),
                    event->status_code,
                    (unsigned int)event->downloaded_size);
            break;
        default:
            break;
    }

    return QM_EOK;
}

static qm_bool qm_http_ota_example_str_enabled(const char *text)
{
    return (text && text[0] != '\0') ? QM_TRUE : QM_FALSE;
}

static QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED const char *
qm_http_ota_example_target_name(qm_http_ota_target_t target)
{
    return target == QM_HTTP_OTA_TARGET_MCU ? "mcu" : "soc";
}

static QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED const char *
qm_http_ota_example_digest_config_name(const qm_http_ota_config_t *ota_cfg)
{
    if (!ota_cfg ||
        (!qm_http_ota_example_str_enabled(ota_cfg->expected_md5_hex) &&
         !qm_http_ota_example_str_enabled(ota_cfg->expected_hmac_sha256_hex))) {
        return "none";
    }
    if (ota_cfg->digest_type == QM_HTTP_OTA_DIGEST_TYPE_MD5) {
        return "md5";
    }
    if (ota_cfg->digest_type == QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256) {
        return "hmac-sha256";
    }
    return "invalid";
}

static QM_HTTP_OTA_EXAMPLE_MAYBE_UNUSED const char *
qm_http_ota_example_digest_mode_name(qm_http_ota_digest_verify_mode_t mode)
{
    return mode == QM_HTTP_OTA_DIGEST_VERIFY_READBACK ? "readback" : "stream";
}

static void qm_http_ota_example_log_config(const qm_http_ota_config_t *ota_cfg)
{
    if (!ota_cfg || !ota_cfg->http_config) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "OTA config target=%s total=%u resume=%d offset=%u digest=%s mode=%s url=%s",
            qm_http_ota_example_target_name(ota_cfg->target),
            (unsigned int)ota_cfg->expected_total_size,
            ota_cfg->resume_enable,
            (unsigned int)ota_cfg->resume_offset,
            qm_http_ota_example_digest_config_name(ota_cfg),
            qm_http_ota_example_digest_mode_name(ota_cfg->digest_verify_mode),
            ota_cfg->http_config->url ? ota_cfg->http_config->url : "");
}

static void qm_http_ota_example_fill_request(qm_http_client_request_t *http_req)
{
    if (!http_req) {
        return;
    }

    qm_memset(http_req, 0, sizeof(*http_req));
    http_req->method = QM_HTTP_GET;
    http_req->url = QM_HTTP_OTA_EXAMPLE_URL;
    http_req->user_agent = "qumi-http-ota-example/1.0";
    http_req->rx_buf_size = QM_HTTP_OTA_EXAMPLE_RX_BUF_SIZE;
}

static void qm_http_ota_example_fill_config(qm_http_ota_config_t *ota_cfg,
                                            qm_http_client_request_t *http_req,
                                            qm_bool resume_enable,
                                            uint32_t resume_offset)
{
    qm_bool md5_enabled;
    qm_bool hmac_enabled;

    if (!ota_cfg || !http_req) {
        return;
    }

    qm_memset(ota_cfg, 0, sizeof(*ota_cfg));
    ota_cfg->http_config = http_req;
    ota_cfg->target = QM_HTTP_OTA_TARGET_SOC;
    ota_cfg->on_event = qm_http_ota_example_event_cb;
    ota_cfg->user = (void *)QM_HTTP_OTA_EXAMPLE_URL;
    ota_cfg->auto_restart = QM_HTTP_OTA_EXAMPLE_AUTO_RESTART;
    ota_cfg->resume_enable = resume_enable;
    ota_cfg->resume_offset = resume_offset;
    ota_cfg->expected_total_size = QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE;
    ota_cfg->progress_step = QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP;
    ota_cfg->resume_if_range = QM_HTTP_OTA_EXAMPLE_IF_RANGE;
    ota_cfg->digest_verify_mode = QM_HTTP_OTA_EXAMPLE_DIGEST_VERIFY_MODE;

    md5_enabled = qm_http_ota_example_str_enabled(QM_HTTP_OTA_EXAMPLE_EXPECTED_MD5);
    hmac_enabled = qm_http_ota_example_str_enabled(QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256);
    if (md5_enabled && hmac_enabled) {
        QM_LOGE(LOG_TAG, "configure only one digest: MD5 or HMAC-SHA256");
    }
    if (md5_enabled) {
        ota_cfg->digest_type = QM_HTTP_OTA_DIGEST_TYPE_MD5;
        ota_cfg->expected_md5_hex = QM_HTTP_OTA_EXAMPLE_EXPECTED_MD5;
    }
    if (hmac_enabled) {
        ota_cfg->digest_type = QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256;
        ota_cfg->product_secret = QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET;
        ota_cfg->expected_hmac_sha256_hex = QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256;
    }
}

static int qm_http_ota_example_mcu_begin(void *user,
                                         const qm_http_ota_mcu_begin_info_t *info)
{
    qm_http_ota_example_mcu_ctx_t *ctx = (qm_http_ota_example_mcu_ctx_t *)user;
    qm_bool readback_storage_enabled = QM_FALSE;

    if (!ctx || !info) {
        return -QM_EINVAL;
    }

#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    if (ctx->mock_storage) {
        qm_free(ctx->mock_storage);
    }
#endif
    qm_memset(ctx, 0, sizeof(*ctx));
    if (info->resume_offset > info->total_size) {
        return -QM_EINVAL;
    }
#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    readback_storage_enabled =
        (QM_HTTP_OTA_EXAMPLE_DIGEST_VERIFY_MODE == QM_HTTP_OTA_DIGEST_VERIFY_READBACK) ?
        QM_TRUE : QM_FALSE;
    if (readback_storage_enabled) {
        if (info->total_size > QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE) {
            return -QM_EFULL;
        }
        ctx->mock_storage_size = info->total_size;
        if (ctx->mock_storage_size > 0) {
            ctx->mock_storage = (uint8_t *)qm_malloc(ctx->mock_storage_size);
            if (!ctx->mock_storage) {
                QM_LOGE(LOG_TAG, "MCU OTA mock storage alloc failed size=%u",
                        (unsigned int)ctx->mock_storage_size);
                return -QM_ENOMEM;
            }
        }
    }
#endif
    ctx->readback_storage_enabled = readback_storage_enabled;
    ctx->total_size = info->total_size;
    ctx->resume_offset = info->resume_offset;
    ctx->written_size = info->resume_offset;
    ctx->active = QM_TRUE;

    QM_LOGI(LOG_TAG, "MCU OTA begin url=%s resume=%u total=%u",
            info->url ? info->url : "",
            (unsigned int)info->resume_offset,
            (unsigned int)info->total_size);

#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    if (ctx->readback_storage_enabled) {
        QM_LOGI(LOG_TAG, "MCU OTA mock storage allocated size=%u",
                (unsigned int)ctx->mock_storage_size);
    }
#endif
    return QM_EOK;
}

static int qm_http_ota_example_mcu_write(void *user, uint32_t offset,
                                         const uint8_t *data, uint32_t len)
{
    qm_http_ota_example_mcu_ctx_t *ctx = (qm_http_ota_example_mcu_ctx_t *)user;

    if (!ctx || !ctx->active || (!data && len > 0)) {
        return -QM_EINVAL;
    }

#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    if (ctx->readback_storage_enabled &&
        ((len > 0 && !ctx->mock_storage) ||
         offset > ctx->mock_storage_size ||
         len > ctx->mock_storage_size - offset)) {
        return -QM_EFULL;
    }
    if (ctx->readback_storage_enabled && len > 0) {
        qm_memcpy(&ctx->mock_storage[offset], data, len);
    }
    ctx->written_size = offset + len;
    QM_LOGI(LOG_TAG, "MCU OTA business write offset=%u len=%u",
            (unsigned int)offset, (unsigned int)len);
    return QM_EOK;
#else
    (void)offset;
    (void)len;
    QM_LOGE(LOG_TAG, "MCU OTA write adapter is not implemented");
    return -QM_ENOSYS;
#endif
}

static int qm_http_ota_example_mcu_read(void *user, uint32_t offset,
                                        uint8_t *data, uint32_t len)
{
    qm_http_ota_example_mcu_ctx_t *ctx = (qm_http_ota_example_mcu_ctx_t *)user;

    if (!ctx || !ctx->active || (!data && len > 0)) {
        return -QM_EINVAL;
    }

#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    if (!ctx->readback_storage_enabled) {
        return -QM_ENOSYS;
    }
    if ((len > 0 && !ctx->mock_storage) ||
        offset > ctx->written_size ||
        len > ctx->written_size - offset) {
        return -QM_EINVAL;
    }
    qm_memcpy(data, &ctx->mock_storage[offset], len);
    return QM_EOK;
#else
    (void)offset;
    (void)len;
    QM_LOGE(LOG_TAG, "MCU OTA read adapter is not implemented");
    return -QM_ENOSYS;
#endif
}

static int qm_http_ota_example_mcu_finish(void *user)
{
    qm_http_ota_example_mcu_ctx_t *ctx = (qm_http_ota_example_mcu_ctx_t *)user;

    if (!ctx || !ctx->active) {
        return -QM_EINVAL;
    }

    QM_LOGI(LOG_TAG, "MCU OTA finish written=%u", (unsigned int)ctx->written_size);
#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
    if (ctx->mock_storage) {
        qm_free(ctx->mock_storage);
        ctx->mock_storage = NULL;
        ctx->mock_storage_size = 0;
    }
#endif
    ctx->active = QM_FALSE;
    return QM_EOK;
}

static int qm_http_ota_example_mcu_abort(void *user, int error_code)
{
    qm_http_ota_example_mcu_ctx_t *ctx = (qm_http_ota_example_mcu_ctx_t *)user;

    QM_UNUSED_ARG(error_code);

    if (ctx) {
#if QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE > 0
        if (ctx->mock_storage) {
            qm_free(ctx->mock_storage);
            ctx->mock_storage = NULL;
            ctx->mock_storage_size = 0;
        }
#endif
        ctx->active = QM_FALSE;
    }
    QM_LOGW(LOG_TAG, "MCU OTA abort error=%d", error_code);
    return QM_EOK;
}

static const qm_http_ota_mcu_ops_t s_mcu_ota_ops = {
    .begin = qm_http_ota_example_mcu_begin,
    .write = qm_http_ota_example_mcu_write,
    .read = qm_http_ota_example_mcu_read,
    .finish = qm_http_ota_example_mcu_finish,
    .abort = qm_http_ota_example_mcu_abort,
};

static const qm_http_ota_mcu_config_t s_mcu_ota_config = {
    .ops = &s_mcu_ota_ops,
    .user = &s_mcu_ota_ctx,
};

static void qm_http_ota_example_print_resume_info_internal(const char *url)
{
    qm_http_ota_resume_info_t info;
    int ret;

    qm_memset(&info, 0, sizeof(info));
    ret = qm_http_ota_resume_info_get(url, &info);
    if (ret != QM_EOK) {
        QM_LOGI(LOG_TAG, "no valid resume info for url=%s ret=%d", url ? url : "", ret);
        return;
    }

    QM_LOGI(LOG_TAG,
            "resume info valid=%d url=%s offset=%u total=%u if_range=%s",
            info.valid,
            info.url,
            (unsigned int)info.offset,
            (unsigned int)info.total_size,
            info.if_range);
}

void qm_http_ota_example_resume_info_start(void)
{
    qm_http_ota_example_print_resume_info_internal(QM_HTTP_OTA_EXAMPLE_URL);
}

void qm_http_ota_example_resume_clear_start(void)
{
    int ret = qm_http_ota_resume_info_clear(QM_HTTP_OTA_EXAMPLE_URL);
    QM_LOGI(LOG_TAG, "resume clear ret=%d", ret);
    (void)ret;
}

void qm_http_ota_example_one_shot_start(void)
{
    qm_http_client_request_t http_req;
    qm_http_ota_config_t ota_cfg;
    int ret;

    qm_http_ota_example_fill_request(&http_req);
    qm_http_ota_example_fill_config(&ota_cfg, &http_req, QM_TRUE, 0);
    qm_http_ota_example_log_config(&ota_cfg);

    qm_http_ota_example_print_resume_info_internal(QM_HTTP_OTA_EXAMPLE_URL);
    ret = qm_http_ota(&ota_cfg);
    QM_LOGI(LOG_TAG, "qm_http_ota(one-shot) ret=%d", ret);
    (void)ret;
}

void qm_http_ota_example_start(void)
{
    qm_http_client_request_t http_req;
    qm_http_ota_config_t ota_cfg;
    qm_http_ota_handle_t handle;
    qm_http_ota_result_t result;
    int ret;

    qm_memset(&result, 0, sizeof(result));
    qm_http_ota_example_fill_request(&http_req);
    qm_http_ota_example_fill_config(&ota_cfg, &http_req, QM_TRUE, 0);
    qm_http_ota_example_log_config(&ota_cfg);

    qm_http_ota_example_print_resume_info_internal(QM_HTTP_OTA_EXAMPLE_URL);

    ret = qm_http_ota_begin(&ota_cfg, &handle);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "qm_http_ota_begin ret=%d", ret);
        return;
    }

    do {
        ret = qm_http_ota_perform(handle);
        if (ret == -QM_ERR_HTTP_OTA_IN_PROGRESS) {
            if (qm_http_ota_get_result(handle, &result) == QM_EOK) {
                QM_LOGD(LOG_TAG,
                        "poll status=%d downloaded=%u image_len=%d total=%u complete=%d",
                        result.status_code,
                        (unsigned int)result.downloaded_size,
                        qm_http_ota_get_image_len_read(handle),
                        (unsigned int)result.total_size,
                        qm_http_ota_is_complete_data_received(handle));
            }
        }
    } while (ret == -QM_ERR_HTTP_OTA_IN_PROGRESS);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "qm_http_ota_perform ret=%d", ret);
        qm_http_ota_abort(handle);
        return;
    }

    if (qm_http_ota_get_result(handle, &result) == QM_EOK) {
        QM_LOGI(LOG_TAG,
                "final result status=%d getter_status=%d downloaded=%u image_len=%d total=%u complete=%d",
                result.status_code,
                qm_http_ota_get_status_code(handle),
                (unsigned int)result.downloaded_size,
                qm_http_ota_get_image_len_read(handle),
                (unsigned int)result.total_size,
                qm_http_ota_is_complete_data_received(handle));
    }

    ret = qm_http_ota_finish(handle);
    QM_LOGI(LOG_TAG, "qm_http_ota_finish ret=%d", ret);
    if (ret == QM_EOK) {
        QM_LOGI(LOG_TAG, "OTA is ready. Call qm_reboot() when your app decides to reboot.");
    }
}

void qm_http_ota_example_abort_start(void)
{
    qm_http_client_request_t http_req;
    qm_http_ota_config_t ota_cfg;
    qm_http_ota_handle_t handle;
    qm_http_ota_result_t result;
    int ret;

    qm_memset(&result, 0, sizeof(result));
    qm_http_ota_example_fill_request(&http_req);
    qm_http_ota_example_fill_config(&ota_cfg, &http_req, QM_TRUE, 0);
    qm_http_ota_example_log_config(&ota_cfg);

    ret = qm_http_ota_begin(&ota_cfg, &handle);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "abort example begin ret=%d", ret);
        return;
    }

    ret = qm_http_ota_perform(handle);
    if (qm_http_ota_get_result(handle, &result) == QM_EOK) {
        QM_LOGI(LOG_TAG, "abort example progress ret=%d status=%d downloaded=%u",
                ret, result.status_code, (unsigned int)result.downloaded_size);
    }

    ret = qm_http_ota_abort(handle);
    QM_LOGI(LOG_TAG, "abort example abort ret=%d", ret);
    qm_http_ota_example_print_resume_info_internal(QM_HTTP_OTA_EXAMPLE_URL);
}

void qm_http_ota_example_mcu_start(void)
{
    qm_http_client_request_t http_req;
    qm_http_ota_config_t ota_cfg;
    int ret;

    qm_http_ota_example_fill_request(&http_req);
    http_req.url = QM_HTTP_OTA_EXAMPLE_MCU_URL;

    qm_http_ota_example_fill_config(&ota_cfg, &http_req,
                                    QM_HTTP_OTA_EXAMPLE_MCU_RESUME_ENABLE, 0);
    ota_cfg.target = QM_HTTP_OTA_TARGET_MCU;
    ota_cfg.mcu_ota = &s_mcu_ota_config;
    ota_cfg.auto_restart = QM_FALSE;
    qm_http_ota_example_log_config(&ota_cfg);

    ret = qm_http_ota(&ota_cfg);
    QM_LOGI(LOG_TAG, "qm_http_ota(MCU) ret=%d", ret);
    (void)ret;
}

void qm_http_ota_example_selected_target_start(void)
{
    if (QM_HTTP_OTA_EXAMPLE_TARGET == QM_HTTP_OTA_TARGET_MCU) {
        qm_http_ota_example_mcu_start();
    } else {
        qm_http_ota_example_start();
    }
}

static void qm_http_ota_example_wifi_event_handler(void *event_handler_arg,
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
            if (!s_ota_demo_started) {
                s_ota_demo_started = QM_TRUE;
                qm_http_ota_example_selected_target_start();
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            s_ota_demo_started = QM_FALSE;
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

void qm_http_ota_wifi_demo_start(void)
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

    QM_LOGI(LOG_TAG, "http ota wifi demo start");

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                   qm_http_ota_example_wifi_event_handler, NULL);

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
