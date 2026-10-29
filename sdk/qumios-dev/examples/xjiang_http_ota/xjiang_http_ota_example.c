#include "xjiang_http_ota.h"

#include "qm.h"
#include "qm_errno.h"
#include "qm_event.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "xjiang_http_ota_example"

#if defined(__GNUC__)
#define XJIANG_HTTP_OTA_EXAMPLE_MAYBE_UNUSED __attribute__((unused))
#else
#define XJIANG_HTTP_OTA_EXAMPLE_MAYBE_UNUSED
#endif

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "your_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD "your_password"
#endif

#ifndef XJIANG_HTTP_OTA_EXAMPLE_PID
#define XJIANG_HTTP_OTA_EXAMPLE_PID ""
#endif

#ifndef XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION
#define XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION "0.0.1"
#endif

#ifndef XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION
#define XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION "0.0.1"
#endif

#ifndef XJIANG_HTTP_OTA_EXAMPLE_PRODUCT_SECRET
#define XJIANG_HTTP_OTA_EXAMPLE_PRODUCT_SECRET ""
#endif

#ifndef XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN
#define XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN ""
#endif

#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10

typedef struct {
    uint32_t total_size;
    uint32_t written_size;
    qm_bool active;
} xjiang_http_ota_example_mcu_ctx_t;

static int s_wifi_retry_count;
static qm_bool s_xjiang_ota_demo_started;
static xjiang_http_ota_example_mcu_ctx_t s_mcu_ota_ctx;

static void xjiang_http_ota_example_mask_url(const char *url,
                                             char *out,
                                             uint32_t out_len)
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

