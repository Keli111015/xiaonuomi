#include "ljyd_provision.h"
#include "ljyd_cloud.h"
#include "ljyd_course.h"

#include "qm.h"
#include "qm_iot_dynreg.h"
#include "qm_kv.h"
#include "qm_provision.h"
#include "qm_wifi.h"
#include "qm_wifi_dev.h"
#include "qm_work.h"

#include <string.h>

#define LOG_TAG "provision"

#define PROVISION_KV_BIND      "cloud/bind"
#define PROVISION_KV_ENV       "cloud/env"
/* 蓝牙断连后再等 500ms 才 qm_ble_end。1s 时 Wi-Fi 和协议栈抢内部 RAM，静态 RX 缓冲分配失败。 */
#define PROVISION_WIFI_DELAY_MS 2000

static qm_provision_handle_t provision_handle;
static uint8_t wifi_event_registered;
static qm_work_t wifi_delay_work;
static char pending_ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
static char pending_password[CONFIG_QM_WIFI_PASSWD_MAX_LEN + 1];

static void provision_env_load(void)
{
    uint8_t env = 0;
    int len = (int)sizeof(env);

    if (qm_kv_get(PROVISION_KV_ENV, &env, &len) != QM_EOK || len != (int)sizeof(env)) {
        return;
    }
    if (env > (uint8_t)QM_IOT_DYNREG_ENV_PROD) {
        return;
    }
    (void)qm_iot_dynreg_env_set((qm_iot_dynreg_env_t)env);
    QM_LOGI(LOG_TAG, "dynreg env=%u", (unsigned)env);
}

static void provision_env_save(uint8_t env)
{
    if (env > (uint8_t)QM_IOT_DYNREG_ENV_PROD) {
        return;
    }
    (void)qm_iot_dynreg_env_set((qm_iot_dynreg_env_t)env);
    (void)qm_kv_set(PROVISION_KV_ENV, &env, (int)sizeof(env), 1);
    QM_LOGI(LOG_TAG, "dynreg env save=%u", (unsigned)env);
}

static void provision_set_bound(uint8_t bound)
{
    (void)qm_kv_set(PROVISION_KV_BIND, &bound, (int)sizeof(bound), 1);
}

void ljyd_provision_clear_and_reboot(void)
{
    uint8_t bound = 0;

    (void)qm_kv_set(PROVISION_KV_BIND, &bound, (int)sizeof(bound), 1);
    (void)qm_kv_del(PROVISION_KV_ENV);
    (void)qm_kv_del("wifi_cfg");
    ljyd_course_clear_configs();
    qm_msleep(50);
    qm_reboot();
}

static int provision_apply_wifi(const char *ssid, const char *password)
{
    qm_wifi_config_t config = {0};
    uint32_t ssid_len;
    uint32_t password_len;

    if (ssid == NULL || ssid[0] == '\0') {
        return -QM_EINVAL;
    }
    ssid_len = (uint32_t)strlen(ssid);
    password_len = (password != NULL) ? (uint32_t)strlen(password) : 0;
    if (ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN || password_len > CONFIG_QM_WIFI_PASSWD_MAX_LEN) {
        return -QM_EINVAL;
    }
    memcpy(config.sta.ssid, ssid, ssid_len);
    config.sta.ssid_len = (uint8_t)ssid_len;
    if (password_len > 0) {
        memcpy(config.sta.password, password, password_len);
        config.sta.password_len = (uint8_t)password_len;
    }
    return qm_wifi_set_config(QM_WIFI_IF_STA, &config);
}

static int provision_has_saved_wifi(void)
{
    qm_wifi_cfg_t cfg;
    int len = (int)sizeof(cfg);

    memset(&cfg, 0, sizeof(cfg));
    if (qm_kv_get("wifi_cfg", &cfg, &len) != QM_EOK || len != (int)sizeof(cfg)) {
        return 0;
    }
    return cfg.config.sta.ssid_len > 0 && cfg.config.sta.ssid[0] != '\0';
}

