#include "qm_http_ota.h"

#include <limits.h>
#include <stdint.h>

#include "qm.h"
#include "qm_kernel.h"
#include "qm_ota.h"
#include "qm_string.h"
#include "qm_kv.h"
#include "qm_utils_crc32.h"
#include "qm_utils_hmac.h"
#include "qm_utils_md5.h"

#ifndef CONFIG_QM_HTTP_OTA_TIMEOUT_MS
#define CONFIG_QM_HTTP_OTA_TIMEOUT_MS 15000
#endif

#ifndef CONFIG_QM_HTTP_OTA_HEADER_MAX
#define CONFIG_QM_HTTP_OTA_HEADER_MAX 2048
#endif

#ifndef CONFIG_QM_HTTP_OTA_RX_BUFFER_SIZE
#define CONFIG_QM_HTTP_OTA_RX_BUFFER_SIZE 1024
#endif

#define LOG_TAG "qm_http_ota"

static volatile uint32_t g_qm_http_ota_log_flags = QM_HTTP_OTA_KCONFIG_LOG_FLAGS;

uint32_t qm_http_ota_get_log_flags(void)
{
    return g_qm_http_ota_log_flags;
}

int qm_http_ota_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_HTTP_OTA_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_http_ota_log_flags = log_flags;
    return QM_EOK;
}
#define QM_HTTP_OTA_DEFAULT_USER_AGENT "qumi-http-ota/1.0"
#define QM_HTTP_OTA_RANGE_VALUE_MAX 63
#define QM_HTTP_OTA_RESUME_KEY_PREFIX "qm_http_ota_"
#define QM_HTTP_OTA_RESUME_KEY_MAX 31
#define QM_HTTP_OTA_RESUME_URL_MAX (QM_HTTP_OTA_RESUME_URL_LEN - 1)
#define QM_HTTP_OTA_RESUME_TAG_MAX (QM_HTTP_OTA_RESUME_TAG_LEN - 1)
#define QM_HTTP_OTA_MD5_DIGEST_LEN 16
#define QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN 32

typedef enum {
    QM_HTTP_OTA_STATE_INIT = 0,
    QM_HTTP_OTA_STATE_IN_PROGRESS,
    QM_HTTP_OTA_STATE_SUCCESS,
    QM_HTTP_OTA_STATE_ABORTED,
    QM_HTTP_OTA_STATE_FAILED,
} qm_http_ota_state_t;


typedef qm_http_ota_resume_info_t qm_http_ota_resume_record_t;

typedef struct {
    qm_http_ota_config_t config;
    qm_http_client_config_t client_config;
    qm_http_client_handle_t client;
    qm_http_ota_result_t result;
    uint32_t offset;
    qm_http_ota_state_t state;
    int last_error;
    int last_progress_bucket;
    qm_bool headers_fetched;
    qm_bool finish_event_sent;
    qm_bool resume_applied;
    qm_bool content_range_validated;
    qm_bool response_if_range_captured;
    qm_bool md5_enabled;
    qm_bool md5_started;
    qm_bool hmac_sha256_enabled;
    qm_bool hmac_sha256_started;
    qm_bool target_started;
    char *buffer;
    int buffer_size;
    char range_value[QM_HTTP_OTA_RANGE_VALUE_MAX + 1];
    uint8_t expected_md5[QM_HTTP_OTA_MD5_DIGEST_LEN];
    uint8_t expected_hmac_sha256[QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN];
    qm_md5_context md5_ctx;
    qm_hmac_sha256_context hmac_sha256_ctx;
    qm_http_ota_resume_record_t resume_record;
} qm_http_ota_ctx_t;

/*
 * Target abstraction:
 * - SoC path keeps the existing qm_ota lifecycle.
 * - MCU path routes start/write/read/finish/abort through callbacks.
 */
static qm_bool qm_http_ota_is_mcu_target(const qm_http_ota_ctx_t *ctx)
{
    return (ctx && ctx->config.target == QM_HTTP_OTA_TARGET_MCU) ? QM_TRUE : QM_FALSE;
}

static qm_bool qm_http_ota_digest_enabled(const qm_http_ota_config_t *config)
{
    if (!config) {
        return QM_FALSE;
    }

    switch (config->digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            return config->expected_md5_hex ? QM_TRUE : QM_FALSE;
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            return config->expected_hmac_sha256_hex ? QM_TRUE : QM_FALSE;
        default:
            return QM_FALSE;
    }
}

static const char *qm_http_ota_digest_name(const qm_http_ota_config_t *config)
{
    if (!qm_http_ota_digest_enabled(config)) {
        return "none";
    }

    switch (config->digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            return "md5";
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            return "hmac-sha256";
        default:
            return "invalid";
    }
}

static const char *qm_http_ota_digest_verify_mode_name(const qm_http_ota_config_t *config)
{
    if (!config) {
        return "stream";
    }
    if (config->digest_verify_mode == QM_HTTP_OTA_DIGEST_VERIFY_READBACK) {
        return "readback";
    }
    return "stream";
}

static qm_http_ota_digest_verify_mode_t qm_http_ota_get_digest_verify_mode(const qm_http_ota_config_t *config)
{
    if (!config) {
        return QM_HTTP_OTA_DIGEST_VERIFY_STREAM;
    }
    return config->digest_verify_mode;
}

static qm_bool qm_http_ota_digest_readback_enabled(const qm_http_ota_config_t *config)
{
    return (config && qm_http_ota_digest_enabled(config) &&
            qm_http_ota_get_digest_verify_mode(config) == QM_HTTP_OTA_DIGEST_VERIFY_READBACK) ?
           QM_TRUE :
           QM_FALSE;
}

static qm_bool qm_http_ota_digest_stream_enabled(const qm_http_ota_config_t *config)
{
    return (config && qm_http_ota_digest_enabled(config) &&
            qm_http_ota_get_digest_verify_mode(config) == QM_HTTP_OTA_DIGEST_VERIFY_STREAM) ?
           QM_TRUE :
           QM_FALSE;
}

static int qm_http_ota_normalize_target_ret(int ret)
{
    return ret == QM_EOK ? QM_EOK : (ret < 0 ? ret : -QM_EIO);
}

static void qm_http_ota_mask_url(const char *url, char *out, uint32_t out_len)
{
    uint32_t url_len;
    uint32_t prefix_len = 16;
    uint32_t suffix_len = 8;

    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';
    if (!url || url[0] == '\0') {
        return;
    }

    url_len = qm_strlen(url);
    if (out_len < prefix_len + suffix_len + 4) {
        return;
    }
    if (url_len <= prefix_len + suffix_len) {
        prefix_len = url_len > 4 ? 4 : url_len;
        suffix_len = 0;
    }

    qm_memcpy(out, url, prefix_len);
    qm_memcpy(out + prefix_len, "...", 3);
    if (suffix_len > 0) {
        qm_memcpy(out + prefix_len + 3, url + url_len - suffix_len, suffix_len);
    }
    out[prefix_len + 3 + suffix_len] = '\0';
}

