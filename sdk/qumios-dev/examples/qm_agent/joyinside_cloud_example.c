#include "qm_agent.h"
#include "qm_agent_example_audio.h"

#include "qm_event.h"
#include "qm_config.h"
#include "qm_ble_gap.h"
#include "qm_cli.h"
#include "qm_errno.h"
#include "qm_iot_api.h"
#include "qm_iot_dynreg.h"
#include "qm_log.h"
#include "qm_provision.h"
#include "qm_string.h"
#include "qm_wifi.h"
#include "qm_work.h"

#ifdef JOYINSIDE_CLOUD_EXAMPLE_CONFIG_HEADER
#include JOYINSIDE_CLOUD_EXAMPLE_CONFIG_HEADER
#endif

#define LOG_TAG "joyinside_cloud_example"

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_WIFI_RETRY_MAX_COUNT
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_WIFI_RETRY_MAX_COUNT 10
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID 11257U
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET "0C2BDAE3407F47D0BEF11076DA8FAA77"
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_ADV_CID
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_ADV_CID ""
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_HOST
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_HOST ""
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_PORT
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_PORT 17605
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_ENV
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_ENV QM_IOT_DYNREG_ENV_PROD
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_VERSION
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_VERSION "1.0.0"
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_CHAT_ROLE_CODE
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_CHAT_ROLE_CODE ""
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_TIMEOUT_MS 5000U
#endif

#ifndef CONFIG_JOYINSIDE_CLOUD_EXAMPLE_AUDIO_DRIVER
#define CONFIG_JOYINSIDE_CLOUD_EXAMPLE_AUDIO_DRIVER (&s_joyinside_cloud_example_audio_driver)
#endif

#define JOYINSIDE_CLOUD_EXAMPLE_EMPTY_TEXT ""

