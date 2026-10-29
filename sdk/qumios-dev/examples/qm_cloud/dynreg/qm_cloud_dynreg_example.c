#include "qm_config.h"

#include "qm.h"
#include "qm_event.h"
#include "qm_iot_api.h"
#include "qm_iot_dynreg.h"
#include "qm_log.h"
#include "qm_provision.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_cloud_dynreg_example"

#if !CONFIG_QM_IOT_DYNREG_SIGNATURE_SUPPORT
#error "qm_cloud dynreg example requires CONFIG_QM_IOT_DYNREG_SIGNATURE_SUPPORT"
#endif

#if !CONFIG_QM_PROVISION_SUPPORT
#error "qm_cloud dynreg example requires CONFIG_QM_PROVISION_SUPPORT"
#endif

#if !CONFIG_QM_WIFI_SUPPORT
#error "qm_cloud dynreg example requires CONFIG_QM_WIFI_SUPPORT"
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID 0U
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET ""
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN "qm-cloud-dynreg-device"
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST ""
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_VERSION
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_VERSION "1.0.0"
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID ""
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ENV
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ENV QM_IOT_DYNREG_ENV_PROD
#endif

#ifndef CONFIG_QM_CLOUD_DYNREG_EXAMPLE_WIFI_RETRY_MAX
#define CONFIG_QM_CLOUD_DYNREG_EXAMPLE_WIFI_RETRY_MAX 10
#endif

