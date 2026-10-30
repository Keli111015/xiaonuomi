#include "ljyd_cloud.h"
#include "ljyd_method.h"
#include "ljyd_provision.h"
#include "time_sync.h"
#include "pomodoro.h"

#include "qm.h"

#if defined(CONFIG_APP_CAMERA_TEST) && CONFIG_APP_CAMERA_TEST
#include "bsp_camera.h"
#endif
#include "qm_iot_api.h"
#include "qm_iot_dynreg.h"
#include "qm_kv.h"
#include "qm_wifi.h"
#include "qm_ble_gap.h"
#include "qm_string.h"

#include <string.h>

#define LOG_TAG "cloud"

#define CLOUD_KV_DID       "cloud/did"
#define CLOUD_KV_HOST      "cloud/host"
#define CLOUD_KV_PORT      "cloud/port"
#define CLOUD_KV_SRV_CA    "cloud/s_ca"
#define CLOUD_KV_CLI_CRT   "cloud/c_ca"
#define CLOUD_KV_CLI_KEY   "cloud/c_key"
#define CLOUD_KV_PUB_KEY   "cloud/p_key"
#define CLOUD_CERT_MAX     (8 * 1024)

#define CLOUD_IOT_TASK_STACK (16 * 1024)
#define CLOUD_IOT_TASK_PRIO  5

static void *iot_handle;
static uint8_t cloud_started;
static uint8_t iot_task_running;
static ljyd_cloud_event_cb_t cloud_event_cb;
static void *cloud_event_userdata;
static ljyd_cloud_prop_cb_t cloud_prop_cb;

static char dynreg_host[128];
/* The four dynamic-registration certificates are only needed during dynreg.
 * Keep them in one PSRAM pool instead of 32 KB of internal .bss. */
static char *dynreg_pool;
static char *dynreg_server_ca;
static char *dynreg_client_crt;
static char *dynreg_client_key;
static char *dynreg_public_key;
static qm_iot_cloud_auth_t cloud_credential;
static char device_sn[13];

static int dynreg_pool_init(void)
{
    if (dynreg_pool != NULL) {
        return QM_EOK;
    }
    dynreg_pool = (char *)qm_malloc(CLOUD_CERT_MAX * 4U);
    if (dynreg_pool == NULL) {
        return -QM_ENOMEM;
    }
    memset(dynreg_pool, 0, CLOUD_CERT_MAX * 4U);
    dynreg_server_ca = dynreg_pool;
    dynreg_client_crt = dynreg_pool + CLOUD_CERT_MAX;
    dynreg_client_key = dynreg_pool + CLOUD_CERT_MAX * 2U;
    dynreg_public_key = dynreg_pool + CLOUD_CERT_MAX * 3U;
    return QM_EOK;
}

static int cloud_save_credential(const qm_iot_cloud_auth_t *credential)
{
    if (credential == NULL || credential->did == 0) {
        return -QM_EINVAL;
    }
    (void)qm_kv_set(CLOUD_KV_DID, (void *)&credential->did, (int)sizeof(credential->did), 1);
    (void)qm_kv_set(CLOUD_KV_HOST, credential->cloud_info.host,
                    (int)strlen(credential->cloud_info.host) + 1, 1);
    (void)qm_kv_set(CLOUD_KV_PORT, (void *)&credential->cloud_info.port,
                    (int)sizeof(credential->cloud_info.port), 1);
    if (credential->server_cert != NULL && credential->server_cert_len > 0) {
        (void)qm_kv_set(CLOUD_KV_SRV_CA, credential->server_cert,
                        (int)credential->server_cert_len, 1);
    }
    if (credential->client_crt != NULL && credential->client_crt_len > 0) {
        (void)qm_kv_set(CLOUD_KV_CLI_CRT, credential->client_crt,
                        (int)credential->client_crt_len, 1);
    }
    if (credential->client_key != NULL && credential->client_key_len > 0) {
        (void)qm_kv_set(CLOUD_KV_CLI_KEY, credential->client_key,
                        (int)credential->client_key_len, 1);
    }
    if (credential->ota_public_key != NULL && credential->ota_public_key_len > 0) {
        (void)qm_kv_set(CLOUD_KV_PUB_KEY, credential->ota_public_key,
                        (int)credential->ota_public_key_len, 1);
    }
    return QM_EOK;
}