static qm_agent_handle_t s_qm_agent;
static qm_agent_example_audio_t s_audio;
static void *s_iot_handle;
static qm_iot_cloud_auth_t s_cloud_credential;
static char s_server_cert[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_key[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_ota_public_key[CONFIG_QM_IOT_OTA_CRT_MAX_LEN + 1];
static char s_device_sn[13];
static char s_dynreg_host[QM_PROVISION_DYNREG_HOST_SIZE];
static qm_provision_handle_t s_provision_handle;
static qm_bool s_iot_started;
static qm_bool s_wifi_started;
static qm_bool s_wifi_reconfiguring;
static qm_bool s_provision_running;
static int s_wifi_retry_count;
static qm_bool s_wifi_event_registered;
static qm_bool s_cli_registered;
static qm_work_t s_cli_register_work;
static qm_wifi_ap_record_t s_wifi_ap_info;
static qm_bool s_wifi_ap_info_valid;
static char s_wifi_ip[sizeof(((qm_iot_wifi_devinfo_t *)0)->ip)];
static qm_bool s_qm_agent_event_registered;
static int joyinside_cloud_example_ensure_audio(void)
{
    return qm_agent_example_audio_init(&s_audio);
}

static const char *safe_text(const char *value)
{
    return value ? value : JOYINSIDE_CLOUD_EXAMPLE_EMPTY_TEXT;
}

static int check_ret(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

static const char *agent_life_state_name(qm_agent_state_t state)
{
    switch (state) {
        case QM_AGENT_STATE_IDLE:
            return "idle";
        case QM_AGENT_STATE_READY:
            return "ready";
        case QM_AGENT_STATE_STARTING:
            return "starting";
        case QM_AGENT_STATE_RUNNING:
            return "running";
        case QM_AGENT_STATE_STOPPING:
            return "stopping";
        case QM_AGENT_STATE_ERROR:
            return "error";
        case QM_AGENT_STATE_NONE:
        default:
            return "none";
    }
}

static const char *agent_dialog_state_name(qm_agent_dialog_state_t state)
{
    switch (state) {
        case QM_AGENT_DIALOG_STATE_SILENT:
            return "silent";
        case QM_AGENT_DIALOG_STATE_LISTENING:
            return "listening";
        case QM_AGENT_DIALOG_STATE_THINKING:
            return "thinking";
        case QM_AGENT_DIALOG_STATE_SPEAKING:
            return "speaking";
        case QM_AGENT_DIALOG_STATE_NONE:
        default:
            return "none";
    }
}

static qm_bool cloud_config_ready(void)
{
    return (CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID != 0U &&
            CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET[0] != '\0' &&
            s_device_sn[0] != '\0');
}

static void refresh_sn(void)
{
    qm_bd_addr_t mac = {0};
    int ret;

    ret = qm_ble_gap_get_local_addr(mac);
    if (ret != QM_EOK) {
        s_device_sn[0] = '\0';
        QM_LOGE(LOG_TAG, "get ble mac failed ret=%d", ret);
        return;
    }

    qm_snprintf(s_device_sn,
                sizeof(s_device_sn),
                "%02X%02X%02X%02X%02X%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);
}

static void qm_agent_event_handler(void *event_handler_arg,
                                   qm_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data,
                                   size_t event_data_size)
{
    const qm_agent_event_notice_t *notice =
        (const qm_agent_event_notice_t *)event_data;

    QM_UNUSED_ARG(event_handler_arg);

    if (event_base != QM_AGENT_EVENT_BASE || notice == NULL ||
        event_data_size != sizeof(*notice)) {
        return;
    }

    switch ((qm_agent_event_id_t)event_id) {
        case QM_AGENT_EVENT_STATE_CHANGED:
            QM_LOGI(LOG_TAG,
                    "agent state %s -> %s",
                    agent_life_state_name(notice->data.state.old_state),
                    agent_life_state_name(notice->data.state.new_state));
            break;

        case QM_AGENT_EVENT_CONNECTED:
            QM_LOGI(LOG_TAG, "agent connected");
            break;

        case QM_AGENT_EVENT_DISCONNECTED:
            QM_LOGI(LOG_TAG, "agent disconnected");
            break;

        case QM_AGENT_EVENT_ASR:
            QM_LOGI(LOG_TAG,
                    "agent asr final=%d text=%s",
                    notice->data.text.final,
                    safe_text(notice->data.text.text));
            break;

        case QM_AGENT_EVENT_TTS:
            QM_LOGI(LOG_TAG,
                    "agent tts final=%d text=%s",
                    notice->data.text.final,
                    safe_text(notice->data.text.text));
            break;

        case QM_AGENT_EVENT_AGENT_TEXT:
            QM_LOGI(LOG_TAG,
                    "agent text final=%d text=%s",
                    notice->data.text.final,
                    safe_text(notice->data.text.text));
            break;

        case QM_AGENT_EVENT_AGENT_STATE:
            QM_LOGI(LOG_TAG,
                    "agent dialog=%s text=%s",
                    agent_dialog_state_name(notice->data.agent_state.state),
                    safe_text(notice->data.agent_state.detail.text));
            break;

        case QM_AGENT_EVENT_ERROR:
            QM_LOGE(LOG_TAG,
                    "agent error code=%d message=%s",
                    notice->code,
                    safe_text(notice->data.text.text));
            break;

        default:
            QM_LOGI(LOG_TAG, "agent event id=%d", (int)event_id);
            break;
    }
}

static int register_qm_agent_events(void)
{
    int ret;

    if (s_qm_agent_event_registered) {
        return QM_EOK;
    }

    ret = qm_event_default_create();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }

    ret = qm_event_handler_register(QM_AGENT_EVENT_BASE,
                                    QM_EVENT_ANY_ID,
                                    qm_agent_event_handler,
                                    NULL);
    if (ret == QM_EOK) {
        s_qm_agent_event_registered = QM_TRUE;
    }
    return ret;
}

static void unregister_qm_agent_events(void)
{
    if (!s_qm_agent_event_registered) {
        return;
    }

    (void)qm_event_handler_unregister(QM_AGENT_EVENT_BASE,
                                      QM_EVENT_ANY_ID,
                                      qm_agent_event_handler,
                                      NULL);
    s_qm_agent_event_registered = QM_FALSE;
}

static void stop_joyinside_client(void)
{
    unregister_qm_agent_events();
    if (s_qm_agent != NULL) {
        (void)qm_agent_stop(s_qm_agent);
        qm_agent_destroy(s_qm_agent);
        s_qm_agent = NULL;
    }
    (void)qm_agent_example_audio_deinit(&s_audio);
}

static int start_qm_agent_with_cloud_auth(void)
{
    qm_agent_config_t config;
    int ret;

    if (s_qm_agent != NULL) {
        return QM_EOK;
    }

    if (s_iot_handle == NULL) {
        return -QM_EINVAL;
    }

    ret = joyinside_cloud_example_ensure_audio();
    if (ret != QM_EOK) {
        return ret;
    }

    ret = register_qm_agent_events();
    if (ret != QM_EOK) {
        return ret;
    }

    qm_memset(&config, 0, sizeof(config));
    config.transport.type = QM_AGENT_TRANSPORT_JOYINSIDE;
    config.audio.control = s_audio.control;
    config.audio.capture_enabled = QM_TRUE;
    qm_agent_config_init_defaults(&config);

    config.transport_if = NULL;
    config.iot_handle = s_iot_handle;
    config.transport.data.joyinside.chat_role_code =
        CONFIG_JOYINSIDE_CLOUD_EXAMPLE_CHAT_ROLE_CODE;
    config.transport.data.joyinside.timeout_ms = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_TIMEOUT_MS;

    s_qm_agent = qm_agent_create(&config);
    if (s_qm_agent == NULL) {
        QM_LOGE(LOG_TAG, "qm_agent create failed");
        unregister_qm_agent_events();
        return -QM_ENOMEM;
    }

    ret = qm_agent_start(s_qm_agent);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "qm_agent start failed ret=%d", ret);
        stop_joyinside_client();
        return ret;
    }

    QM_LOGI(LOG_TAG, "qm_agent started, waiting internal token");
    return QM_EOK;
}

static void stop_provision(void)
{
    int ret;

    if (s_provision_handle == NULL || !s_provision_running) {
        return;
    }

    ret = qm_provision_stop(s_provision_handle);
    if (ret == QM_EOK) {
        s_provision_running = QM_FALSE;
    } else {
        QM_LOGE(LOG_TAG, "provision stop failed ret=%d", ret);
    }
}

static int clear_provision_state(void)
{
    int ret;

    stop_joyinside_client();
    stop_provision();

    if (s_iot_handle != NULL) {
        (void)qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_DOWN);
    }

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret == QM_EOK) {
        s_wifi_started = QM_FALSE;
    }

    ret = qm_wifi_clear_config();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_retry_count = 0;
    s_wifi_ap_info_valid = QM_FALSE;
    s_wifi_ip[0] = '\0';
    qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
    s_wifi_reconfiguring = QM_FALSE;

    return QM_EOK;
}

