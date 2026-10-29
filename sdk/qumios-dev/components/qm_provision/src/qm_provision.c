#include "qm_provision.h"

#include "qm_kernel.h"
#include "qm_spec_core.h"
#include "qm_spec_service.h"
#include "qm_string.h"
#include "qm_work.h"

#include "qm_ble_common.h"
#include "qm_ble_core.h"
#include "qm_ble_export.h"
#include "qm_ble_gap.h"

#define LOG_TAG "qm provision"

#ifndef CONFIG_QM_PROVISION_NTP_TIMEZONE
#define CONFIG_QM_PROVISION_NTP_TIMEZONE 8
#endif

#ifndef CONFIG_QM_PROVISION_BLE_ADV_CID
#define CONFIG_QM_PROVISION_BLE_ADV_CID "xj"
#endif

#ifndef CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS
#define CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS 500
#endif

#ifndef CONFIG_QM_PROVISION_BLE_LOCAL_NAME_MAX_LEN
#define CONFIG_QM_PROVISION_BLE_LOCAL_NAME_MAX_LEN 24
#endif

#ifndef CONFIG_QM_PROVISION_APINFO_ALT_FORMAT
#define CONFIG_QM_PROVISION_APINFO_ALT_FORMAT 0
#endif

#define QM_PROVISION_BLE_LOCAL_NAME_SIZE (CONFIG_QM_PROVISION_BLE_LOCAL_NAME_MAX_LEN + 1)
#define QM_PROVISION_BLE_ADV_NAME "%s-%u-%02x%02x"
#define QM_PROVISION_ALT_STRING_TYPE 0x0B
#define QM_PROVISION_ALT_INT_TYPE    0x01
#define QM_PROVISION_ALT_STRING_META_LEN 3
#define QM_PROVISION_SPEC_TYPE_MASK 0x7FU

/**
 * @brief Internal BLE provisioning session state.
 */
struct qm_provision {
    qm_provision_config_t config;
    qm_mutex_t lock;
    qm_work_t deinit_work;
    qm_bool lock_valid;
    qm_bool started;
    qm_bool ble_initialized;
    qm_bool ble_connected;
    qm_bool disconnect_pending;
    qm_bool need_deinit;
    char local_name[QM_PROVISION_BLE_LOCAL_NAME_SIZE];
};

static qm_provision_handle_t g_ble_provision = NULL;
static volatile uint32_t g_qm_provision_log_flags =
    QM_PROVISION_KCONFIG_LOG_FLAGS;

uint32_t qm_provision_get_log_flags(void)
{
    return g_qm_provision_log_flags;
}

int qm_provision_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_PROVISION_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }

    g_qm_provision_log_flags = log_flags;
    return QM_EOK;
}

static int32_t ble_start(qm_provision_handle_t handle);
static int32_t ble_stop(qm_provision_handle_t handle);
static void release_handle(qm_provision_handle_t handle);
static void ble_deinit_action(void *arg);
static void ble_disconnect_action(void *arg);
static void ble_status_cb(qm_ble_event_t event);
#if (QB_VERSION < 3)
static void ble_apinfo_cb(qm_ble_apinfo_t *ap);
#else
static void ble_apinfo_cb(uint8_t *data, uint32_t len);
#endif
static void ble_set_status_cb(uint8_t msg_id, uint8_t cmd,
                              uint8_t *buffer, uint32_t length);
static void ble_get_status_cb(uint8_t msg_id, uint8_t cmd,
                              uint8_t *buffer, uint32_t length);
static void ble_response_cb(uint8_t msg_id, uint8_t cmd,
                            uint8_t *buffer, uint32_t length);
static void ble_extinfo_cb(uint8_t msg_id, uint8_t cmd,
                           uint8_t *buffer, uint32_t len);

static qm_bool config_valid(const qm_provision_config_t *config)
{
    if (config == NULL ||
        config->product_id == 0 ||
        config->product_secret == NULL ||
        config->product_secret[0] == '\0') {
        return QM_FALSE;
    }

    return QM_TRUE;
}

qm_provision_handle_t qm_provision_create(const qm_provision_config_t *config)
{
    qm_provision_handle_t handle;

    if (!config_valid(config)) {
        return NULL;
    }

    handle = (qm_provision_handle_t)qm_malloc(sizeof(struct qm_provision));
    if (handle == NULL) {
        return NULL;
    }

    qm_memset(handle, 0, sizeof(struct qm_provision));
    handle->config = *config;

    if (qm_mutex_new(&handle->lock) != QM_EOK) {
        qm_free(handle);
        return NULL;
    }

    handle->lock_valid = QM_TRUE;

    return handle;
}

