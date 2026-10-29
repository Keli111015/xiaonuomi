#include "prodtst_hal_wifi.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "qm_log.h"

#define LOG_TAG  "hal wifi"

static prodtst_wifi_event_cb prodtst_wifi_event_callback = NULL;
static void *wifi_event_cb_arg = NULL;

int prodtst_wifi_deauth_sta(uint16_t aid)
{
    int ret = 0;
    ret = esp_wifi_deauth_sta(aid);
    if(ret != ESP_OK){
        return -QM_ERROR;
    }else{
        return QM_EOK;
    }
}

int prodtst_wifi_ap_init(char *ssid, char *pwd, int channel, int max_conn)
{
     wifi_config_t wifi_config = {0};

    memcpy(wifi_config.ap.ssid, ssid, strlen(ssid));
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = channel;
    memcpy(wifi_config.ap.password, pwd, strlen(pwd));
    wifi_config.ap.max_connection = max_conn;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    QM_LOGI(LOG_TAG, "softap finished. SSID:%s, password:%s, channel:%d, max connection:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.channel, wifi_config.ap.max_connection);

    return QM_EOK;

}


int prodtst_wifi_ap_deinit(void)
{
    return QM_EOK;
}


int prodtst_wifi_sta_mac_get(uint8_t mac[6])
{
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    return QM_EOK;
}


int prodtst_wifi_ap_event_set(prodtst_wifi_event_t *event)
{
    if(prodtst_wifi_event_callback){
        prodtst_wifi_event_callback(event, wifi_event_cb_arg);
    }
    return QM_EOK;
}


int prodtst_wifi_ap_event_register(prodtst_wifi_event_cb cb, void *arg)
{
    if(cb == NULL){
        return -QM_EINVAL;
    }
    prodtst_wifi_event_callback = cb;
    wifi_event_cb_arg = arg;
    return QM_EOK;
}   