static void joyinside_cloud_reset_cmd(char *pcWriteBuffer,
                                      int xWriteBufferLen,
                                      int argc,
                                      char **argv)
{
    int ret;

    (void)argc;
    (void)argv;

    ret = clear_provision_state();
    if (ret == QM_EOK) {
        qm_snprintf(pcWriteBuffer,
                    xWriteBufferLen,
                    "joyinside cloud reset ok, rebooting\r\n");
        qm_reboot();
        return;
    }

    qm_snprintf(pcWriteBuffer,
                xWriteBufferLen,
                "joyinside cloud reset failed ret=%d\r\n",
                ret);
}

static const struct qm_cli_command s_cli_cmd = {
    .name = "provision_reset",
    .help = "clear saved wifi config and reboot into BLE provisioning",
    .function = joyinside_cloud_reset_cmd,
};

static void ensure_cli_registered(void *arg)
{
    qm_err_t ret;

    (void)arg;

    if (s_cli_registered) {
        return;
    }

    ret = qm_cli_register_command(&s_cli_cmd);
    if (ret == QM_EOK) {
        s_cli_registered = QM_TRUE;
        return;
    }

    QM_LOGW(LOG_TAG, "cli not ready ret=%d", (int)ret);
    (void)qm_post_delayed_action(&s_cli_register_work,
                                 ensure_cli_registered,
                                 NULL,
                                 1000);
}