void qm_provision_destroy(qm_provision_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    handle->started = QM_FALSE;
    handle->need_deinit = QM_TRUE;
    qm_mutex_unlock(&handle->lock);

    if (g_ble_provision == handle) {
        (void)ble_stop(handle);
    } else {
        release_handle(handle);
    }
}

int32_t qm_provision_start(qm_provision_handle_t handle)
{
    int32_t ret = QM_EOK;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->started) {
        qm_mutex_unlock(&handle->lock);
        return QM_EOK;
    }
    if (handle->disconnect_pending) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EBUSY;
    }
    handle->started = QM_TRUE;
    handle->need_deinit = QM_FALSE;
    qm_mutex_unlock(&handle->lock);

    ret = ble_start(handle);
    if (ret != QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->started = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
        return ret;
    }

    return QM_EOK;
}

int32_t qm_provision_stop(qm_provision_handle_t handle)
{
    qm_bool was_started;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    was_started = handle->started;
    handle->started = QM_FALSE;
    qm_mutex_unlock(&handle->lock);

    if (!was_started) {
        return QM_EOK;
    }

    return ble_stop(handle);
}

static int32_t ble_start(qm_provision_handle_t handle)
{
    uint32_t ble_ret;
    uint8_t mac[6] = {0};
    qm_ble_dev_info_t dinfo = {0};
    const char *adv_cid;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (g_ble_provision != NULL && g_ble_provision != handle) {
        return -QM_EBUSY;
    }

    g_ble_provision = handle;
    adv_cid = handle->config.adv_cid ?
              handle->config.adv_cid :
              CONFIG_QM_PROVISION_BLE_ADV_CID;

    /* Keep the same BLE authentication inputs as the legacy qm_cloud provision flow. */
    dinfo.product_id = handle->config.product_id;
    dinfo.dev_adv_mac = mac;
    dinfo.product_secret = (char *)handle->config.product_secret;

    if (!handle->ble_initialized) {
        ble_ret = qm_ble_init(&dinfo,
                              ble_status_cb,
                              ble_set_status_cb,
                              ble_get_status_cb,
                              ble_response_cb,
                              ble_extinfo_cb,
                              ble_apinfo_cb,
                              NULL);
        if (ble_ret != QB_SUCCESS) {
            QM_PROVISION_LOGE(LOG_TAG, "BLE init failed ret=%u",
                              (unsigned int)ble_ret);
            g_ble_provision = NULL;
            (void)qm_ble_end();
            return -QM_EIO;
        }
        handle->ble_initialized = QM_TRUE;
    }

    if (qm_ble_gap_get_local_addr(mac) != QM_EOK) {
        QM_PROVISION_LOGW(LOG_TAG, QM_PROVISION_LOG_LIFECYCLE,
                          "BLE local address unavailable");
    }
    qm_ble_core_device_id_set(mac, 6);

    /* Advertising name format: <cid>-<product_id>-<last two mac bytes>. */
    qm_snprintf(handle->local_name,
                sizeof(handle->local_name),
                QM_PROVISION_BLE_ADV_NAME,
                adv_cid,
                handle->config.product_id,
                mac[4],
                mac[5]);

    return (int32_t)qm_ble_start_advertising(3, 1, 0, handle->local_name);
}

static int32_t ble_stop(qm_provision_handle_t handle)
{
    if (handle == NULL || g_ble_provision != handle) {
        return -QM_EINVAL;
    }

    if (!handle->ble_connected) {
        qm_ble_stop_advertising();

        if (handle->need_deinit) {
            g_ble_provision = NULL;
            if (handle->ble_initialized) {
                (void)qm_ble_end();
                handle->ble_initialized = QM_FALSE;
            }
            release_handle(handle);
        }
        return QM_EOK;
    }

    /* Prevent an expected terminal disconnect from restarting advertising. */
    qm_ble_stop_advertising();
    handle->disconnect_pending = QM_TRUE;
    (void)qm_cancel_delayed_action(&handle->deinit_work);
    if (qm_post_delayed_action(&handle->deinit_work,
                               ble_disconnect_action,
                               NULL,
                               CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS) != QM_EOK) {
        QM_PROVISION_LOGW(LOG_TAG, QM_PROVISION_LOG_LIFECYCLE,
                          "queue BLE disconnect failed, disconnect now");
        qm_ble_disconnect_ble();
    }

    return QM_EOK;
}