static int qm_http_ota_validate_target_config(const qm_http_ota_config_t *config)
{
    const qm_http_ota_mcu_ops_t *ops;
    qm_bool read_required;

    if (!config) {
        return -QM_EINVAL;
    }
    if (config->digest_type != QM_HTTP_OTA_DIGEST_TYPE_MD5 &&
        config->digest_type != QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256) {
        return -QM_EINVAL;
    }
    if (config->expected_md5_hex && config->expected_hmac_sha256_hex) {
        return -QM_EINVAL;
    }
    if (config->expected_md5_hex && config->digest_type != QM_HTTP_OTA_DIGEST_TYPE_MD5) {
        return -QM_EINVAL;
    }
    if (config->expected_hmac_sha256_hex &&
        config->digest_type != QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256) {
        return -QM_EINVAL;
    }
    if (config->digest_type == QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256 &&
        config->expected_hmac_sha256_hex &&
        (!config->product_secret ||
         config->product_secret[0] == '\0')) {
        return -QM_EINVAL;
    }
    if (config->digest_verify_mode != QM_HTTP_OTA_DIGEST_VERIFY_STREAM &&
        config->digest_verify_mode != QM_HTTP_OTA_DIGEST_VERIFY_READBACK) {
        return -QM_EINVAL;
    }
    if (config->target == QM_HTTP_OTA_TARGET_SOC) {
        return QM_EOK;
    }
    if (config->target != QM_HTTP_OTA_TARGET_MCU || !config->mcu_ota || !config->mcu_ota->ops) {
        return -QM_EINVAL;
    }

    ops = config->mcu_ota->ops;
    if (!ops->begin || !ops->write || !ops->finish || !ops->abort) {
        return -QM_EINVAL;
    }

    read_required = qm_http_ota_digest_readback_enabled(config);
    if (read_required && !ops->read) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

/* Start the active target before payload streaming begins. */
static int qm_http_ota_target_begin(qm_http_ota_ctx_t *ctx)
{
    const qm_http_ota_mcu_config_t *mcu;
    qm_http_ota_mcu_begin_info_t info;
    char masked_url[32];
    const char *digest_name;
    int ret;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->target_started) {
        return QM_EOK;
    }

    qm_http_ota_mask_url(ctx->config.http_config ? ctx->config.http_config->url : NULL,
                         masked_url,
                         sizeof(masked_url));
    digest_name = qm_http_ota_digest_name(&ctx->config);
    QM_UNUSED_ARG(digest_name);
    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_LIFECYCLE, "ota begin target=%s url=%s resume=%u digest=%s",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            masked_url,
            (unsigned int)ctx->config.resume_offset,
            digest_name);

    if (!qm_http_ota_is_mcu_target(ctx)) {
        ret = qm_ota_start(NULL);
        if (ret == QM_EOK) {
            ctx->target_started = QM_TRUE;
        }
        return qm_http_ota_normalize_target_ret(ret);
    }

    mcu = ctx->config.mcu_ota;
    if (!mcu || !mcu->ops || !mcu->ops->begin) {
        return -QM_EINVAL;
    }

    qm_memset(&info, 0, sizeof(info));
    info.url = ctx->config.http_config ? ctx->config.http_config->url : NULL;
    info.total_size = ctx->result.total_size;
    info.resume_offset = ctx->config.resume_offset;

    ret = mcu->ops->begin(mcu->user, &info);
    if (ret == QM_EOK) {
        ctx->target_started = QM_TRUE;
    }
    return qm_http_ota_normalize_target_ret(ret);
}

/* Write one chunk into the selected target. */
static int qm_http_ota_target_write(qm_http_ota_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    const qm_http_ota_mcu_config_t *mcu;
    int ret;

    if (!ctx || (!data && len > 0)) {
        return -QM_EINVAL;
    }
    if (len == 0) {
        return QM_EOK;
    }
    if (!ctx->target_started) {
        return -QM_EIO;
    }

    if (!qm_http_ota_is_mcu_target(ctx)) {
        ret = qm_ota_write(&ctx->offset, (uint8_t *)data, len);
        return qm_http_ota_normalize_target_ret(ret);
    }

    mcu = ctx->config.mcu_ota;
    if (!mcu || !mcu->ops || !mcu->ops->write) {
        return -QM_EINVAL;
    }

    ret = mcu->ops->write(mcu->user, ctx->offset, data, len);
    if (ret != QM_EOK) {
        return qm_http_ota_normalize_target_ret(ret);
    }
    ctx->offset += len;
    return QM_EOK;
}

/* Read one chunk back from the selected target. Used by read-back digest verification. */
static int qm_http_ota_target_read(qm_http_ota_ctx_t *ctx, uint32_t *offset,
                                   uint8_t *data, uint32_t len)
{
    const qm_http_ota_mcu_config_t *mcu;
    int ret;

    if (!ctx || !offset || (!data && len > 0)) {
        return -QM_EINVAL;
    }
    if (len == 0) {
        return QM_EOK;
    }
    if (!ctx->target_started) {
        return -QM_EIO;
    }

    if (!qm_http_ota_is_mcu_target(ctx)) {
        ret = qm_ota_read(offset, data, len);
        return qm_http_ota_normalize_target_ret(ret);
    }

    mcu = ctx->config.mcu_ota;
    if (!mcu || !mcu->ops || !mcu->ops->read) {
        return -QM_EINVAL;
    }

    ret = mcu->ops->read(mcu->user, *offset, data, len);
    if (ret != QM_EOK) {
        return qm_http_ota_normalize_target_ret(ret);
    }
    *offset += len;
    return QM_EOK;
}

/* Finish the selected target after download and optional digest verification. */
static int qm_http_ota_target_finish(qm_http_ota_ctx_t *ctx)
{
    const qm_http_ota_mcu_config_t *mcu;
    int ret;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (!ctx->target_started) {
        return -QM_EIO;
    }

    if (!qm_http_ota_is_mcu_target(ctx)) {
        ret = qm_ota_end(NULL);
    } else {
        mcu = ctx->config.mcu_ota;
        if (!mcu || !mcu->ops || !mcu->ops->finish) {
            return -QM_EINVAL;
        }
        ret = mcu->ops->finish(mcu->user);
    }

    if (ret == QM_EOK) {
        ctx->target_started = QM_FALSE;
    }
    return qm_http_ota_normalize_target_ret(ret);
}

/* Abort the selected target and release its lower-layer resources. */
static void qm_http_ota_target_abort(qm_http_ota_ctx_t *ctx, int error_code)
{
    const qm_http_ota_mcu_config_t *mcu;

    if (!ctx || !ctx->target_started) {
        return;
    }

    QM_HTTP_OTA_LOGW(LOG_TAG, QM_HTTP_OTA_LOG_CONTROL, "ota abort target=%s error=%d downloaded=%u",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            error_code,
            (unsigned int)ctx->result.downloaded_size);

    if (!qm_http_ota_is_mcu_target(ctx)) {
        (void)qm_ota_abort(NULL);
    } else {
        mcu = ctx->config.mcu_ota;
        if (mcu && mcu->ops && mcu->ops->abort) {
            (void)mcu->ops->abort(mcu->user, error_code);
        }
    }
    ctx->target_started = QM_FALSE;
}