static int provision_start_wifi(void)
{
    int ret = qm_wifi_init();
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_wifi_start();
}

static void provision_wifi_connect_later(void *arg)
{
    int ret;

    (void)arg;
    ret = qm_wifi_init();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi init ret=%d", ret);
        return;
    }
    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi set mode ret=%d", ret);
        return;
    }
    ret = provision_apply_wifi(pending_ssid, pending_password);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "save provision wifi failed ret=%d", ret);
        return;
    }
    ret = qm_wifi_start();
    QM_LOGI(LOG_TAG, "wifi start ssid=%s ret=%d", pending_ssid, ret);
}

static void provision_recv(qm_provision_handle_t handle,
                           const qm_provision_recv_t *recv,
                           void *userdata)
{
    (void)userdata;
    if (recv == NULL || recv->ssid[0] == '\0') {
        return;
    }
    QM_LOGI(LOG_TAG, "provision ssid=%s", recv->ssid);
    if (recv->env_type_valid) {
        provision_env_save(recv->env_type);
    }
    memset(pending_ssid, 0, sizeof(pending_ssid));
    memset(pending_password, 0, sizeof(pending_password));
    strncpy(pending_ssid, recv->ssid, sizeof(pending_ssid) - 1);
    if (recv->password[0] != '\0') {
        strncpy(pending_password, recv->password, sizeof(pending_password) - 1);
    }
    provision_set_bound(1);
    if (handle != NULL) {
        qm_provision_destroy(handle);
        if (provision_handle == handle) {
            provision_handle = NULL;
        }
        QM_LOGI(LOG_TAG, "ble stopped");
    }
    (void)qm_post_delayed_action(&wifi_delay_work, provision_wifi_connect_later,
                                 NULL, PROVISION_WIFI_DELAY_MS);
}

static int provision_start_ble(void)
{
    qm_provision_config_t config = {
        .product_id = CONFIG_XJIANG_CLOUD_PID,
        .product_secret = CONFIG_XJIANG_CLOUD_SECERT,
        .adv_cid = CONFIG_QM_IOT_PROV_BLE_ADV_CID,
        .recv_handler = provision_recv,
        .userdata = NULL,
    };
    int ret;

    if (provision_handle == NULL) {
        provision_handle = qm_provision_create(&config);
        if (provision_handle == NULL) {
            return -QM_ENOMEM;
        }
    }
    ret = qm_provision_start(provision_handle);
    QM_LOGI(LOG_TAG, "ble provision start ret=%d", ret);
    return ret;
}

static void provision_wifi_event_handler(void *arg, qm_event_base_t event_base,
                                         int32_t event_id, void *event_data,
                                         size_t event_data_size)
{
    (void)arg;
    (void)event_base;
    (void)event_data;
    (void)event_data_size;

    switch ((qm_wifi_event_t)event_id) {
        case QM_WIFI_EVENT_STA_START:
            QM_LOGI(LOG_TAG, "wifi sta start");
            (void)qm_wifi_connect();
            break;
        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGI(LOG_TAG, "wifi got ip");
            ljyd_cloud_on_network_up();
            break;
        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG, "wifi disconnected");
            ljyd_cloud_on_network_down();
            (void)qm_wifi_connect();
            break;
        default:
            break;
    }
}

int ljyd_provision_init(void)
{
    int ret;

    if (!wifi_event_registered) {
        ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                        provision_wifi_event_handler, NULL);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "wifi event register failed ret=%d", ret);
            return ret;
        }
        wifi_event_registered = 1;
    }

    provision_env_load();

    if (provision_has_saved_wifi()) {
        QM_LOGI(LOG_TAG, "saved wifi, skip ble");
        return provision_start_wifi();
    }

    QM_LOGI(LOG_TAG, "no saved wifi, start ble provision");
    return provision_start_ble();
}