static int xjiang_http_ota_example_mcu_begin(void *user,
                                             const qm_http_ota_mcu_begin_info_t *info)
{
    xjiang_http_ota_example_mcu_ctx_t *ctx = (xjiang_http_ota_example_mcu_ctx_t *)user;
    char masked_url[32];

    if (!ctx || !info) {
        return -QM_EINVAL;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    ctx->total_size = info->total_size;
    ctx->written_size = info->resume_offset;
    ctx->active = QM_TRUE;

    xjiang_http_ota_example_mask_url(info->url, masked_url, sizeof(masked_url));
    QM_LOGI(LOG_TAG, "mcu ota begin url=%s resume=%u total=%u",
            masked_url,
            (unsigned int)info->resume_offset,
            (unsigned int)info->total_size);
    return QM_EOK;
}

static int xjiang_http_ota_example_mcu_write(void *user,
                                             uint32_t offset,
                                             const uint8_t *data,
                                             uint32_t len)
{
    xjiang_http_ota_example_mcu_ctx_t *ctx = (xjiang_http_ota_example_mcu_ctx_t *)user;

    if (!ctx || !ctx->active || (!data && len > 0) || offset != ctx->written_size) {
        return -QM_EINVAL;
    }
    if (ctx->total_size > 0 &&
        (offset > ctx->total_size || len > ctx->total_size - offset)) {
        return -QM_EFULL;
    }

    QM_LOGI(LOG_TAG, "mcu ota business write offset=%u len=%u",
            (unsigned int)offset,
            (unsigned int)len);
    ctx->written_size = offset + len;
    return QM_EOK;
}

static int xjiang_http_ota_example_mcu_finish(void *user)
{
    xjiang_http_ota_example_mcu_ctx_t *ctx = (xjiang_http_ota_example_mcu_ctx_t *)user;

    if (!ctx || !ctx->active) {
        return -QM_EINVAL;
    }

    QM_LOGI(LOG_TAG, "mcu ota finish written=%u total=%u",
            (unsigned int)ctx->written_size,
            (unsigned int)ctx->total_size);
    ctx->active = QM_FALSE;
    return QM_EOK;
}

static int xjiang_http_ota_example_mcu_abort(void *user, int error_code)
{
    xjiang_http_ota_example_mcu_ctx_t *ctx = (xjiang_http_ota_example_mcu_ctx_t *)user;

    QM_UNUSED_ARG(error_code);
    if (ctx) {
        ctx->active = QM_FALSE;
    }
    QM_LOGW(LOG_TAG, "mcu ota abort error=%d", error_code);
    return QM_EOK;
}

static const qm_http_ota_mcu_ops_t s_mcu_ota_ops = {
    .begin = xjiang_http_ota_example_mcu_begin,
    .write = xjiang_http_ota_example_mcu_write,
    .finish = xjiang_http_ota_example_mcu_finish,
    .abort = xjiang_http_ota_example_mcu_abort,
};

static const qm_http_ota_mcu_config_t s_mcu_ota_config = {
    .ops = &s_mcu_ota_ops,
    .user = &s_mcu_ota_ctx,
};

static XJIANG_HTTP_OTA_EXAMPLE_MAYBE_UNUSED const char *
xjiang_http_ota_example_target_name(xjiang_http_ota_target_t target)
{
    return target == XJIANG_HTTP_OTA_TARGET_MCU ? "mcu" : "module";
}

static void xjiang_http_ota_example_log_firmware(const char *name,
                                                 const xjiang_http_ota_firmware_info_t *info)
{
    char masked_url[32];

    if (!name || !info) {
        return;
    }

    xjiang_http_ota_example_mask_url(info->url, masked_url, sizeof(masked_url));
    QM_LOGI(LOG_TAG,
            "%s available=%d version=%s file=%s size=%u sign=%s url=%s",
            name,
            info->available,
            info->version ? info->version : "",
            info->file_name ? info->file_name : "",
            (unsigned int)info->file_size,
            info->sign ? info->sign : "",
            masked_url);
}

void xjiang_http_ota_example_check_start(void)
{
    xjiang_http_ota_info_t info;
    int ret;

    if (XJIANG_HTTP_OTA_EXAMPLE_PID[0] == '\0') {
        QM_LOGE(LOG_TAG, "set XJIANG_HTTP_OTA_EXAMPLE_PID before running");
        return;
    }

    ret = xjiang_http_ota_check(XJIANG_HTTP_OTA_EXAMPLE_PID,
                                XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION,
                                XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION,
                                XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN,
                                &info);
    QM_LOGI(LOG_TAG, "xjiang_http_ota_check ret=%d status=%d", ret, info.status_code);
    if (ret != QM_EOK) {
        return;
    }

    xjiang_http_ota_example_log_firmware("module", &info.module);
    xjiang_http_ota_example_log_firmware("mcu", &info.mcu);
    xjiang_http_ota_info_free(&info);
}

void xjiang_http_ota_example_start(void)
{
    qm_http_ota_result_t result;
    xjiang_http_ota_info_t info;
    xjiang_http_ota_target_t target;
    qm_bool module_upgraded = QM_FALSE;
    qm_bool mcu_upgraded = QM_FALSE;
    int ret;

    if (XJIANG_HTTP_OTA_EXAMPLE_PID[0] == '\0') {
        QM_LOGE(LOG_TAG, "set XJIANG_HTTP_OTA_EXAMPLE_PID before running");
        return;
    }

    QM_LOGI(LOG_TAG, "start xjiang OTA module=%s mcu=%s",
            XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION,
            XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION);

    ret = xjiang_http_ota_set_product_secret(XJIANG_HTTP_OTA_EXAMPLE_PRODUCT_SECRET);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "set product secret failed: %d", ret);
        return;
    }
    ret = xjiang_http_ota_check(XJIANG_HTTP_OTA_EXAMPLE_PID,
                                XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION,
                                XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION,
                                XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN,
                                &info);
    QM_LOGI(LOG_TAG, "xjiang_http_ota_check ret=%d status=%d", ret, info.status_code);
    if (ret != QM_EOK) {
        return;
    }

    xjiang_http_ota_example_log_firmware("module", &info.module);
    xjiang_http_ota_example_log_firmware("mcu", &info.mcu);
    QM_LOGI(LOG_TAG, "available module=%d mcu=%d",
            info.module.available,
            info.mcu.available);

    if (info.module.available) {
        target = XJIANG_HTTP_OTA_TARGET_MODULE;
    } else if (info.mcu.available) {
        target = XJIANG_HTTP_OTA_TARGET_MCU;
    } else {
        QM_LOGI(LOG_TAG, "no upgrade available");
        xjiang_http_ota_info_free(&info);
        return;
    }

    if (target == XJIANG_HTTP_OTA_TARGET_MCU) {
        ret = xjiang_http_ota_set_mcu_ota(&s_mcu_ota_config);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "set mcu ota callbacks failed: %d", ret);
            xjiang_http_ota_info_free(&info);
            return;
        }
    }

    qm_memset(&result, 0, sizeof(result));
    ret = xjiang_http_ota_perform(&info, target, &result);
    QM_LOGI(LOG_TAG,
            "xjiang_http_ota_perform ret=%d target=%s downloaded=%u/%u",
            ret,
            xjiang_http_ota_example_target_name(target),
            (unsigned int)result.downloaded_size,
            (unsigned int)result.total_size);

    if (ret == QM_EOK) {
        module_upgraded = target == XJIANG_HTTP_OTA_TARGET_MODULE ? QM_TRUE : QM_FALSE;
        mcu_upgraded = target == XJIANG_HTTP_OTA_TARGET_MCU ? QM_TRUE : QM_FALSE;
    }

    xjiang_http_ota_example_log_firmware("module", &info.module);
    xjiang_http_ota_example_log_firmware("mcu", &info.mcu);
    QM_LOGI(LOG_TAG, "available module=%d mcu=%d",
            info.module.available,
            info.mcu.available);
    QM_LOGI(LOG_TAG, "upgraded module=%d mcu=%d",
            module_upgraded,
            mcu_upgraded);
    QM_LOGI(LOG_TAG, "skipped module=%d mcu=%d",
            info.module.available && !module_upgraded,
            info.mcu.available && !mcu_upgraded);
    QM_UNUSED_ARG(module_upgraded);
    QM_UNUSED_ARG(mcu_upgraded);

    if (ret == -QM_ERR_XJIANG_HTTP_OTA_NO_UPGRADE) {
        QM_LOGI(LOG_TAG, "no upgrade available");
    } else if (ret == -QM_ERR_XJIANG_HTTP_OTA_UPGRADE_NOT_ALLOWED) {
        QM_LOGI(LOG_TAG, "upgrade available but not allowed");
    }
    xjiang_http_ota_info_free(&info);
}

static void xjiang_http_ota_example_wifi_event_handler(void *event_handler_arg,
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
            QM_LOGI(LOG_TAG, "ip:" QM_IPSTR,
                    QM_IP2STR(&event_info->got_ip.ip_info.ip));
            QM_LOGI(LOG_TAG, "netmask:" QM_IPSTR,
                    QM_IP2STR(&event_info->got_ip.ip_info.netmask));
            QM_LOGI(LOG_TAG, "gw:" QM_IPSTR,
                    QM_IP2STR(&event_info->got_ip.ip_info.gw));
            if (!s_xjiang_ota_demo_started) {
                s_xjiang_ota_demo_started = QM_TRUE;
                xjiang_http_ota_example_start();
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            s_xjiang_ota_demo_started = QM_FALSE;
            if (!event_info) {
                QM_LOGW(LOG_TAG, "wifi disconnected");
                break;
            }
            QM_LOGW(LOG_TAG, "wifi disconnected reason=%d",
                    event_info->sta_disconnected.reason);
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

void xjiang_http_ota_wifi_demo_start(void)
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

    QM_LOGI(LOG_TAG, "xjiang http ota wifi demo start");

    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                   xjiang_http_ota_example_wifi_event_handler, NULL);

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
    }
}