static int qm_http_ota_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static int qm_http_ota_parse_hex_digest(const char *hex, uint8_t *digest, uint32_t digest_len)
{
    uint32_t i;
    int high;
    int low;

    if (!hex || !digest || digest_len == 0 || qm_strlen(hex) != digest_len * 2u) {
        return -QM_EINVAL;
    }

    for (i = 0; i < digest_len; i++) {
        high = qm_http_ota_hex_nibble(hex[i * 2]);
        low = qm_http_ota_hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return -QM_EINVAL;
        }
        digest[i] = (uint8_t)((high << 4) | low);
    }
    return QM_EOK;
}

static qm_bool qm_http_ota_digest_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint8_t diff = 0;
    uint32_t i;

    if (!a || !b) {
        return QM_FALSE;
    }
    for (i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0 ? QM_TRUE : QM_FALSE;
}

static int qm_http_ota_resume_key_make(const char *url, char *key, int key_len)
{
    uint32_t crc;

    if (!url || !key || key_len <= (int)qm_strlen(QM_HTTP_OTA_RESUME_KEY_PREFIX)) {
        return -QM_EINVAL;
    }
    crc = qm_utils_crc32((const uint8_t *)url, (uint32_t)qm_strlen(url), 0);
    if (qm_snprintf(key, (uint32_t)key_len, "%s%08X",
                    QM_HTTP_OTA_RESUME_KEY_PREFIX, (unsigned int)crc) < 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}

static void qm_http_ota_capture_if_range(qm_http_ota_ctx_t *ctx,
                                         const char *header_name,
                                         const char *header_value)
{
    if (!ctx || !header_name || !header_value || header_value[0] == '\0') {
        return;
    }

    if (qm_strcasecmp(header_name, "ETag") == 0) {
        qm_strncpy(ctx->resume_record.if_range, header_value, QM_HTTP_OTA_RESUME_TAG_MAX);
        ctx->response_if_range_captured = QM_TRUE;
        return;
    }

    if (!ctx->response_if_range_captured &&
        qm_strcasecmp(header_name, "Last-Modified") == 0) {
        qm_strncpy(ctx->resume_record.if_range, header_value, QM_HTTP_OTA_RESUME_TAG_MAX);
        ctx->response_if_range_captured = QM_TRUE;
    }
}

static int qm_http_ota_get_progress_percent(const qm_http_ota_ctx_t *ctx)
{
    uint32_t progress_percent;

    if (!ctx || ctx->result.total_size == 0) {
        return 0;
    }

    progress_percent = (uint32_t)(((uint64_t)ctx->result.downloaded_size * 100u) /
                                  ctx->result.total_size);
    return (int)(progress_percent > 100u ? 100u : progress_percent);
}

static int qm_http_ota_dispatch(qm_http_ota_ctx_t *ctx,
                                qm_http_ota_event_id_t event_id,
                                const char *header_name,
                                const char *header_value,
                                const char *data,
                                int data_len,
                                int error_code)
{
    qm_http_ota_event_t event;

    if (!ctx || !ctx->config.on_event) {
        return QM_EOK;
    }

    qm_memset(&event, 0, sizeof(event));
    event.event_id = event_id;
    event.user = ctx->config.user;
    event.status_code = ctx->result.status_code;
    event.error_code = error_code;
    event.total_size = ctx->result.total_size;
    event.downloaded_size = ctx->result.downloaded_size;
    event.header_name = header_name;
    event.header_value = header_value;
    event.data = data;
    event.data_len = data_len;
    event.progress_percent = qm_http_ota_get_progress_percent(ctx);
    return ctx->config.on_event(&event);
}

static int qm_http_ota_fail(qm_http_ota_ctx_t *ctx, int error_code)
{
    if (!ctx) {
        return -QM_EINVAL;
    }

    if (error_code == QM_EOK) {
        error_code = -QM_EIO;
    } else if (error_code > 0) {
        error_code = -error_code;
    }

    ctx->state = QM_HTTP_OTA_STATE_FAILED;
    ctx->last_error = error_code;
    (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL,
                               NULL, 0, ctx->last_error);
    return ctx->last_error;
}

static int qm_http_ota_dispatch_progress(qm_http_ota_ctx_t *ctx)
{
    int progress;
    int bucket;
    uint8_t step;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->result.total_size == 0) {
        return QM_EOK;
    }

    progress = qm_http_ota_get_progress_percent(ctx);
    step = ctx->config.progress_step;
    if (step == 0) {
        return qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_PROGRESS,
                                    NULL, NULL, NULL, 0, QM_EOK);
    }
    if (step > 100) {
        step = 100;
    }

    bucket = (progress / step) * step;
    if (progress < 100 && ctx->last_progress_bucket >= 0 &&
        bucket < ctx->last_progress_bucket + step) {
        return QM_EOK;
    }
    ctx->last_progress_bucket = bucket;
    return qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_PROGRESS,
                                NULL, NULL, NULL, 0, QM_EOK);
}

static int qm_http_ota_set_total_size(qm_http_ota_ctx_t *ctx, uint32_t total_size)
{
    if (!ctx) {
        return -QM_EINVAL;
    }
    if (total_size == 0 || total_size != ctx->config.expected_total_size) {
        return -QM_EIO;
    }
    if (ctx->result.total_size != 0 && ctx->result.total_size != total_size) {
        return -QM_EIO;
    }

    ctx->result.total_size = total_size;
    return QM_EOK;
}

static int qm_http_ota_apply_expected_total_size(qm_http_ota_ctx_t *ctx)
{
    if (!ctx) {
        return -QM_EINVAL;
    }
    return qm_http_ota_set_total_size(ctx, ctx->config.expected_total_size);
}

static int qm_http_ota_check_total_size(qm_http_ota_ctx_t *ctx, uint32_t next_len)
{
    uint32_t next_total;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->result.downloaded_size > UINT32_MAX - next_len) {
        return -QM_EFULL;
    }
    next_total = ctx->result.downloaded_size + next_len;

    if (next_total > ctx->result.total_size) {
        return -QM_EFULL;
    }
    return QM_EOK;
}

static int qm_http_ota_resume_load(const char *url, qm_http_ota_resume_record_t *record)
{
    char key[QM_HTTP_OTA_RESUME_KEY_MAX + 1];
    int len;
    int ret;

    if (!url || !record) {
        return -QM_EINVAL;
    }
    ret = qm_http_ota_resume_key_make(url, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }
    qm_memset(record, 0, sizeof(*record));

    ret = qm_kv_init();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }

    len = (int)sizeof(*record);
    ret = qm_kv_get(key, record, &len);
    if (ret != QM_EOK) {
        return ret;
    }
    if (len != (int)sizeof(*record) || !record->valid) {
        return -QM_EIO;
    }
    return QM_EOK;
}