static void release_handle(qm_provision_handle_t handle)
{
    if (handle->lock_valid) {
        qm_mutex_free(&handle->lock);
        handle->lock_valid = QM_FALSE;
    }

    qm_free(handle);
}

static void ble_deinit_action(void *arg)
{
    qm_provision_handle_t handle = g_ble_provision;

    (void)arg;

    if (handle == NULL || handle->ble_connected) {
        return;
    }

    g_ble_provision = NULL;
    if (handle->ble_initialized) {
        (void)qm_ble_end();
        handle->ble_initialized = QM_FALSE;
    }

    release_handle(handle);
}

static void ble_disconnect_action(void *arg)
{
    (void)arg;

    if (g_ble_provision != NULL && g_ble_provision->ble_connected) {
        qm_ble_disconnect_ble();
    }
}

static void ble_status_cb(qm_ble_event_t event)
{
    qm_provision_handle_t handle = g_ble_provision;
    int32_t ret;

    if (handle == NULL) {
        return;
    }

    switch (event) {
        case QM_BLE_CONNECTED:
            handle->ble_connected = QM_TRUE;
            QM_PROVISION_LOGI(LOG_TAG, QM_PROVISION_LOG_LIFECYCLE,
                              "BLE connected");
            break;

        case QM_BLE_DISCONNECTED:
            handle->ble_connected = QM_FALSE;
            handle->disconnect_pending = QM_FALSE;
            QM_PROVISION_LOGI(LOG_TAG, QM_PROVISION_LOG_LIFECYCLE,
                              "BLE disconnected");

            (void)qm_cancel_delayed_action(&handle->deinit_work);
            if (handle->need_deinit) {
                ret = qm_post_delayed_action(
                    &handle->deinit_work,
                    ble_deinit_action,
                    NULL,
                    CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS);
                if (ret != QM_EOK &&
                    CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS != 0) {
                    ret = qm_post_delayed_action(&handle->deinit_work,
                                                 ble_deinit_action,
                                                 NULL,
                                                 0);
                }
                if (ret != QM_EOK) {
                    QM_PROVISION_LOGE(LOG_TAG,
                                      "queue BLE deinit failed ret=%d",
                                      (int)ret);
                }
            } else if (handle->started) {
                /* Keep advertising available until the user explicitly stops provisioning. */
                qm_ble_start_advertising(3, 1, 0, handle->local_name);
            }
            break;

        case QM_BLE_AUTHENTICATED:
            QM_PROVISION_LOGI(LOG_TAG, QM_PROVISION_LOG_PROTOCOL,
                              "BLE authenticated");
            break;
        default:
            break;
    }
}