static void *s_iot_handle;
static qm_provision_handle_t s_provision_handle;
static qm_iot_cloud_auth_t s_cloud_credential;
static char s_server_cert[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_key[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_ota_public_key[CONFIG_QM_IOT_OTA_CRT_MAX_LEN + 1];
static char s_dynreg_host[QM_PROVISION_DYNREG_HOST_SIZE];
static qm_bool s_iot_started;
static qm_bool s_wifi_event_registered;
static uint32_t s_wifi_retry_count;

static void wifi_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                               int32_t event_id, void *event_data, size_t event_data_size);

static int check_ret(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

static void iot_event_handler(void *handle, const qm_iot_event_t *event, void *userdata)
{
    (void)handle;
    (void)userdata;

    if (event == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG, "iot event=%d", event->type);
}

static int fill_wifi_devinfo(qm_iot_devinfo_t *info)
{
    uint8_t mac[6] = {0};
    qm_wifi_ap_record_t ap_info = {0};

    if (info == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(info, 0, sizeof(*info));
    info->type = QM_IOT_NETWORK_TYPE_WIFI;

    qm_wifi_get_mac(QM_WIFI_IF_STA, mac);
    qm_snprintf(info->data.wifi.mac,
                sizeof(info->data.wifi.mac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);

    if (qm_wifi_sta_get_ap_info(&ap_info) == QM_EOK) {
        qm_snprintf(info->data.wifi.bssid,
                    sizeof(info->data.wifi.bssid),
                    "%02X:%02X:%02X:%02X:%02X:%02X",
                    ap_info.bssid[0],
                    ap_info.bssid[1],
                    ap_info.bssid[2],
                    ap_info.bssid[3],
                    ap_info.bssid[4],
                    ap_info.bssid[5]);
        qm_snprintf(info->data.wifi.ssid,
                    sizeof(info->data.wifi.ssid),
                    "%s",
                    (char *)ap_info.ssid);
        info->data.wifi.rssi = ap_info.rssi;
        info->data.wifi.channel = ap_info.channel;
    }

    return QM_EOK;
}

static void dynreg_recv_handler(void *handle,
                                const qm_iot_dynreg_recv_t *packet,
                                void *userdata)
{
    uint32_t len;

    (void)handle;
    (void)userdata;

    qm_memset(&s_cloud_credential, 0, sizeof(s_cloud_credential));
    qm_memset(s_server_cert, 0, sizeof(s_server_cert));
    qm_memset(s_client_crt, 0, sizeof(s_client_crt));
    qm_memset(s_client_key, 0, sizeof(s_client_key));
    qm_memset(s_ota_public_key, 0, sizeof(s_ota_public_key));

    if (packet == NULL) {
        return;
    }

    if (packet->type == QM_IOT_DYNREGRECV_STATUS_CODE) {
        QM_LOGE(LOG_TAG, "dynreg status code=%u",
                (unsigned int)packet->data.status_code.code);
        return;
    }

    s_cloud_credential.did = packet->data.device_info.did;
    qm_snprintf(s_cloud_credential.cloud_info.host,
                sizeof(s_cloud_credential.cloud_info.host),
                "%.*s",
                (int)packet->data.device_info.mqtt_host_len,
                packet->data.device_info.mqtt_host);
    s_cloud_credential.cloud_info.port = packet->data.device_info.mqtt_port;
    if (s_cloud_credential.cloud_info.port == 0) {
        s_cloud_credential.cloud_info.port = 8883;
    }

    len = packet->data.device_info.server_cert_len;
    if (len >= sizeof(s_server_cert)) {
        return;
    }
    qm_memcpy(s_server_cert, packet->data.device_info.server_cert, len);

    len = packet->data.device_info.client_cert_len;
    if (len >= sizeof(s_client_crt)) {
        return;
    }
    qm_memcpy(s_client_crt, packet->data.device_info.client_crt, len);

    len = packet->data.device_info.client_privkey_len;
    if (len >= sizeof(s_client_key)) {
        return;
    }
    qm_memcpy(s_client_key, packet->data.device_info.client_private_key, len);

    len = packet->data.device_info.public_key_len;
    if (len >= sizeof(s_ota_public_key)) {
        return;
    }
    qm_memcpy(s_ota_public_key, packet->data.device_info.public_key, len);

    s_cloud_credential.server_cert = s_server_cert;
    s_cloud_credential.server_cert_len = packet->data.device_info.server_cert_len;
    s_cloud_credential.client_crt = s_client_crt;
    s_cloud_credential.client_crt_len = packet->data.device_info.client_cert_len;
    s_cloud_credential.client_key = s_client_key;
    s_cloud_credential.client_key_len = packet->data.device_info.client_privkey_len;
    s_cloud_credential.ota_public_key = s_ota_public_key;
    s_cloud_credential.ota_public_key_len = packet->data.device_info.public_key_len;
}

static int dynreg_request(void)
{
    void *dynreg_handle;
    uint32_t product_id = CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID;
    uint16_t port = CONFIG_QM_IOT_DYNREG_PORT;
    uint32_t retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;
    uint32_t timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    int ret = QM_EOK;

    dynreg_handle = qm_iot_dynreg_init();
    if (dynreg_handle == NULL) {
        return -QM_ENOMEM;
    }

    if (s_dynreg_host[0] != '\0') {
        ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_HOST,
                                    s_dynreg_host);
    } else if (CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST[0] != '\0') {
        ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_HOST,
                                    (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST);
    }
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PORT, &port);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PRODUCT_ID, &product_id);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_PRODUCT_SECRET,
                                (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_SN,
                                (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_MAX_RETRY_NUM, &retry_num);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_RECV_TIMEOUT_MS, &timeout);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_RECV_HANDLER,
                                dynreg_recv_handler);
    if (ret == QM_EOK) {
        ret = qm_iot_dynreg_start(dynreg_handle, QM_IOT_NETWORK_TYPE_WIFI);
    }

    (void)qm_iot_dynreg_deinit(&dynreg_handle);
    if (ret != QM_EOK) {
        return ret;
    }

    return (s_cloud_credential.did != 0) ? QM_EOK : -QM_EINVAL;
}

static int iot_start_after_dynreg(void)
{
    uint32_t product_id = CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID;
    char *version = (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_VERSION;
    qm_iot_devinfo_t devinfo = {0};

    if (s_iot_started) {
        return QM_EOK;
    }

    if (check_ret("dynreg request", dynreg_request()) != QM_EOK) {
        return -QM_EIO;
    }

    if (check_ret("set product id",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_PRODUCT_ID, &product_id)) != QM_EOK ||
        check_ret("set version",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_VERSION_STR, version)) != QM_EOK ||
        check_ret("set product secret",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_PRODUCT_SECRET,
                                (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET)) != QM_EOK ||
        check_ret("set sn",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_SN,
                                (char *)CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN)) != QM_EOK ||
        check_ret("set cloud credential",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_CLOUD_CREDENTIAL,
                                &s_cloud_credential)) != QM_EOK ||
        check_ret("set event handler",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_EVENT_HANDLER,
                                iot_event_handler)) != QM_EOK ||
        check_ret("set devinfo",
                  fill_wifi_devinfo(&devinfo) == QM_EOK ?
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_DEVINFO, &devinfo) : -QM_EIO) != QM_EOK ||
        check_ret("iot start", qm_iot_start(s_iot_handle)) != QM_EOK) {
        return -QM_EIO;
    }

    if (check_ret("network up",
                  qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_UP)) != QM_EOK) {
        return -QM_EIO;
    }

    s_iot_started = QM_TRUE;
    return QM_EOK;
}