static void refresh_device_sn(void)
{
    qm_bd_addr_t mac = {0};

    if (device_sn[0] != '\0') {
        return;
    }
    if (qm_ble_gap_get_local_addr(mac) != QM_EOK) {
        QM_LOGW(LOG_TAG, "get ble mac failed");
        return;
    }
    qm_snprintf(device_sn, sizeof(device_sn), "%02X%02X%02X%02X%02X%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void copy_len(char *dst, uint32_t dst_size, const char *src, uint32_t len)
{
    if (src == NULL || len == 0 || len >= dst_size) {
        return;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void dynreg_recv_handler(void *handle, const qm_iot_dynreg_recv_t *packet, void *userdata)
{
    const qm_iot_dynreg_device_info_t *info;

    (void)handle;
    (void)userdata;
    if (packet == NULL || packet->type != QM_IOT_DYNREGRECV_DEVICE_INFO) {
        return;
    }
    info = &packet->data.device_info;
    memset(&cloud_credential, 0, sizeof(cloud_credential));
    cloud_credential.did = info->did;
    copy_len(dynreg_host, sizeof(dynreg_host), info->mqtt_host, info->mqtt_host_len);
    strncpy(cloud_credential.cloud_info.host, dynreg_host, sizeof(cloud_credential.cloud_info.host) - 1);
    cloud_credential.cloud_info.port = info->mqtt_port ? info->mqtt_port : 8883;
    QM_LOGI(LOG_TAG, "dynreg did=%u host=%s port=%u",
            (unsigned)cloud_credential.did,
            cloud_credential.cloud_info.host,
            (unsigned)cloud_credential.cloud_info.port);
    copy_len(dynreg_server_ca, CLOUD_CERT_MAX, info->server_cert, info->server_cert_len);
    cloud_credential.server_cert = dynreg_server_ca;
    cloud_credential.server_cert_len = info->server_cert_len;
    copy_len(dynreg_client_crt, CLOUD_CERT_MAX, info->client_crt, info->client_cert_len);
    cloud_credential.client_crt = dynreg_client_crt;
    cloud_credential.client_crt_len = info->client_cert_len;
    copy_len(dynreg_client_key, CLOUD_CERT_MAX, info->client_private_key, info->client_privkey_len);
    cloud_credential.client_key = dynreg_client_key;
    cloud_credential.client_key_len = info->client_privkey_len;
    copy_len(dynreg_public_key, CLOUD_CERT_MAX, info->public_key, info->public_key_len);
    cloud_credential.ota_public_key = dynreg_public_key;
    cloud_credential.ota_public_key_len = info->public_key_len;
}

static int cloud_run_dynreg(void)
{
    void *dynreg_handle;
    uint32_t product_id = CONFIG_XJIANG_CLOUD_PID;
    uint16_t port = CONFIG_QM_IOT_DYNREG_PORT;
    uint32_t retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;
    uint32_t timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    int ret = QM_EOK;

    ret = dynreg_pool_init();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "dynreg cert pool alloc failed");
        return ret;
    }
    refresh_device_sn();
    if (device_sn[0] == '\0') {
        return -QM_EINVAL;
    }

    dynreg_handle = qm_iot_dynreg_init();
    if (dynreg_handle == NULL) {
        return -QM_ENOMEM;
    }
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PORT, &port);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PRODUCT_ID, &product_id);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PRODUCT_SECRET,
                                (void *)CONFIG_XJIANG_CLOUD_SECERT);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_SN, device_sn);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_MAX_RETRY_NUM, &retry_num);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_RECV_TIMEOUT_MS, &timeout);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_RECV_HANDLER, dynreg_recv_handler);
    if (ret == QM_EOK) {
        ret = qm_iot_dynreg_start(dynreg_handle, QM_IOT_NETWORK_TYPE_WIFI);
    }
    (void)qm_iot_dynreg_deinit(&dynreg_handle);
    if (ret != QM_EOK || cloud_credential.did == 0) {
        QM_LOGE(LOG_TAG, "dynreg failed ret=%d did=%u", ret, (unsigned)cloud_credential.did);
        return ret != QM_EOK ? ret : -QM_EINVAL;
    }
    ret = qm_iot_setopt(iot_handle, QM_IOT_OPT_CLOUD_CREDENTIAL, &cloud_credential);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "set credential failed ret=%d", ret);
        return ret;
    }
    return cloud_save_credential(&cloud_credential);
}