static int qm_http_ota_resume_save(qm_http_ota_ctx_t *ctx)
{
    char key[QM_HTTP_OTA_RESUME_KEY_MAX + 1];
    char if_range[QM_HTTP_OTA_RESUME_TAG_LEN];
    int ret;

    if (!ctx || !ctx->config.http_config || !ctx->config.http_config->url) {
        return -QM_EINVAL;
    }
    ret = qm_http_ota_resume_key_make(ctx->config.http_config->url, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }

    qm_memset(if_range, 0, sizeof(if_range));
    if (ctx->resume_record.if_range[0]) {
        qm_strncpy(if_range, ctx->resume_record.if_range, QM_HTTP_OTA_RESUME_TAG_MAX);
    } else if (ctx->config.resume_if_range) {
        qm_strncpy(if_range, ctx->config.resume_if_range, QM_HTTP_OTA_RESUME_TAG_MAX);
    }

    qm_memset(&ctx->resume_record, 0, sizeof(ctx->resume_record));
    ctx->resume_record.valid = QM_TRUE;
    ctx->resume_record.offset = ctx->result.downloaded_size;
    ctx->resume_record.total_size = ctx->result.total_size;
    qm_strncpy(ctx->resume_record.url, ctx->config.http_config->url, QM_HTTP_OTA_RESUME_URL_MAX);
    if (if_range[0]) {
        qm_strncpy(ctx->resume_record.if_range, if_range, QM_HTTP_OTA_RESUME_TAG_MAX);
    }

    ret = qm_kv_init();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }
    return qm_kv_set(key, &ctx->resume_record, sizeof(ctx->resume_record), 1);
}

static int qm_http_ota_resume_clear(const char *url)
{
    char key[QM_HTTP_OTA_RESUME_KEY_MAX + 1];
    int ret;

    if (!url) {
        return -QM_EINVAL;
    }
    ret = qm_http_ota_resume_key_make(url, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_kv_init();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }

    ret = qm_kv_del(key);
    return ret == QM_EOK ? QM_EOK : (ret < 0 ? ret : -QM_EIO);
}

static int qm_http_ota_parse_uint32(const char *text, uint32_t *value)
{
    uint32_t parsed = 0;

    if (!text || !*text || !value) {
        return -QM_EINVAL;
    }
    while (*text) {
        uint32_t digit;

        if (*text < '0' || *text > '9') {
            return -QM_EINVAL;
        }
        digit = (uint32_t)(*text - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) {
            return -QM_EFULL;
        }
        parsed = parsed * 10u + digit;
        text++;
    }
    *value = parsed;
    return QM_EOK;
}

static int qm_http_ota_validate_content_range(qm_http_ota_ctx_t *ctx, const char *value)
{
    const char *prefix = "bytes ";
    const char *dash;
    const char *slash;
    char start_buf[16];
    char total_buf[16];
    uint32_t start;
    uint32_t total;
    int start_len;
    int total_len;

    if (!ctx || !value) {
        return -QM_EINVAL;
    }
    if (qm_strncmp(value, prefix, (int)qm_strlen(prefix)) != 0) {
        return -QM_EINVAL;
    }

    value += qm_strlen(prefix);
    dash = qm_strchr(value, '-');
    slash = qm_strchr(value, '/');
    if (!dash || !slash || dash >= slash) {
        return -QM_EINVAL;
    }

    start_len = (int)(dash - value);
    total_len = (int)qm_strlen(slash + 1);
    if (start_len <= 0 || start_len >= (int)sizeof(start_buf) ||
        total_len <= 0 || total_len >= (int)sizeof(total_buf)) {
        return -QM_EINVAL;
    }

    qm_memcpy(start_buf, value, start_len);
    start_buf[start_len] = '\0';
    qm_memcpy(total_buf, slash + 1, total_len);
    total_buf[total_len] = '\0';

    if (qm_http_ota_parse_uint32(start_buf, &start) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (qm_strcmp(total_buf, "*") == 0) {
        total = 0;
    } else if (qm_http_ota_parse_uint32(total_buf, &total) != QM_EOK) {
        return -QM_EINVAL;
    }

    if (start != ctx->config.resume_offset) {
        return -QM_EIO;
    }

    if (qm_http_ota_set_total_size(ctx, total) != QM_EOK) {
        return -QM_EIO;
    }
    ctx->content_range_validated = QM_TRUE;
    return QM_EOK;
}

static int qm_http_ota_http_event(const qm_http_client_event_t *event)
{
    qm_http_ota_ctx_t *ctx;
    int ret;

    if (!event || !event->user) {
        return -QM_EINVAL;
    }

    ctx = (qm_http_ota_ctx_t *)event->user;
    ctx->result.status_code = event->status_code;

    switch (event->event_id) {
        case QM_HTTP_CLIENT_EVENT_CONNECTED:
            return qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_CONNECTED,
                                        NULL, NULL, NULL, 0, QM_EOK);
        case QM_HTTP_CLIENT_EVENT_HEADER:
            if (event->status_code >= 200 && event->status_code < 300) {
                qm_http_ota_capture_if_range(ctx, event->header_name, event->header_value);
            }
            if (ctx->resume_applied && event->header_name && event->header_value &&
                qm_strcasecmp(event->header_name, "Content-Range") == 0) {
                ret = qm_http_ota_validate_content_range(ctx, event->header_value);
                if (ret != QM_EOK) {
                    ctx->last_error = ret;
                    return ret;
                }
            }
            return qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_HEADER,
                                        event->header_name, event->header_value,
                                        NULL, 0, QM_EOK);
        case QM_HTTP_CLIENT_EVENT_ERROR:
            ctx->last_error = event->error_code < 0 ? event->error_code : -event->error_code;
            return qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR,
                                        NULL, NULL, NULL, 0, ctx->last_error);
        default:
            return QM_EOK;
    }
}

static uint32_t qm_http_ota_get_effective_body_limit(const qm_http_ota_ctx_t *ctx,
                                                     const qm_http_client_request_t *http_req)
{
    uint32_t limit;

    if (!ctx) {
        return UINT32_MAX;
    }
    limit = ctx->config.expected_total_size;
    if (http_req && http_req->max_body_len > 0 && http_req->max_body_len < limit) {
        limit = http_req->max_body_len;
    }
    return limit;
}

static int qm_http_ota_get_effective_rx_buf_size(const qm_http_client_request_t *http_req)
{
    uint32_t rx_buf_size = http_req && http_req->rx_buf_size ?
                           http_req->rx_buf_size : CONFIG_QM_HTTP_OTA_RX_BUFFER_SIZE;

    if (rx_buf_size == 0 || rx_buf_size > (uint32_t)INT_MAX) {
        return -QM_EINVAL;
    }
    return (int)rx_buf_size;
}

static int qm_http_ota_build_client_config(qm_http_ota_ctx_t *ctx,
                                           const qm_http_client_request_t *http_req)
{
    if (!ctx || !http_req || !http_req->url) {
        return -QM_EINVAL;
    }

    qm_memset(&ctx->client_config, 0, sizeof(ctx->client_config));
    ctx->client_config.method = QM_HTTP_GET;
    ctx->client_config.url = http_req->url;
    ctx->client_config.username = http_req->username;
    ctx->client_config.password = http_req->password;
    ctx->client_config.auth_type = http_req->auth_type;
    ctx->client_config.content_type = http_req->content_type;
    ctx->client_config.user_agent = http_req->user_agent ? http_req->user_agent : QM_HTTP_OTA_DEFAULT_USER_AGENT;
    ctx->client_config.headers = http_req->headers;
    ctx->client_config.header_count = http_req->header_count;
    ctx->client_config.timeout_ms = http_req->timeout_ms ? http_req->timeout_ms : CONFIG_QM_HTTP_OTA_TIMEOUT_MS;
    ctx->client_config.max_header_len = http_req->max_header_len ? http_req->max_header_len : CONFIG_QM_HTTP_OTA_HEADER_MAX;
    ctx->client_config.max_body_len = qm_http_ota_get_effective_body_limit(ctx, http_req);
    ctx->client_config.rx_buf_size = (uint32_t)ctx->buffer_size;
    ctx->client_config.disable_auto_redirect = QM_TRUE;
    ctx->client_config.max_redirection_count = 0;
    ctx->client_config.keep_alive = QM_FALSE;
    ctx->client_config.tls = http_req->tls;
    ctx->client_config.user = ctx;
    ctx->client_config.on_event = qm_http_ota_http_event;
    return QM_EOK;
}