static int apply_wifi_config(const char *ssid, const char *password)
{
    qm_wifi_config_t config = {0};
    uint32_t ssid_len;
    uint32_t password_len;

    if (ssid == NULL || ssid[0] == '\0') {
        return -QM_EINVAL;
    }

    ssid_len = qm_strlen(ssid);
    password_len = password ? qm_strlen(password) : 0;
    if (ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN ||
        password_len > CONFIG_QM_WIFI_PASSWD_MAX_LEN) {
        QM_LOGE(LOG_TAG,
                "wifi credential too long ssid_len=%u password_len=%u",
                (unsigned int)ssid_len,
                (unsigned int)password_len);
        return -QM_EINVAL;
    }

    qm_memcpy(config.sta.ssid, ssid, ssid_len);
    config.sta.ssid_len = (uint8_t)ssid_len;
    if (password_len > 0) {
        qm_memcpy(config.sta.password, password, password_len);
        config.sta.password_len = (uint8_t)password_len;
    }

    return qm_wifi_set_config(QM_WIFI_IF_STA, &config);
}

static int start_wifi_with_config(const char *ssid, const char *password)
{
    int ret;

    ret = apply_wifi_config(ssid, password);
    if (ret != QM_EOK) {
        return ret;
    }

    if (!s_wifi_started) {
        ret = qm_wifi_start();
        if (ret == QM_EOK) {
            s_wifi_started = QM_TRUE;
        }
        return ret;
    }

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        s_wifi_started = QM_FALSE;
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_started = QM_TRUE;
    return QM_EOK;
}

static qm_bool has_saved_wifi_config(void)
{
    qm_wifi_config_t config = {0};

    if (qm_wifi_get_config(QM_WIFI_IF_STA, &config) != QM_EOK) {
        return QM_FALSE;
    }

    return (config.sta.ssid_len != 0 && config.sta.ssid[0] != '\0');
}

static int start_saved_wifi(void)
{
    int ret;

    if (!has_saved_wifi_config()) {
        return -QM_ENOENT;
    }

    if (!s_wifi_started) {
        ret = qm_wifi_start();
        if (ret == QM_EOK) {
            s_wifi_started = QM_TRUE;
        }
        return ret;
    }

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        s_wifi_started = QM_FALSE;
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_started = QM_TRUE;
    return QM_EOK;
}

static void provision_recv(qm_provision_handle_t handle,
                           const qm_provision_recv_t *recv,
                           void *userdata)
{
    int ret;

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
        qm_snprintf(s_dynreg_host,
                    sizeof(s_dynreg_host),
                    "%s",
                    recv->dynreg_host);
    }

    s_wifi_retry_count = 0;
    ret = start_wifi_with_config(recv->ssid, recv->password);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "start wifi with provision config failed ret=%d", ret);
    }
}

static int start_provision(void)
{
    int ret;
    qm_provision_config_t config = {
        .product_id = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID,
        .product_secret = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET,
        .adv_cid = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_ADV_CID[0] != '\0' ?
                   CONFIG_JOYINSIDE_CLOUD_EXAMPLE_ADV_CID : NULL,
        .recv_handler = provision_recv,
        .userdata = NULL,
    };

    if (CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID == 0U ||
        CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET[0] == '\0') {
        QM_LOGE(LOG_TAG, "set product_id/product_secret before provision");
        return -QM_EINVAL;
    }

    if (s_provision_handle == NULL) {
        s_provision_handle = qm_provision_create(&config);
        if (s_provision_handle == NULL) {
            return -QM_EINVAL;
        }
    }

    ret = qm_provision_start(s_provision_handle);
    if (ret == QM_EOK) {
        s_provision_running = QM_TRUE;
    }

    return ret;
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
        QM_LOGE(LOG_TAG,
                "dynreg status code=%u",
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

static int request_cloud_credential(void)
{
    void *dynreg_handle;
    uint32_t product_id = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID;
    uint16_t port = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_PORT;
    uint32_t retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;
    uint32_t timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    int ret;

    qm_memset(&s_cloud_credential, 0, sizeof(s_cloud_credential));

    dynreg_handle = qm_iot_dynreg_init();
    if (dynreg_handle == NULL) {
        return -QM_ENOMEM;
    }

    qm_iot_dynreg_env_set(CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_ENV);

    ret = QM_EOK;
    if (s_dynreg_host[0] != '\0') {
        ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_HOST,
                                    s_dynreg_host);
    } else if (CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_HOST[0] != '\0') {
        ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_HOST,
                                    (char *)CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_HOST);
    }
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PORT, &port);
    ret |= qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PRODUCT_ID, &product_id);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_PRODUCT_SECRET,
                                (char *)CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET);
    ret |= qm_iot_dynreg_setopt(dynreg_handle,
                                QM_IOT_DYNREGOPT_SN,
                                s_device_sn);
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

