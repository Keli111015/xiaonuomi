#include "qm_config.h"

#include "qm.h"
#include "qm_iot_api.h"
#include "qm_iot_dynreg.h"
#include "qm_log.h"
#include "qm_string.h"

#define LOG_TAG "qm_cloud_authcode_example"

#if !CONFIG_QM_IOT_DYNREG_AUTHCODE_SUPPORT
#error "qm_cloud authCode example requires CONFIG_QM_IOT_DYNREG_AUTHCODE_SUPPORT"
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID 0U
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE ""
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN "qm-cloud-authcode-device"
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST ""
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_VERSION
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_VERSION "1.0.0"
#endif

#ifndef CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_ENV
#define CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_ENV QM_IOT_DYNREG_ENV_PROD
#endif

static void *s_iot_handle;
static qm_iot_cloud_auth_t s_cloud_credential;
static char s_server_cert[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_key[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_ota_public_key[CONFIG_QM_IOT_OTA_CRT_MAX_LEN + 1];

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
        QM_LOGE(LOG_TAG, "authcode status code=%u",
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

static int authcode_request(void)
{
    void *dynreg_handle;
    uint32_t product_id = CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID;
    uint16_t port = CONFIG_QM_IOT_DYNREG_PORT;
    uint32_t retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;
    uint32_t timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    int ret = QM_EOK;

    dynreg_handle = qm_iot_dynreg_init();
    if (dynreg_handle == NULL) {
        return -QM_ENOMEM;
    }

    if (CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST[0] != '\0') {
        ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_HOST,
                                    (char *)CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST);
    }
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PORT, &port);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PRODUCT_ID, &product_id);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_AUTHCODE,
                                (char *)CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_SN,
                                (char *)CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN);
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

void qm_application_start(void)
{
    uint32_t product_id = CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID;
    char *version = (char *)CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_VERSION;

    if (CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID == 0U ||
        CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE[0] == '\0' ||
        CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN[0] == '\0') {
        QM_LOGE(LOG_TAG, "set product_id/authcode/sn before starting");
        return;
    }

    qm_iot_dynreg_env_set(CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_ENV);
    if (check_ret("authcode request", authcode_request()) != QM_EOK) {
        return;
    }

    s_iot_handle = qm_iot_init();
    if (s_iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return;
    }

    if (check_ret("set product id",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_PRODUCT_ID, &product_id)) != QM_EOK ||
        check_ret("set version",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_VERSION_STR, version)) != QM_EOK ||
        check_ret("set sn",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_SN,
                                (char *)CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN)) != QM_EOK ||
        check_ret("set cloud credential",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_CLOUD_CREDENTIAL,
                                &s_cloud_credential)) != QM_EOK ||
        check_ret("set event handler",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_EVENT_HANDLER,
                                iot_event_handler)) != QM_EOK ||
        check_ret("iot start", qm_iot_start(s_iot_handle)) != QM_EOK) {
        return;
    }

    (void)check_ret("network up",
                    qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_UP));
}