static int qm_http_ota_update_meta(qm_http_ota_ctx_t *ctx)
{
    if (!ctx || !ctx->client) {
        return -QM_EINVAL;
    }

    ctx->result.status_code = qm_http_client_get_status_code(ctx->client);
    if (!ctx->content_range_validated) {
        if (qm_http_client_has_content_length(ctx->client)) {
            return qm_http_ota_set_total_size(ctx,
                                              qm_http_client_get_content_length(ctx->client));
        }
        return qm_http_ota_apply_expected_total_size(ctx);
    }
    return QM_EOK;
}

static void qm_http_ota_try_load_resume(qm_http_ota_ctx_t *ctx)
{
    qm_http_ota_resume_record_t record;

    if (!ctx || !ctx->config.resume_enable || !ctx->config.http_config || !ctx->config.http_config->url) {
        return;
    }
    if (ctx->config.resume_offset > 0) {
        return;
    }
    if (qm_http_ota_resume_load(ctx->config.http_config->url, &record) != QM_EOK) {
        return;
    }
    if (qm_strcmp(record.url, ctx->config.http_config->url) != 0) {
        return;
    }
    if (record.total_size != ctx->config.expected_total_size ||
        record.offset == 0 ||
        record.offset >= ctx->config.expected_total_size) {
        (void)qm_http_ota_resume_clear(ctx->config.http_config->url);
        return;
    }

    ctx->resume_record = record;
    ctx->config.resume_offset = ctx->resume_record.offset;
}

static int qm_http_ota_apply_resume_headers(qm_http_ota_ctx_t *ctx)
{
    const char *if_range;
    int ret;

    if (!ctx || !ctx->client || !ctx->config.resume_enable || ctx->config.resume_offset == 0) {
        return QM_EOK;
    }

    qm_snprintf(ctx->range_value, sizeof(ctx->range_value), "bytes=%u-",
                (unsigned int)ctx->config.resume_offset);
    ret = qm_http_client_set_header(ctx->client, "Range", ctx->range_value);
    if (ret != QM_EOK) {
        return ret;
    }
    if_range = ctx->config.resume_if_range ? ctx->config.resume_if_range :
               (ctx->resume_record.if_range[0] ? ctx->resume_record.if_range : NULL);
    if (if_range) {
        ret = qm_http_client_set_header(ctx->client, "If-Range", if_range);
        if (ret != QM_EOK) {
            return ret;
        }
    }

    ctx->resume_applied = QM_TRUE;
    ctx->offset = ctx->config.resume_offset;
    ctx->result.downloaded_size = ctx->config.resume_offset;
    return QM_EOK;
}

static int qm_http_ota_hmac_sha256_init(qm_http_ota_ctx_t *ctx)
{
    int ret;

    if (!ctx || !ctx->config.expected_hmac_sha256_hex) {
        return QM_EOK;
    }
    if (!ctx->config.product_secret || ctx->config.product_secret[0] == '\0') {
        return -QM_EINVAL;
    }

    ret = qm_http_ota_parse_hex_digest(ctx->config.expected_hmac_sha256_hex,
                                       ctx->expected_hmac_sha256,
                                       QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_utils_hmac_sha256_init(&ctx->hmac_sha256_ctx,
                                    (const uint8_t *)ctx->config.product_secret,
                                    qm_strlen(ctx->config.product_secret));
    if (ret != QM_EOK) {
        return ret;
    }
    ctx->hmac_sha256_enabled = QM_TRUE;
    ctx->hmac_sha256_started = QM_TRUE;
    return QM_EOK;
}

static int qm_http_ota_md5_init(qm_http_ota_ctx_t *ctx)
{
    int ret;

    if (!ctx || !ctx->config.expected_md5_hex) {
        return QM_EOK;
    }

    ret = qm_http_ota_parse_hex_digest(ctx->config.expected_md5_hex,
                                       ctx->expected_md5,
                                       QM_HTTP_OTA_MD5_DIGEST_LEN);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_utils_md5_init(&ctx->md5_ctx);
    qm_utils_md5_starts(&ctx->md5_ctx);
    ctx->md5_enabled = QM_TRUE;
    ctx->md5_started = QM_TRUE;
    return QM_EOK;
}

static void qm_http_ota_hmac_sha256_free(qm_http_ota_ctx_t *ctx)
{
    if (!ctx || !ctx->hmac_sha256_started) {
        return;
    }

    qm_utils_hmac_sha256_free(&ctx->hmac_sha256_ctx);
    ctx->hmac_sha256_started = QM_FALSE;
}

static void qm_http_ota_md5_free(qm_http_ota_ctx_t *ctx)
{
    if (!ctx || !ctx->md5_started) {
        return;
    }

    qm_utils_md5_free(&ctx->md5_ctx);
    ctx->md5_started = QM_FALSE;
}

static int qm_http_ota_digest_init(qm_http_ota_ctx_t *ctx)
{
    if (!ctx) {
        return -QM_EINVAL;
    }
    if (!qm_http_ota_digest_enabled(&ctx->config)) {
        return QM_EOK;
    }

    switch (ctx->config.digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            return qm_http_ota_md5_init(ctx);
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            return qm_http_ota_hmac_sha256_init(ctx);
        default:
            return -QM_EINVAL;
    }
}

static void qm_http_ota_digest_free(qm_http_ota_ctx_t *ctx)
{
    qm_http_ota_md5_free(ctx);
    qm_http_ota_hmac_sha256_free(ctx);
}

static int qm_http_ota_hmac_sha256_update(qm_http_ota_ctx_t *ctx, const uint8_t *data,
                                          uint32_t data_len)
{
    if (!ctx || !ctx->hmac_sha256_enabled || data_len == 0) {
        return QM_EOK;
    }
    if (!data) {
        return -QM_EINVAL;
    }

    return qm_utils_hmac_sha256_update(&ctx->hmac_sha256_ctx, data, data_len);
}

static int qm_http_ota_md5_update(qm_http_ota_ctx_t *ctx, const uint8_t *data,
                                  uint32_t data_len)
{
    if (!ctx || !ctx->md5_enabled || data_len == 0) {
        return QM_EOK;
    }
    if (!data) {
        return -QM_EINVAL;
    }

    qm_utils_md5_update(&ctx->md5_ctx, data, data_len);
    return QM_EOK;
}

static int qm_http_ota_digest_update(qm_http_ota_ctx_t *ctx,
                                     const uint8_t *data,
                                     uint32_t data_len)
{
    if (!ctx || !qm_http_ota_digest_enabled(&ctx->config)) {
        return QM_EOK;
    }

    switch (ctx->config.digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            return qm_http_ota_md5_update(ctx, data, data_len);
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            return qm_http_ota_hmac_sha256_update(ctx, data, data_len);
        default:
            return -QM_EINVAL;
    }
}

static int qm_http_ota_digest_rebuild_from_target(qm_http_ota_ctx_t *ctx)
{
    uint32_t read_offset = 0;
    uint32_t remaining;
    uint32_t read_len;
    const char *digest_name;
    int ret;

    if (!ctx || !qm_http_ota_digest_enabled(&ctx->config)) {
        return QM_EOK;
    }

    digest_name = qm_http_ota_digest_name(&ctx->config);
    QM_UNUSED_ARG(digest_name);
    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "rebuild digest=%s from target=%s bytes=%u",
            digest_name,
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            (unsigned int)ctx->result.downloaded_size);

    switch (ctx->config.digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            qm_http_ota_md5_free(ctx);
            qm_utils_md5_init(&ctx->md5_ctx);
            qm_utils_md5_starts(&ctx->md5_ctx);
            ctx->md5_started = QM_TRUE;
            break;
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            qm_http_ota_hmac_sha256_free(ctx);
            ret = qm_utils_hmac_sha256_init(&ctx->hmac_sha256_ctx,
                                            (const uint8_t *)ctx->config.product_secret,
                                            qm_strlen(ctx->config.product_secret));
            if (ret != QM_EOK) {
                return ret;
            }
            ctx->hmac_sha256_started = QM_TRUE;
            break;
        default:
            return -QM_EINVAL;
    }

    remaining = ctx->result.downloaded_size;
    while (remaining > 0) {
        read_len = remaining > (uint32_t)ctx->buffer_size ?
                   (uint32_t)ctx->buffer_size : remaining;
        ret = qm_http_ota_target_read(ctx, &read_offset, (uint8_t *)ctx->buffer, read_len);
        if (ret != QM_EOK) {
            return ret < 0 ? ret : -QM_EIO;
        }
        ret = qm_http_ota_digest_update(ctx, (const uint8_t *)ctx->buffer, read_len);
        if (ret != QM_EOK) {
            return ret;
        }
        remaining -= read_len;
    }

    return QM_EOK;
}