static int fill_devinfo(qm_iot_devinfo_t *info)
{
    uint8_t mac[6] = {0};

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

    if (s_wifi_ap_info_valid) {
        qm_snprintf(info->data.wifi.bssid,
                    sizeof(info->data.wifi.bssid),
                    "%02X:%02X:%02X:%02X:%02X:%02X",
                    s_wifi_ap_info.bssid[0],
                    s_wifi_ap_info.bssid[1],
                    s_wifi_ap_info.bssid[2],
                    s_wifi_ap_info.bssid[3],
                    s_wifi_ap_info.bssid[4],
                    s_wifi_ap_info.bssid[5]);
        qm_snprintf(info->data.wifi.ssid,
                    sizeof(info->data.wifi.ssid),
                    "%s",
                    (char *)s_wifi_ap_info.ssid);
        info->data.wifi.rssi = s_wifi_ap_info.rssi;
        info->data.wifi.channel = s_wifi_ap_info.channel;
    }

    if (s_wifi_ip[0] != '\0') {
        qm_snprintf(info->data.wifi.ip,
                    sizeof(info->data.wifi.ip),
                    "%s",
                    s_wifi_ip);
    }

    return QM_EOK;
}

static int update_devinfo(void)
{
    qm_iot_devinfo_t devinfo;
    int ret;

    if (s_iot_handle == NULL) {
        return -QM_EINVAL;
    }

    ret = fill_devinfo(&devinfo);
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_iot_setopt(s_iot_handle, QM_IOT_OPT_DEVINFO, &devinfo);
}

static void iot_event_handler(void *handle, const qm_iot_event_t *event, void *userdata)
{
    (void)handle;
    (void)userdata;

    if (event == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG, "iot event=%d", event->type);

    switch (event->type) {
        case QM_IOT_EVENT_CLOUD_CONN:
            (void)check_ret("qm_agent start", start_qm_agent_with_cloud_auth());
            break;

        case QM_IOT_EVENT_CLOUD_DISCONN:
            stop_joyinside_client();
            break;

        case QM_IOT_EVENT_RESET:
            QM_LOGW(LOG_TAG, "cloud reset event");
            if (clear_provision_state() == QM_EOK) {
                qm_reboot();
            }
            break;

        default:
            break;
    }
}

static int start_iot(void)
{
    uint32_t product_id = CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_ID;
    char *version = (char *)CONFIG_JOYINSIDE_CLOUD_EXAMPLE_VERSION;

    if (s_iot_started) {
        return QM_EOK;
    }

    refresh_sn();
    if (!cloud_config_ready()) {
        QM_LOGE(LOG_TAG, "set product_id/product_secret and ensure device sn is available");
        return -QM_EINVAL;
    }

    if (check_ret("dynreg", request_cloud_credential()) != QM_EOK) {
        return -QM_EINVAL;
    }

    s_iot_handle = qm_iot_init();
    if (s_iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return -QM_ENOMEM;
    }

    if (check_ret("set version",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_VERSION_STR, version)) != QM_EOK ||
        check_ret("set product id",
                  qm_iot_setopt(s_iot_handle, QM_IOT_OPT_PRODUCT_ID, &product_id)) != QM_EOK ||
        check_ret("set product secret",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_PRODUCT_SECRET,
                                (char *)CONFIG_JOYINSIDE_CLOUD_EXAMPLE_PRODUCT_SECRET)) != QM_EOK ||
        check_ret("set sn",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_SN,
                                s_device_sn)) != QM_EOK ||
        check_ret("set cloud credential",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_CLOUD_CREDENTIAL,
                                &s_cloud_credential)) != QM_EOK ||
        check_ret("set event handler",
                  qm_iot_setopt(s_iot_handle,
                                QM_IOT_OPT_EVENT_HANDLER,
                                iot_event_handler)) != QM_EOK ||
        check_ret("iot start", qm_iot_start(s_iot_handle)) != QM_EOK) {
        return -QM_EINVAL;
    }

    s_iot_started = QM_TRUE;
    return QM_EOK;
}