static void cloud_set_devinfo(void)
{
    qm_iot_devinfo_t info = {0};
    qm_wifi_ap_record_t ap = {0};
    uint8_t mac[6] = {0};

    info.type = QM_IOT_NETWORK_TYPE_WIFI;
    if (qm_wifi_get_mac(QM_WIFI_IF_STA, mac) == QM_EOK) {
        qm_snprintf(info.data.wifi.mac, sizeof(info.data.wifi.mac),
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    if (qm_wifi_sta_get_ap_info(&ap) == QM_EOK && ap.ssid_len > 0) {
        memcpy(info.data.wifi.ssid, ap.ssid, ap.ssid_len);
        qm_snprintf(info.data.wifi.bssid, sizeof(info.data.wifi.bssid),
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    ap.bssid[0], ap.bssid[1], ap.bssid[2],
                    ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        info.data.wifi.rssi = ap.rssi;
        info.data.wifi.channel = ap.channel;
    }
    (void)qm_iot_setopt(iot_handle, QM_IOT_OPT_DEVINFO, &info);
}

/*
 * is_reset 非 0 时，MQTT 连上也不发 CLOUD_CONN，要等 bindDevNotify。
 * 本地标记会停在 1，云不一定再推绑定通知。配网并动态注册之后，
 * 启动前改成未复位，让连接成功事件发出来。
 */
static void cloud_migrate_reset_marker(void)
{
    qm_iot_reset_info_t reset_info;
    int ret;
    int was;

    if (iot_handle == NULL) {
        return;
    }
    memset(&reset_info, 0, sizeof(reset_info));
    ret = qm_iot_reset_info_get(iot_handle, &reset_info);
    was = (ret == QM_EOK) ? (int)reset_info.is_reset : -1;
    if (was == (int)QM_IOT_NOT_RESET) {
        QM_LOGI(LOG_TAG, "reset marker already clear");
        return;
    }
    reset_info.is_reset = QM_IOT_NOT_RESET;
    ret = qm_iot_reset_info_set(iot_handle, &reset_info);
    QM_LOGI(LOG_TAG, "reset marker force not-reset was=%d ret=%d", was, ret);
}

static int cloud_start_iot(void)
{
    int ret;

    if (iot_handle == NULL || cloud_started) {
        return QM_EOK;
    }
    /* 已存 WiFi 也每次重新注册，不用 KV 里的旧证书直接连。 */
    ret = cloud_run_dynreg();
    if (ret != QM_EOK) {
        return ret;
    }
    cloud_set_devinfo();
    cloud_migrate_reset_marker();
    ret = qm_iot_start(iot_handle);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "iot start failed ret=%d", ret);
        return ret;
    }
    cloud_started = 1;
    /* Session task only starts when started is already set and network is up. */
    (void)qm_iot_network_update(iot_handle, QM_IOT_NETWORK_UP);
    QM_LOGI(LOG_TAG, "iot start ok");
    return ret;
}

static void cloud_iot_task(void *arg)
{
    (void)arg;
    (void)cloud_start_iot();
    iot_task_running = 0;
    qm_task_exit(NULL);
}

static void cloud_schedule_iot(void)
{
    static qm_task_t task;

    if (cloud_started || iot_task_running) {
        return;
    }
    iot_task_running = 1;
    if (qm_task_new(&task, "cloud_iot", cloud_iot_task, NULL,
                    CLOUD_IOT_TASK_STACK, CLOUD_IOT_TASK_PRIO) != 0) {
        iot_task_running = 0;
        QM_LOGE(LOG_TAG, "iot task create failed");
    }
}

static void cloud_iot_event_handler(void *handle, const qm_iot_event_t *event, void *userdata)
{
    (void)handle;
    (void)userdata;
    if (event == NULL) {
        return;
    }
    switch (event->type) {
        case QM_IOT_EVENT_CLOUD_CONN:
            QM_LOGI(LOG_TAG, "cloud connected");
            /* [MODIFIED] 线性控制：连云瞬间不再自动开启常驻摄像头流，避免与声网进房及语音串口并发冲突 */
            (void)ljyd_method_on_connected(iot_handle);
            time_sync_start();      /* 网络已可用：SNTP 对时 → 成功后通知闹钟重排 */
#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
            (void)pomodoro_on_connected(iot_handle);
#endif
            if (cloud_event_cb != NULL) {
                cloud_event_cb(LJYD_CLOUD_EVENT_CONNECTED, iot_handle, cloud_event_userdata);
            }
            break;
        case QM_IOT_EVENT_CLOUD_DISCONN:
            QM_LOGW(LOG_TAG, "cloud disconnected");
            if (cloud_event_cb != NULL) {
                cloud_event_cb(LJYD_CLOUD_EVENT_DISCONNECTED, iot_handle, cloud_event_userdata);
            }
            break;
        case QM_IOT_EVENT_CONNING_CLOUD:
            QM_LOGI(LOG_TAG, "cloud connecting");
            break;
        case QM_IOT_EVENT_RESET:
            QM_LOGI(LOG_TAG, "cloud reset, clear provision and reboot");
            ljyd_provision_clear_and_reboot();
            break;
        default:
            QM_LOGD(LOG_TAG, "iot event %d", event->type);
            break;
    }
}

static const char *iot_recv_type_name(qm_iot_recv_type_t type)
{
    switch (type) {
    case QM_IOT_RECV_TYPE_PROPERTY_SET:
        return "set";
    case QM_IOT_RECV_TYPE_PROPERTY_GET_RSP:
        return "get_rsp";
    case QM_IOT_RECV_TYPE_PROPERTY_GET_REQ:
        return "get_req";
    default:
        return "unknown";
    }
}

static qm_spec_property_t *iot_recv_property(const qm_iot_recv_t *recv)
{
    switch (recv->type) {
    case QM_IOT_RECV_TYPE_PROPERTY_SET:
        return recv->data.set.property;
    case QM_IOT_RECV_TYPE_PROPERTY_GET_RSP:
        return recv->data.get_rsp.property;
    case QM_IOT_RECV_TYPE_PROPERTY_GET_REQ:
        return recv->data.get_req.property;
    default:
        return NULL;
    }
}

static void format_spec_value(qm_spec_property_t *prop, char *buf, int buf_size)
{
    uint8_t fmt;

    if (buf == NULL || buf_size <= 0) {
        return;
    }
    if (prop == NULL) {
        qm_snprintf(buf, (size_t)buf_size, "none");
        return;
    }

    fmt = prop->value.property_format;
    switch (fmt) {
    case QM_SPEC_PROPERTY_FORMAT_BOOL: {
        bool_t v = 0;
        qm_spec_property_unpack_bool(prop, &v);
        qm_snprintf(buf, (size_t)buf_size, "bool %d", (int)v);
        break;
    }
    case QM_SPEC_PROPERTY_FORMAT_NUMBER:
    case QM_SPEC_PROPERTY_FORMAT_INT32: {
        int32_t v = 0;
        qm_spec_property_unpack_int32(prop, &v);
        qm_snprintf(buf, (size_t)buf_size, "i32 %d", (int)v);
        break;
    }
    case QM_SPEC_PROPERTY_FORMAT_STRING: {
        char *s = NULL;
        int n = 0;
        qm_spec_property_unpack_string_direct(prop, &s, &n);
        qm_snprintf(buf, (size_t)buf_size, "str %.*s", n, s != NULL ? s : "");
        break;
    }
    default:
        qm_snprintf(buf, (size_t)buf_size, "fmt=%u len=%u",
                    (unsigned)fmt, (unsigned)prop->value.len);
        break;
    }
}

static void cloud_iot_recv_handler(void *handle, const qm_iot_recv_t *recv, void *userdata)
{
    qm_spec_property_t *prop;
    char value[96];

    (void)handle;
    (void)userdata;
    if (recv == NULL) {
        return;
    }

    prop = iot_recv_property(recv);
    format_spec_value(prop, value, (int)sizeof(value));
    QM_LOGI(LOG_TAG, "iot recv %s(%d) did=%u siid=%u piid=%u code=%d value=%s",
            iot_recv_type_name(recv->type), (int)recv->type,
            (unsigned)recv->did, (unsigned)recv->siid, (unsigned)recv->piid,
            prop != NULL ? (int)prop->code : 0, value);

    if (recv->type == QM_IOT_RECV_TYPE_PROPERTY_SET && cloud_prop_cb != NULL) {
        cloud_prop_cb(prop);
    }
}

void ljyd_cloud_on_network_up(void)
{
    if (iot_handle != NULL) {
        (void)qm_iot_network_update(iot_handle, QM_IOT_NETWORK_UP);
    }
    cloud_schedule_iot();
}

void ljyd_cloud_on_network_down(void)
{
    if (iot_handle != NULL) {
        (void)qm_iot_network_update(iot_handle, QM_IOT_NETWORK_DOWN);
    }
}

void ljyd_cloud_set_event_cb(ljyd_cloud_event_cb_t cb, void *userdata)
{
    cloud_event_cb = cb;
    cloud_event_userdata = userdata;
}

void ljyd_cloud_set_prop_cb(ljyd_cloud_prop_cb_t cb)
{
    cloud_prop_cb = cb;
}

int ljyd_cloud_request_reset(void)
{
    if (iot_handle == NULL) {
        return -QM_EIO;
    }
    return qm_iot_reset(iot_handle);
}

void *ljyd_cloud_get_handle(void)
{
    return iot_handle;
}

int ljyd_cloud_init(void)
{
    uint32_t pid = CONFIG_XJIANG_CLOUD_PID;

    iot_handle = qm_iot_init();
    if (iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return -QM_EIO;
    }
    qm_iot_setopt(iot_handle, QM_IOT_OPT_PRODUCT_ID, &pid);
    qm_iot_setopt(iot_handle, QM_IOT_OPT_VERSION_STR, CONFIG_XJIANG_VERSION_STR);
    qm_iot_setopt(iot_handle, QM_IOT_OPT_PRODUCT_SECRET, CONFIG_XJIANG_CLOUD_SECERT);
    qm_iot_setopt(iot_handle, QM_IOT_OPT_RECV_HANDLER, cloud_iot_recv_handler);
    qm_iot_setopt(iot_handle, QM_IOT_OPT_EVENT_HANDLER, cloud_iot_event_handler);

    refresh_device_sn();
    if (device_sn[0] != '\0') {
        (void)qm_iot_setopt(iot_handle, QM_IOT_OPT_SN, device_sn);
    }
    return QM_EOK;
}