static int qm_http_ota_md5_verify(qm_http_ota_ctx_t *ctx)
{
    uint8_t actual[QM_HTTP_OTA_MD5_DIGEST_LEN];
    char actual_hex[QM_HTTP_OTA_MD5_HEX_LEN + 1];
    char expected_hex[QM_HTTP_OTA_MD5_HEX_LEN + 1];
    qm_bool matched;

    if (!ctx || !ctx->md5_enabled) {
        return QM_EOK;
    }

    qm_utils_md5_finish(&ctx->md5_ctx, actual);
    ctx->md5_started = QM_FALSE;
    matched = qm_http_ota_digest_equal(actual, ctx->expected_md5,
                                       QM_HTTP_OTA_MD5_DIGEST_LEN);
    (void)qm_hex2str(actual, QM_HTTP_OTA_MD5_DIGEST_LEN, actual_hex, 1);
    (void)qm_hex2str(ctx->expected_md5, QM_HTTP_OTA_MD5_DIGEST_LEN, expected_hex, 1);
    actual_hex[QM_HTTP_OTA_MD5_HEX_LEN] = '\0';
    expected_hex[QM_HTTP_OTA_MD5_HEX_LEN] = '\0';
    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "md5 digest target=%s expected=%s actual=%s",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            expected_hex,
            actual_hex);
    if (!matched) {
        QM_HTTP_OTA_LOGE(LOG_TAG, "md5 mismatch target=%s downloaded=%u",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                (unsigned int)ctx->result.downloaded_size);
        qm_memset(actual, 0, sizeof(actual));
        return -QM_ERR_HTTP_OTA_MD5_MISMATCH;
    }
    qm_memset(actual, 0, sizeof(actual));

    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "md5 verified target=%s downloaded=%u",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            (unsigned int)ctx->result.downloaded_size);
    return QM_EOK;
}

static int qm_http_ota_hmac_sha256_verify_finish(qm_http_ota_ctx_t *ctx)
{
    uint8_t actual[QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN];
    char actual_hex[QM_HTTP_OTA_HMAC_SHA256_HEX_LEN + 1];
    char expected_hex[QM_HTTP_OTA_HMAC_SHA256_HEX_LEN + 1];
    qm_bool matched;
    int ret;

    if (!ctx || !ctx->hmac_sha256_enabled) {
        return QM_EOK;
    }

    ret = qm_utils_hmac_sha256_finish(&ctx->hmac_sha256_ctx, actual);
    ctx->hmac_sha256_started = QM_FALSE;
    if (ret != QM_EOK) {
        return ret;
    }
    matched = qm_http_ota_digest_equal(actual, ctx->expected_hmac_sha256,
                                       QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN);
    (void)qm_hex2str(actual, QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN, actual_hex, 1);
    (void)qm_hex2str(ctx->expected_hmac_sha256, QM_HTTP_OTA_HMAC_SHA256_DIGEST_LEN, expected_hex, 1);
    actual_hex[QM_HTTP_OTA_HMAC_SHA256_HEX_LEN] = '\0';
    expected_hex[QM_HTTP_OTA_HMAC_SHA256_HEX_LEN] = '\0';
    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "hmac digest target=%s expected=%s actual=%s",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            expected_hex,
            actual_hex);
    if (!matched) {
        QM_HTTP_OTA_LOGE(LOG_TAG, "hmac mismatch target=%s downloaded=%u",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                (unsigned int)ctx->result.downloaded_size);
        qm_memset(actual, 0, sizeof(actual));
        return -QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH;
    }
    qm_memset(actual, 0, sizeof(actual));

    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "hmac verified target=%s downloaded=%u",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            (unsigned int)ctx->result.downloaded_size);
    return QM_EOK;
}

static int qm_http_ota_digest_verify(qm_http_ota_ctx_t *ctx)
{
    const char *digest_name;
    const char *mode_name;
    int ret;

    if (!ctx || !qm_http_ota_digest_enabled(&ctx->config)) {
        return QM_EOK;
    }

    digest_name = qm_http_ota_digest_name(&ctx->config);
    mode_name = qm_http_ota_digest_verify_mode_name(&ctx->config);
    QM_UNUSED_ARG(digest_name);
    QM_UNUSED_ARG(mode_name);
    if (qm_http_ota_digest_readback_enabled(&ctx->config)) {
        QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "digest verify mode=readback target=%s digest=%s",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                digest_name);
        ret = qm_http_ota_digest_rebuild_from_target(ctx);
        if (ret != QM_EOK) {
            return ret;
        }
    } else {
        QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "digest verify mode=stream target=%s digest=%s",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                digest_name);
    }

    switch (ctx->config.digest_type) {
        case QM_HTTP_OTA_DIGEST_TYPE_MD5:
            ret = qm_http_ota_md5_verify(ctx);
            break;
        case QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256:
            ret = qm_http_ota_hmac_sha256_verify_finish(ctx);
            break;
        default:
            return -QM_EINVAL;
    }
    if (ret != QM_EOK) {
        return ret;
    }

    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_VERIFY, "digest verify success target=%s digest=%s mode=%s downloaded=%u",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            digest_name,
            mode_name,
            (unsigned int)ctx->result.downloaded_size);
    return QM_EOK;
}