static void wifi_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                               int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t *)event_data;
    int ret;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch ((qm_wifi_event_t)event_id) {
        case QM_WIFI_EVENT_STA_START:
            QM_LOGI(LOG_TAG, "wifi sta start");
            s_wifi_started = QM_TRUE;
            s_wifi_reconfiguring = QM_FALSE;
            if (qm_wifi_connect() != QM_EOK) {
                QM_LOGE(LOG_TAG, "wifi connect failed");
            }
            break;

        case QM_WIFI_EVENT_STA_STOP:
            QM_LOGI(LOG_TAG, "wifi sta stop");
            if (!s_wifi_reconfiguring) {
                s_wifi_started = QM_FALSE;
            }
            break;

        case QM_WIFI_EVENT_STA_CONNECTED:
            QM_LOGI(LOG_TAG, "wifi sta connected");
            s_wifi_retry_count = 0;
            if (qm_wifi_sta_get_ap_info(&s_wifi_ap_info) == QM_EOK) {
                s_wifi_ap_info_valid = QM_TRUE;
            } else {
                s_wifi_ap_info_valid = QM_FALSE;
                qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
            }
            break;

        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGI(LOG_TAG, "wifi got ip");
            if (event_info != NULL) {
                qm_snprintf(s_wifi_ip,
                            sizeof(s_wifi_ip),
                            QM_IPSTR,
                            QM_IP2STR(&event_info->got_ip.ip_info.ip));
                QM_LOGI(LOG_TAG,
                        "ip:" QM_IPSTR,
                        QM_IP2STR(&event_info->got_ip.ip_info.ip));
            }
            if (qm_wifi_sta_get_ap_info(&s_wifi_ap_info) == QM_EOK) {
                s_wifi_ap_info_valid = QM_TRUE;
            }

            ret = start_iot();
            if (ret == QM_EOK && s_iot_handle != NULL) {
                stop_provision();
                if (check_ret("devinfo", update_devinfo()) == QM_EOK) {
                    (void)check_ret("network up",
                                    qm_iot_network_update(s_iot_handle,
                                                          QM_IOT_NETWORK_UP));
                }
            }
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG,
                    "wifi disconnected reason=%d",
                    event_info ? event_info->sta_disconnected.reason : -QM_EINVAL);
            s_wifi_ip[0] = '\0';
            s_wifi_ap_info_valid = QM_FALSE;
            qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
            if (s_wifi_reconfiguring) {
                break;
            }
            if (s_iot_handle != NULL) {
                (void)qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_DOWN);
            }
            stop_joyinside_client();
            if (s_wifi_retry_count < CONFIG_JOYINSIDE_CLOUD_EXAMPLE_WIFI_RETRY_MAX_COUNT) {
                s_wifi_retry_count++;
                if (qm_wifi_connect() != QM_EOK) {
                    QM_LOGE(LOG_TAG, "wifi reconnect failed");
                    break;
                }
                QM_LOGI(LOG_TAG,
                        "retry wifi connect %d/%d",
                        s_wifi_retry_count,
                        CONFIG_JOYINSIDE_CLOUD_EXAMPLE_WIFI_RETRY_MAX_COUNT);
            }
            break;

        default:
            break;
    }
}

void opencpu_joyinside_cloud_example_start(void)
{
    qm_err_t ret;
    int call_ret;

    QM_LOGI(LOG_TAG, "joyinside cloud example start");

    ensure_cli_registered(NULL);
    qm_iot_dynreg_env_set(CONFIG_JOYINSIDE_CLOUD_EXAMPLE_DYNREG_ENV);

    if (!s_wifi_event_registered) {
        ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                             wifi_event_handler, NULL);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "wifi event register failed: %d", ret);
            return;
        }
        s_wifi_event_registered = QM_TRUE;
    }

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

    refresh_sn();

    call_ret = start_saved_wifi();
    QM_LOGI(LOG_TAG, "start saved wifi ret=%d", call_ret);
    if (call_ret == QM_EOK) {
        return;
    }

    call_ret = start_provision();
    QM_LOGI(LOG_TAG, "start provision ret=%d", call_ret);
}