static void copy_string(char *dst, uint32_t dst_size,
                        const char *src, int src_len)
{
    uint32_t copy_len;

    if (dst == NULL || dst_size == 0 || src == NULL || src_len <= 0) {
        return;
    }

    copy_len = ((uint32_t)src_len >= dst_size) ? (dst_size - 1) : (uint32_t)src_len;
    qm_memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void copy_authcode(qm_provision_recv_t *recv,
                          const uint8_t *src,
                          uint32_t src_len)
{
    uint32_t copy_len;

    if (recv == NULL || src == NULL || src_len == 0) {
        return;
    }

    copy_len = (src_len >= sizeof(recv->authcode)) ?
               (sizeof(recv->authcode) - 1) : src_len;
    qm_memcpy(recv->authcode, src, copy_len);
    recv->authcode[copy_len] = '\0';
    recv->authcode_len = (uint8_t)copy_len;
}

#if (QB_VERSION < 3)
static void ble_apinfo_cb(qm_ble_apinfo_t *ap)
{
    qm_provision_recv_t recv = {0};

    if (g_ble_provision == NULL || ap == NULL || ap->ssid[0] == '\0') {
        return;
    }

    copy_string(recv.ssid, sizeof(recv.ssid), ap->ssid, qm_strlen(ap->ssid));
    copy_string(recv.password, sizeof(recv.password), ap->pw, qm_strlen(ap->pw));
    copy_authcode(&recv, ap->apptoken, ap->apptoken_len);
    recv.token_type = ap->token_type;
    recv.env_type = ap->env_type;
    recv.env_type_valid = ap->env_type_valid ? QM_TRUE : QM_FALSE;
    recv.timezone = CONFIG_QM_PROVISION_NTP_TIMEZONE;

    if (g_ble_provision->config.recv_handler != NULL) {
        g_ble_provision->config.recv_handler(g_ble_provision,
                                             &recv,
                                             g_ble_provision->config.userdata);
    }
}
#else
#if CONFIG_QM_PROVISION_APINFO_ALT_FORMAT
static int alt_read_string(uint8_t *data,
                           uint32_t len,
                           uint32_t *idx,
                           char *dst,
                           uint32_t dst_size)
{
    uint32_t value_len;

    if (data == NULL || idx == NULL || dst == NULL || dst_size == 0) {
        return -QM_EINVAL;
    }

    if (*idx + 1 + QM_PROVISION_ALT_STRING_META_LEN + 2 > len ||
        data[*idx] != QM_PROVISION_ALT_STRING_TYPE) {
        return -QM_EINVAL;
    }

    *idx += 1 + QM_PROVISION_ALT_STRING_META_LEN;
    value_len = ((uint32_t)data[*idx] << 8) | data[*idx + 1];
    *idx += 2;

    if (*idx + value_len > len) {
        return -QM_EINVAL;
    }

    copy_string(dst, dst_size, (const char *)&data[*idx], (int)value_len);
    *idx += value_len;
    return QM_EOK;
}

static int alt_read_int8(uint8_t *data,
                         uint32_t len,
                         uint32_t *idx,
                         int8_t *value)
{
    if (data == NULL || idx == NULL || value == NULL) {
        return -QM_EINVAL;
    }

    if (*idx + 3 > len || data[*idx] != QM_PROVISION_ALT_INT_TYPE) {
        return -QM_EINVAL;
    }

    *idx += 2;
    *value = (int8_t)data[*idx];
    *idx += 1;
    return QM_EOK;
}

static int apinfo_alt_unpack(uint8_t *data,
                             uint32_t len,
                             qm_provision_recv_t *recv)
{
    uint32_t idx = 0;

    if (recv == NULL) {
        return -QM_EINVAL;
    }

    recv->timezone = CONFIG_QM_PROVISION_NTP_TIMEZONE;

    if (alt_read_string(data, len, &idx,
                        recv->ssid, sizeof(recv->ssid)) != QM_EOK ||
        alt_read_string(data, len, &idx,
                        recv->password, sizeof(recv->password)) != QM_EOK) {
        return -QM_EINVAL;
    }

    (void)alt_read_int8(data, len, &idx, &recv->timezone);
    (void)alt_read_string(data, len, &idx,
                          recv->dynreg_host, sizeof(recv->dynreg_host));
    (void)alt_read_string(data, len, &idx,
                          recv->audio_uri, sizeof(recv->audio_uri));
    return (recv->ssid[0] != '\0') ? QM_EOK : -QM_EINVAL;
}
#endif

static void copy_property_string(qm_spec_property_operation_t *operation,
                                 uint16_t siid,
                                 uint16_t piid,
                                 char *dst,
                                 uint32_t dst_size)
{
    int string_len = 0;
    char *string_value = NULL;
    qm_spec_property_t *property;

    property = qm_spec_property_find(operation, siid, piid);
    if (property == NULL) {
        return;
    }

    qm_spec_property_unpack_string_direct(property, &string_value, &string_len);
    copy_string(dst, dst_size, string_value, string_len);
}

static void copy_property_authcode(qm_spec_property_operation_t *operation,
                                   uint16_t siid,
                                   uint16_t piid,
                                   qm_provision_recv_t *recv)
{
    int string_len = 0;
    char *string_value = NULL;
    qm_spec_property_t *property;

    property = qm_spec_property_find(operation, siid, piid);
    if (property == NULL) {
        return;
    }

    qm_spec_property_unpack_string_direct(property, &string_value, &string_len);
    if (string_len > 0) {
        copy_authcode(recv, (const uint8_t *)string_value, (uint32_t)string_len);
    }
}

static void ble_apinfo_cb(uint8_t *data, uint32_t len)
{
    qm_spec_property_t *property = NULL;
    qm_spec_property_operation_t *operation = NULL;
    qm_provision_recv_t recv = {0};

    if (g_ble_provision == NULL || data == NULL || len == 0) {
        return;
    }

    operation = qm_spec_property_operation_creat();
    if (operation == NULL) {
        return;
    }

    qm_spec_standard_service_unpack(QM_SPEC_DATA_TYPE_HEX, operation, data, len);

    copy_property_string(operation,
                         QM_SPEC_WIFI_SSID_SERVICE_SIID,
                         QM_SPEC_WIFI_SSID_SERVICE_PIID,
                         recv.ssid,
                         sizeof(recv.ssid));
    if (recv.ssid[0] == '\0') {
#if CONFIG_QM_PROVISION_APINFO_ALT_FORMAT
        if (apinfo_alt_unpack(data, len, &recv) == QM_EOK) {
            goto notify;
        }
#endif
        goto exit;
    }

    copy_property_string(operation,
                         QM_SPEC_WIFI_PASSWORD_SERVICE_SIID,
                         QM_SPEC_WIFI_PASSWORD_SERVICE_PIID,
                         recv.password,
                         sizeof(recv.password));

    recv.timezone = CONFIG_QM_PROVISION_NTP_TIMEZONE;
    property = qm_spec_property_find(operation,
                                     QM_SPEC_NTP_TIMEZONE_SIID,
                                     QM_SPEC_NTP_TIMEZONE_PIID);
    if (property != NULL) {
        qm_spec_property_unpack_int8(property, &recv.timezone);
    }

    copy_property_string(operation,
                         QM_SPEC_WIFI_DYNREG_HOST_SERVICE_SIID,
                         QM_SPEC_WIFI_DYNREG_HOST_SERVICE_PIID,
                         recv.dynreg_host,
                         sizeof(recv.dynreg_host));
    copy_property_string(operation,
                         QM_SPEC_AUDIO_RPOV_URI_SIID,
                         QM_SPEC_AUDIO_RPOV_URI_PIID,
                         recv.audio_uri,
                         sizeof(recv.audio_uri));

    recv.env_type_valid = QM_FALSE;
    property = qm_spec_property_find(operation,
                                     QM_SPEC_ENV_TYPE_SERVICE_SIID,
                                     QM_SPEC_ENV_TYPE_SERVICE_PIID);
    if (property != NULL &&
        (property->value.property_format & QM_PROVISION_SPEC_TYPE_MASK) ==
            QM_SPEC_PROPERTY_FORMAT_UINT8 &&
        property->value.len == sizeof(uint8_t) &&
        qm_spec_property_unpack_uint8(property, &recv.env_type) == QM_EOK) {
        recv.env_type_valid = QM_TRUE;
    }

    copy_property_authcode(operation,
                           QM_SPEC_BLE_APPKEY_SERVICE_SIID,
                           QM_SPEC_BLE_APPKEY_SERVICE_PIID,
                           &recv);
    if (recv.authcode_len == 0) {
        copy_property_authcode(operation,
                               QM_SPEC_BLE_NETKEY_SERVICE_SIID,
                               QM_SPEC_BLE_NETKEY_SERVICE_PIID,
                               &recv);
    }

#if CONFIG_QM_PROVISION_APINFO_ALT_FORMAT
notify:
#endif
    if (g_ble_provision->config.recv_handler != NULL) {
        g_ble_provision->config.recv_handler(g_ble_provision,
                                             &recv,
                                             g_ble_provision->config.userdata);
    }

exit:
    qm_spec_property_operation_delete(operation);
}
#endif

static void ble_set_status_cb(uint8_t msg_id, uint8_t cmd,
                              uint8_t *buffer, uint32_t length)
{
    (void)msg_id;
    (void)cmd;
    (void)buffer;
    (void)length;
}

static void ble_get_status_cb(uint8_t msg_id, uint8_t cmd,
                              uint8_t *buffer, uint32_t length)
{
    (void)msg_id;
    (void)cmd;
    (void)buffer;
    (void)length;
}

static void ble_response_cb(uint8_t msg_id, uint8_t cmd,
                            uint8_t *buffer, uint32_t length)
{
    (void)msg_id;
    (void)cmd;
    (void)buffer;
    (void)length;
}

static void ble_extinfo_cb(uint8_t msg_id, uint8_t cmd,
                           uint8_t *buffer, uint32_t len)
{
    (void)msg_id;
    (void)cmd;
    (void)buffer;
    (void)len;
}