int qm_http_ota_begin(const qm_http_ota_config_t *config, qm_http_ota_handle_t *handle)
{
    qm_http_ota_ctx_t *ctx;
    const qm_http_client_request_t *http_req;
    int buffer_size;
    int ret;

    if (!config || !config->http_config || !handle) {
        return -QM_EINVAL;
    }
    http_req = config->http_config;
    if (!http_req->url || http_req->header_count < 0 || config->expected_total_size == 0) {
        return -QM_EINVAL;
    }
    ret = qm_http_ota_validate_target_config(config);
    if (ret != QM_EOK) {
        return ret;
    }

    ctx = (qm_http_ota_ctx_t *)qm_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return -QM_ENOMEM;
    }

    ctx->config = *config;
    qm_http_ota_try_load_resume(ctx);
    ctx->last_error = QM_EOK;
    ctx->last_progress_bucket = -1;
    ctx->state = QM_HTTP_OTA_STATE_INIT;
    ret = qm_http_ota_digest_init(ctx);
    if (ret != QM_EOK) {
        qm_free(ctx);
        return ret;
    }
    buffer_size = qm_http_ota_get_effective_rx_buf_size(http_req);
    if (buffer_size < 0) {
        qm_http_ota_digest_free(ctx);
        qm_free(ctx);
        return buffer_size;
    }
    ctx->buffer_size = buffer_size;
    ctx->buffer = (char *)qm_malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        qm_http_ota_digest_free(ctx);
        qm_free(ctx);
        return -QM_ENOMEM;
    }

    ret = qm_http_ota_build_client_config(ctx, http_req);
    if (ret != QM_EOK) {
        qm_http_ota_digest_free(ctx);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return ret;
    }

    ctx->client = qm_http_client_init(&ctx->client_config);
    if (!ctx->client) {
        qm_http_ota_digest_free(ctx);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return -QM_ENOMEM;
    }

    ret = qm_http_ota_apply_resume_headers(ctx);
    if (ret != QM_EOK) {
        qm_http_ota_digest_free(ctx);
        qm_http_client_cleanup(ctx->client);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return ret;
    }

    ret = qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_START, NULL, NULL, NULL, 0, QM_EOK);
    if (ret != QM_EOK) {
        qm_http_ota_digest_free(ctx);
        qm_http_client_cleanup(ctx->client);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return -QM_EIO;
    }

    if (!qm_http_ota_is_mcu_target(ctx)) {
        ret = qm_http_ota_target_begin(ctx);
        if (ret != QM_EOK) {
            ret = qm_http_ota_fail(ctx, ret);
            qm_http_ota_digest_free(ctx);
            qm_http_client_cleanup(ctx->client);
            qm_free(ctx->buffer);
            qm_free(ctx);
            return ret;
        }
    }

    ret = qm_http_client_open(ctx->client, 0);
    if (ret != QM_EOK) {
        ret = qm_http_ota_fail(ctx, ret);
        qm_http_ota_target_abort(ctx, ret);
        qm_http_ota_digest_free(ctx);
        qm_http_client_cleanup(ctx->client);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return ret;
    }

    *handle = ctx;
    return QM_EOK;
}

int qm_http_ota_perform(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    int ret;
    int read_len;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->state == QM_HTTP_OTA_STATE_SUCCESS) {
        return QM_EOK;
    }
    if (ctx->state == QM_HTTP_OTA_STATE_ABORTED || ctx->state == QM_HTTP_OTA_STATE_FAILED) {
        return ctx->last_error != QM_EOK ? ctx->last_error : -QM_EIO;
    }

    if (!ctx->headers_fetched) {
        ret = qm_http_client_fetch_headers(ctx->client);
        if (ret != QM_EOK) {
            return qm_http_ota_fail(ctx, ret);
        }
        ctx->headers_fetched = QM_TRUE;
        ret = qm_http_ota_update_meta(ctx);
        if (ret != QM_EOK) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = ret;
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
            return ctx->last_error;
        }

        if (ctx->resume_applied) {
            if (ctx->result.status_code == 206) {
                if (!ctx->content_range_validated) {
                    return qm_http_ota_fail(ctx, -QM_EIO);
                }
                ret = qm_http_ota_apply_expected_total_size(ctx);
                if (ret != QM_EOK) {
                    ctx->state = QM_HTTP_OTA_STATE_FAILED;
                    ctx->last_error = ret;
                    (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
                    return ctx->last_error;
                }
            } else if (ctx->result.status_code == 200) {
                ctx->resume_applied = QM_FALSE;
                ctx->config.resume_offset = 0;
                ctx->offset = 0;
                ctx->result.downloaded_size = 0;
                if (!ctx->response_if_range_captured) {
                    ctx->resume_record.if_range[0] = '\0';
                }
                (void)qm_http_ota_resume_clear(ctx->config.http_config->url);
            } else {
                return qm_http_ota_fail(ctx, -QM_EIO);
            }
        }

        if (ctx->result.status_code < 200 || ctx->result.status_code >= 300) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = -QM_EIO;
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
            return ctx->last_error;
        }

        ret = qm_http_ota_check_total_size(ctx, 0);
        if (ret != QM_EOK) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = ret;
            QM_HTTP_OTA_LOGE(LOG_TAG, "invalid ota size target=%s downloaded=%u total=%u expected=%u",
                    qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                    (unsigned int)ctx->result.downloaded_size,
                    (unsigned int)ctx->result.total_size,
                    (unsigned int)ctx->config.expected_total_size);
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
            return ctx->last_error;
        }

        QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_LIFECYCLE, "headers ready target=%s status=%d total=%u resume=%d offset=%u",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                ctx->result.status_code,
                (unsigned int)ctx->result.total_size,
                ctx->resume_applied,
                (unsigned int)ctx->config.resume_offset);
        ret = qm_http_ota_target_begin(ctx);
        if (ret != QM_EOK) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = ret;
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
            return ctx->last_error;
        }
        ctx->state = QM_HTTP_OTA_STATE_IN_PROGRESS;
    }

    read_len = qm_http_client_read(ctx->client, ctx->buffer, ctx->buffer_size);
    if (read_len < 0) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = read_len;
        QM_HTTP_OTA_LOGE(LOG_TAG, "http read failed target=%s err=%d downloaded=%u",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                read_len,
                (unsigned int)ctx->result.downloaded_size);
        (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
        return read_len;
    }
    if (read_len == 0) {
        if (ctx->result.downloaded_size != ctx->result.total_size) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = -QM_EIO;
            QM_HTTP_OTA_LOGE(LOG_TAG, "ota size mismatch target=%s downloaded=%u total=%u",
                    qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                    (unsigned int)ctx->result.downloaded_size,
                    (unsigned int)ctx->result.total_size);
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
            return ctx->last_error;
        }
        ctx->state = QM_HTTP_OTA_STATE_SUCCESS;
        if (!ctx->finish_event_sent) {
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_FINISH, NULL, NULL, NULL, 0, QM_EOK);
            ctx->finish_event_sent = QM_TRUE;
        }
        return QM_EOK;
    }

    ret = qm_http_ota_check_total_size(ctx, (uint32_t)read_len);
    if (ret != QM_EOK) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = ret;
        QM_HTTP_OTA_LOGE(LOG_TAG, "ota size overflow target=%s downloaded=%u chunk=%d total=%u expected=%u",
                qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
                (unsigned int)ctx->result.downloaded_size,
                read_len,
                (unsigned int)ctx->result.total_size,
                (unsigned int)ctx->config.expected_total_size);
        (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
        return ctx->last_error;
    }

    if (qm_http_ota_digest_stream_enabled(&ctx->config)) {
        ret = qm_http_ota_digest_update(ctx, (const uint8_t *)ctx->buffer,
                                        (uint32_t)read_len);
        if (ret != QM_EOK) {
            ctx->state = QM_HTTP_OTA_STATE_FAILED;
            ctx->last_error = ret;
            (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL,
                                       NULL, 0, ctx->last_error);
            return ctx->last_error;
        }
    }

    ret = qm_http_ota_target_write(ctx, (const uint8_t *)ctx->buffer, (uint32_t)read_len);
    if (ret != QM_EOK) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = ret < 0 ? ret : -QM_EIO;
        (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL, NULL, 0, ctx->last_error);
        return ctx->last_error;
    }

    ctx->result.downloaded_size += (uint32_t)read_len;
    if (ctx->config.resume_enable) {
        (void)qm_http_ota_resume_save(ctx);
    }
    ret = qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_DATA, NULL, NULL,
                               ctx->buffer, read_len, QM_EOK);
    if (ret != QM_EOK) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = -QM_EIO;
        return ctx->last_error;
    }
    ret = qm_http_ota_dispatch_progress(ctx);
    if (ret != QM_EOK) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = -QM_EIO;
        return ctx->last_error;
    }

    return -QM_ERR_HTTP_OTA_IN_PROGRESS;
}