static int wifi_start_with_credential(const char *ssid, const char *password)
{
    qm_wifi_config_t config = {0};
    size_t ssid_len;
    size_t password_len;
    int ret;

    if (ssid == NULL || ssid[0] == '\0') {
        return -QM_EINVAL;
    }

    ssid_len = qm_strlen(ssid);
    password_len = password ? qm_strlen(password) : 0;
    if (ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN ||
        password_len > CONFIG_QM_WIFI_PASSWD_MAX_LEN) {
        return -QM_EINVAL;
    }

    config.sta.ssid_len = (uint8_t)ssid_len;
    qm_memcpy(config.sta.ssid, ssid, ssid_len);
    if (password_len > 0) {
        config.sta.password_len = (uint8_t)password_len;
        qm_memcpy(config.sta.password, password, password_len);
    }

    if (!s_wifi_event_registered) {
        ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, wifi_event_handler, NULL);
        if (ret != QM_EOK) {
            return ret;
        }
        s_wifi_event_registered = QM_TRUE;
    }

    ret = qm_wifi_init();
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_wifi_start();
}

static void provision_recv_handler(qm_provision_handle_t handle,
                                   const qm_provision_recv_t *recv,
                                   void *userdata)
{
    (void)handle;
    (void)userdata;

    if (recv == NULL || recv->ssid[0] == '\0') {
        return;
    }

    QM_LOGI(LOG_TAG,
            "provision recv ssid=%s password_len=%u dynreg_host=%s",
            recv->ssid,
            (unsigned int)qm_strlen(recv->password),
            recv->dynreg_host);

    if (recv->dynreg_host[0] != '\0') {
        qm_snprintf(s_dynreg_host, sizeof(s_dynreg_host), "%s", recv->dynreg_host);
    }

    if (s_provision_handle != NULL) {
        (void)qm_provision_stop(s_provision_handle);
    }

    s_wifi_retry_count = 0;
    if (check_ret("wifi start with provision",
                  wifi_start_with_credential(recv->ssid, recv->password)) != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi start failed");
    }
}

static int provision_start(void)
{
    const char *adv_cid = NULL;
    qm_provision_config_t config = {
        .product_id = CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID,
        .product_secret = CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET,
        .adv_cid = NULL,
        .recv_handler = provision_recv_handler,
        .userdata = NULL,
    };

    if (s_provision_handle != NULL) {
        return qm_provision_start(s_provision_handle);
    }

    if (CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID[0] != '\0') {
        adv_cid = CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID;
    }
    config.adv_cid = adv_cid;

    s_provision_handle = qm_provision_create(&config);
    if (s_provision_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_provision_start(s_provision_handle);
}

static void wifi_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                               int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    event_info = (qm_wifi_event_info_t *)event_data;
    switch ((qm_wifi_event_t)event_id) {
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
            if (event_info != NULL) {
                QM_LOGI(LOG_TAG, "ip:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
            }
            if (s_iot_started) {
                (void)check_ret("network up",
                                qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_UP));
            } else {
                (void)iot_start_after_dynreg();
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG, "wifi disconnected");
            if (s_iot_handle != NULL) {
                (void)qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_DOWN);
            }
            if (s_wifi_retry_count < CONFIG_QM_CLOUD_DYNREG_EXAMPLE_WIFI_RETRY_MAX) {
                s_wifi_retry_count++;
                (void)qm_wifi_connect();
            }
            break;

        default:
            break;
    }
}

void qm_application_start(void)
{
    if (CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID == 0U ||
        CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET[0] == '\0' ||
        CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN[0] == '\0') {
        QM_LOGE(LOG_TAG, "set product_id/product_secret/sn before starting");
        return;
    }

    s_iot_handle = qm_iot_init();
    if (s_iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return;
    }

    qm_iot_dynreg_env_set(CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ENV);
    (void)check_ret("start ble provision", provision_start());
}