qm_bool qm_http_ota_is_complete_data_received(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    if (!ctx) {
        return QM_FALSE;
    }
    return ctx->result.downloaded_size == ctx->result.total_size;
}

int qm_http_ota_finish(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    const char *digest_name;
    int ret;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->state != QM_HTTP_OTA_STATE_SUCCESS) {
        return -QM_EIO;
    }
    if (!qm_http_ota_is_complete_data_received(handle)) {
        return -QM_EIO;
    }

    digest_name = qm_http_ota_digest_name(&ctx->config);
    QM_UNUSED_ARG(digest_name);
    QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_LIFECYCLE, "finish start target=%s downloaded=%u total=%u digest=%s",
            qm_http_ota_is_mcu_target(ctx) ? "mcu" : "soc",
            (unsigned int)ctx->result.downloaded_size,
            (unsigned int)ctx->result.total_size,
            digest_name);

    ret = qm_http_ota_digest_verify(ctx);
    if (ret != QM_EOK) {
        ctx->state = QM_HTTP_OTA_STATE_FAILED;
        ctx->last_error = ret < 0 ? ret : -QM_EIO;
        (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ERROR, NULL, NULL,
                                   NULL, 0, ctx->last_error);
        qm_http_ota_digest_free(ctx);
        qm_http_ota_target_abort(ctx, ctx->last_error);
        (void)qm_http_ota_resume_clear(ctx->config.http_config->url);
        qm_http_client_cleanup(ctx->client);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return ctx->last_error;
    }

    ret = qm_http_ota_target_finish(ctx);
    if (ret != QM_EOK) {
        ret = qm_http_ota_fail(ctx, ret);
        qm_http_ota_target_abort(ctx, ret);
        qm_http_client_cleanup(ctx->client);
        qm_free(ctx->buffer);
        qm_free(ctx);
        return ret;
    }

    qm_http_client_cleanup(ctx->client);
    qm_free(ctx->buffer);

    (void)qm_http_ota_resume_clear(ctx->config.http_config->url);
    if (!qm_http_ota_is_mcu_target(ctx) && ctx->config.auto_restart) {
        QM_HTTP_OTA_LOGI(LOG_TAG, QM_HTTP_OTA_LOG_CONTROL, "auto restart enabled for soc target");
        qm_reboot();
    }

    qm_free(ctx);
    return QM_EOK;
}

int qm_http_ota_abort(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;

    if (!ctx) {
        return -QM_EINVAL;
    }

    ctx->state = QM_HTTP_OTA_STATE_ABORTED;
    ctx->last_error = -QM_EINTR;
    (void)qm_http_ota_dispatch(ctx, QM_HTTP_OTA_EVENT_ABORT, NULL, NULL, NULL, 0, ctx->last_error);
    qm_http_ota_digest_free(ctx);
    qm_http_ota_target_abort(ctx, ctx->last_error);
    qm_http_client_cleanup(ctx->client);
    qm_free(ctx->buffer);
    qm_free(ctx);
    return QM_EOK;
}

int qm_http_ota_get_image_len_read(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    if (!ctx) {
        return -1;
    }
    return (int)ctx->result.downloaded_size;
}

int qm_http_ota_get_status_code(qm_http_ota_handle_t handle)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    if (!ctx) {
        return -QM_EINVAL;
    }
    return ctx->result.status_code;
}

int qm_http_ota_get_result(qm_http_ota_handle_t handle, qm_http_ota_result_t *result)
{
    qm_http_ota_ctx_t *ctx = (qm_http_ota_ctx_t *)handle;
    if (!ctx || !result) {
        return -QM_EINVAL;
    }
    *result = ctx->result;
    return QM_EOK;
}

int qm_http_ota(const qm_http_ota_config_t *config)
{
    qm_http_ota_handle_t handle;
    int ret;

    ret = qm_http_ota_begin(config, &handle);
    if (ret != QM_EOK) {
        return ret;
    }

    do {
        ret = qm_http_ota_perform(handle);
    } while (ret == -QM_ERR_HTTP_OTA_IN_PROGRESS);

    if (ret != QM_EOK) {
        (void)qm_http_ota_abort(handle);
        return ret;
    }

    return qm_http_ota_finish(handle);
}

int qm_http_ota_resume_info_get(const char *url, qm_http_ota_resume_info_t *info)
{
    char key[QM_HTTP_OTA_RESUME_KEY_MAX + 1];
    int len;
    int ret;

    if (!url || !info) {
        return -QM_EINVAL;
    }

    qm_memset(info, 0, sizeof(*info));
    ret = qm_http_ota_resume_key_make(url, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_kv_init();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }

    len = (int)sizeof(*info);
    ret = qm_kv_get(key, info, &len);
    if (ret != QM_EOK) {
        return ret;
    }
    if (len != (int)sizeof(*info) || !info->valid) {
        return -QM_EIO;
    }
    if (qm_strcmp(info->url, url) != 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}

int qm_http_ota_resume_info_clear(const char *url)
{
    if (!url) {
        return -QM_EINVAL;
    }
    return qm_http_ota_resume_clear(url);
}
