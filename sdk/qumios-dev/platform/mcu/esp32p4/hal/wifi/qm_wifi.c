#include "esp_idf_version.h"

#include "qm.h"
#include "qm_wifi_dev.h"
#include "qm_errno.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_netif.h"
#include "esp_netif_types.h"
#endif

static int wifi_err_transform(esp_err_t err)
{
    qm_err_t ret = QM_EOK;
    switch(err){
        case ESP_OK:
            ret = QM_EOK;
        break;

        case ESP_ERR_WIFI_NOT_INIT:
            ret = -QM_ERR_WIFI_NOT_INIT;
        break;

        case ESP_ERR_INVALID_ARG:
            ret = -QM_EINVAL;
        break;

        case ESP_ERR_NO_MEM:
            ret = -QM_ENOMEM;
        break;

        case ESP_ERR_WIFI_PASSWORD:
            ret = -QM_ERR_WIFI_PASSWORD;
        break;

        case ESP_ERR_WIFI_SSID:
            ret = -QM_ERR_WIFI_SSID;
        break;

        case ESP_FAIL:
            ret = -QM_ERROR;
        break;

        default:
            ret = -QM_ERROR;
        break;
    }
    return ret;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    qm_wifi_event_info_t event_info = {0};
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t*)arg;

    
    qm_wifi_event_ap_staconnected_t *ap_staconnected = &event_info.ap_staconnected;
    qm_wifi_event_sta_disconnected_t *sta_disconnected = &event_info.sta_disconnected;
    qm_wifi_event_ap_stadisconnected_t *ap_stadisconnected = &event_info.ap_stadisconnected;
    qm_wifi_event_scan_done_t *scan_done = &event_info.scan_done;

    if(wifi->event_handler == NULL){
        return;
    }

    switch (event_id) {

        case WIFI_EVENT_WIFI_READY:
            wifi->event_handler(QM_WIFI_EVENT_READY, NULL, wifi->arg);
        break;

        case WIFI_EVENT_STA_START:
            wifi->event_handler(QM_WIFI_EVENT_STA_START, NULL, wifi->arg);
        break;

        case WIFI_EVENT_STA_STOP:
            wifi->event_handler(QM_WIFI_EVENT_STA_STOP, NULL, wifi->arg);
        break;

        case WIFI_EVENT_STA_CONNECTED:
            wifi->event_handler(QM_WIFI_EVENT_STA_CONNECTED, NULL, wifi->arg);
        break;

        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            if(event->reason == WIFI_REASON_NO_AP_FOUND){
                sta_disconnected->reason = QM_WIFI_REASON_NO_AP_FOUND;
            }else {
                sta_disconnected->reason = QM_WIFI_REASON_CONNECTION_FAIL;
            }
            wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);

        break;

        case WIFI_EVENT_AP_START:
            wifi->event_handler(QM_WIFI_EVENT_AP_START, NULL, wifi->arg);
        break;

        case WIFI_EVENT_AP_STOP:
            wifi->event_handler(QM_WIFI_EVENT_AP_STOP, NULL, wifi->arg);
        break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            memcpy(ap_staconnected->mac, &event->mac, 6);
            wifi->event_handler(QM_WIFI_EVENT_AP_STACONNECTED, &event_info, wifi->arg);

        }
        break;

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            memcpy(ap_stadisconnected->mac, &event->mac, 6);
            wifi->event_handler(QM_WIFI_EVENT_AP_STADISCONNECTED, &event_info, wifi->arg);
        }
        break;

        case WIFI_EVENT_SCAN_DONE:
        {
            wifi_event_sta_scan_done_t* event = (wifi_event_sta_scan_done_t*) event_data;
            uint16_t sta_number = 0;
            wifi_ap_record_t *ap_list;
            qm_wifi_ap_record_t *qm_ap_list = NULL;
            scan_done->status = event->status;
            scan_done->ap_num = event->number;
       
            esp_wifi_scan_get_ap_num(&sta_number);
            ap_list = (wifi_ap_record_t *)qm_malloc(sizeof(wifi_ap_record_t) * sta_number);
            if (ap_list == NULL) {
                esp_wifi_clear_ap_list();
                scan_done->status = 1;
                goto ERROR_R;
            }
            memset(ap_list, 0, sizeof(wifi_ap_record_t) * sta_number);

            if (esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)ap_list) != ESP_OK) {
                esp_wifi_clear_ap_list();
                scan_done->status = 1;
                goto ERROR_R;
            }
            qm_ap_list = (qm_wifi_ap_record_t *)qm_malloc(sizeof(qm_wifi_ap_record_t) * event->number);
            if (qm_ap_list == NULL) {
                scan_done->status = 1;
                goto ERROR_R;
            }
            memset(qm_ap_list, 0, sizeof(qm_wifi_ap_record_t) * event->number);
            for(uint8_t i = 0; i < event->number; i++) {
                qm_ap_list[i].rssi = ap_list[i].rssi;
                qm_ap_list[i].channel = ap_list[i].primary;
                qm_ap_list[i].ssid_len = strlen((char *)ap_list[i].ssid);
                qm_ap_list[i].authmode = (qm_wifi_auth_mode_t)ap_list[i].authmode;
                memcpy(qm_ap_list[i].bssid, ap_list[i].bssid, 6);
                strcpy((char *)qm_ap_list[i].ssid, (char *)ap_list[i].ssid);
            }
ERROR_R:
            if(ap_list) {
                qm_free(ap_list);
                ap_list = NULL;
            }
            event_info.scan_done.ap_record = qm_ap_list;
            wifi->event_handler(QM_WIFI_EVENT_SCAN_DONE, &event_info, wifi->arg);
            if(qm_ap_list) {
                qm_free(qm_ap_list);
                qm_ap_list = NULL;
            }
        }
        break;      

        default:
        break;
    }
    return;
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    qm_wifi_event_info_t event_info = {0};
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t*)arg;
    qm_ip_info_t *qm_ip_info = &event_info.got_ip.ip_info;
    qm_wifi_event_ap_staipassigned_t *ap_staipassigned = &event_info.ap_staipassigned;

    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t*)event_data;

            qm_ip_info->ip.addr = event->ip_info.ip.addr;
            qm_ip_info->netmask.addr = event->ip_info.netmask.addr;
            qm_ip_info->gw.addr = event->ip_info.gw.addr;

            wifi->event_handler(QM_WIFI_EVENT_STA_GOT_IP, &event_info, wifi->arg);
        }
        break;
        
        case IP_EVENT_STA_LOST_IP:
            wifi->event_handler(QM_WIFI_EVENT_STA_LOST_IP, NULL, wifi->arg);
        break;

        case IP_EVENT_AP_STAIPASSIGNED:
        {
            ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t*)event_data;

            ap_staipassigned->ip.addr = event->ip.addr;
            wifi->event_handler(QM_WIFI_EVENT_AP_STAIPASSIGNED, &event_info, wifi->arg);
        }
        break;
        default:
        break;
    }
    return;
}

#else
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t*)ctx;

    qm_wifi_event_info_t event_info = {0};

    esp_netif_ip_info_t *ip_info = &event->event_info.got_ip.ip_info;
    qm_ip_info_t *qm_ip_info = &event_info.got_ip.ip_info;
    qm_wifi_event_ap_staconnected_t *ap_staconnected = &event_info.ap_staconnected;
    qm_wifi_event_sta_disconnected_t *sta_disconnected = &event_info.sta_disconnected;
    qm_wifi_event_ap_stadisconnected_t *ap_stadisconnected = &event_info.ap_stadisconnected;
    qm_wifi_event_ap_staipassigned_t *ap_staipassigned = &event_info.ap_staipassigned;
    qm_wifi_event_scan_done_t *scan_done = &event_info.scan_done;

    if(wifi->event_handler == NULL){
        return ESP_OK;
    }

    switch(event->event_id) {

        case SYSTEM_EVENT_WIFI_READY:

           wifi->event_handler(QM_WIFI_EVENT_READY, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_START:

            wifi->event_handler(QM_WIFI_EVENT_STA_START, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_STOP:

            wifi->event_handler(QM_WIFI_EVENT_STA_STOP, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_CONNECTED:

            wifi->event_handler(QM_WIFI_EVENT_STA_CONNECTED, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_GOT_IP:

            qm_ip_info->ip.addr = ip_info->ip.addr;
            qm_ip_info->netmask.addr = ip_info->netmask.addr;
            qm_ip_info->gw.addr = ip_info->gw.addr;

            wifi->event_handler(QM_WIFI_EVENT_STA_GOT_IP, &event_info, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_LOST_IP:

            wifi->event_handler(QM_WIFI_EVENT_STA_LOST_IP, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_STA_DISCONNECTED:

            if(event->event_info.disconnected.reason == WIFI_REASON_NO_AP_FOUND){
                sta_disconnected->reason = QM_WIFI_REASON_NO_AP_FOUND;
            }else {
                sta_disconnected->reason = QM_WIFI_REASON_CONNECTION_FAIL;
            }
            wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);

        break;

        case SYSTEM_EVENT_AP_START:

            wifi->event_handler(QM_WIFI_EVENT_AP_START, NULL, wifi->arg);

        break;

        case SYSTEM_EVENT_AP_STOP:

            wifi->event_handler(QM_WIFI_EVENT_AP_STOP, NULL, wifi->arg);
        break;

        case SYSTEM_EVENT_AP_STACONNECTED:

            memcpy(ap_staconnected->mac, &event->event_info.sta_connected.mac, 6);
            wifi->event_handler(QM_WIFI_EVENT_AP_STACONNECTED, &event_info, wifi->arg);

        break;

        case SYSTEM_EVENT_AP_STADISCONNECTED:

            memcpy(ap_stadisconnected->mac, &event->event_info.sta_disconnected.mac, 6);
            wifi->event_handler(QM_WIFI_EVENT_AP_STADISCONNECTED, &event_info, wifi->arg);
        break;

        case SYSTEM_EVENT_AP_STAIPASSIGNED:

            ap_staipassigned->ip.addr = event->event_info.ap_staipassigned.ip.addr;
            wifi->event_handler(QM_WIFI_EVENT_AP_STAIPASSIGNED, &event_info, wifi->arg);
        break;

        case SYSTEM_EVENT_SCAN_DONE:

            scan_done->status = event->event_info.scan_done.status;
            scan_done->ap_num = event->event_info.scan_done.number;
            wifi->event_handler(QM_WIFI_EVENT_SCAN_DONE, &event_info, wifi->arg);
        break;

        default:
        break;
    }

    return ESP_OK;
}
#endif

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    wifi_mode_t mode = WIFI_MODE_NULL;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, wifi);
        if (err != ESP_OK) {
            return wifi_err_transform(err);
        }
        err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, wifi);
        if (err != ESP_OK) {
            return wifi_err_transform(err);
        }
        return QM_EOK;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return wifi_err_transform(err);
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return wifi_err_transform(err);
    }
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, wifi);
    if (err != ESP_OK) {
        return wifi_err_transform(err);
    }
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, wifi);
    if (err != ESP_OK) {
        return wifi_err_transform(err);
    }
    esp_netif_create_default_wifi_sta();
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    esp_netif_create_default_wifi_ap();
#endif
#else
    tcpip_adapter_init();
    esp_event_loop_init(event_handler, wifi);
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK;
    wifi_config_t wifi_config = {0};

    if(wifi->wifi_cfg.mode == QM_WIFI_MODE_STA){
        esp_wifi_set_mode(WIFI_MODE_STA);
        memcpy(wifi_config.sta.ssid, wifi->wifi_cfg.config.sta.ssid, wifi->wifi_cfg.config.sta.ssid_len);
        memcpy(wifi_config.sta.password, wifi->wifi_cfg.config.sta.password, wifi->wifi_cfg.config.sta.password_len);
        wifi_config.sta.channel = wifi->wifi_cfg.config.sta.channel;
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }
        err = esp_wifi_start();
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }

    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    else if(wifi->wifi_cfg.mode == QM_WIFI_MODE_AP){
        esp_wifi_set_mode(WIFI_MODE_AP);

        memcpy(wifi_config.ap.ssid, wifi->wifi_cfg.config.ap.ssid, wifi->wifi_cfg.config.ap.ssid_len);
        wifi_config.ap.ssid_len = wifi->wifi_cfg.config.ap.ssid_len;
        memcpy(wifi_config.ap.password, wifi->wifi_cfg.config.ap.password, wifi->wifi_cfg.config.ap.password_len);
        wifi_config.ap.channel = wifi->wifi_cfg.config.ap.channel;
        wifi_config.ap.max_connection = wifi->wifi_cfg.config.ap.max_connection;
        wifi_config.ap.ssid_hidden = wifi->wifi_cfg.config.ap.ssid_hidden;
        wifi_config.ap.authmode = wifi->wifi_cfg.config.ap.authmode;

        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }
        err = esp_wifi_start();
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }
    }else if(wifi->wifi_cfg.mode == QM_WIFI_MODE_APSTA){
        esp_wifi_set_mode(WIFI_MODE_APSTA);

        memcpy(wifi_config.sta.ssid, wifi->wifi_cfg.config.sta.ssid, wifi->wifi_cfg.config.sta.ssid_len);
        memcpy(wifi_config.sta.password, wifi->wifi_cfg.config.sta.password, wifi->wifi_cfg.config.sta.password_len);
        wifi_config.sta.channel = wifi->wifi_cfg.config.sta.channel;
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }

        memcpy(wifi_config.ap.ssid, wifi->wifi_cfg.config.ap.ssid, wifi->wifi_cfg.config.ap.ssid_len);
        wifi_config.ap.ssid_len = wifi->wifi_cfg.config.ap.ssid_len;
        memcpy(wifi_config.ap.password, wifi->wifi_cfg.config.ap.password, wifi->wifi_cfg.config.ap.password_len);
        wifi_config.ap.channel = wifi->wifi_cfg.config.ap.channel;
        wifi_config.ap.max_connection = wifi->wifi_cfg.config.ap.max_connection;
        wifi_config.ap.ssid_hidden = wifi->wifi_cfg.config.ap.ssid_hidden;
        wifi_config.ap.authmode = wifi->wifi_cfg.config.ap.authmode;

        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }

        err = esp_wifi_start();
        if(err != ESP_OK){
            return wifi_err_transform(err);
        }
    }
#endif
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK;
    err = esp_wifi_stop();
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK;
    err = esp_wifi_connect();
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK;
    err = esp_wifi_disconnect();
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    esp_err_t err = ESP_OK;
    if(config == NULL){
        if(wifi->wifi_cfg.mode != QM_WIFI_MODE_STA){
            return -QM_ERR_WIFI_MODE;
        }
        esp_wifi_set_mode(WIFI_MODE_STA);
        err = esp_wifi_scan_start(NULL, false);
    }else{
        return -QM_ERROR;
    }
    
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    esp_err_t err = ESP_OK; 
    err = esp_wifi_scan_stop();
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{   
    esp_err_t err = ESP_OK; 
    uint16_t aid = 0;
    esp_wifi_ap_get_sta_aid(mac, &aid);
    err = esp_wifi_deauth_sta(aid);
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_set_ps(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type)
{
    esp_err_t err = ESP_OK; 
    if(type == QM_WIFI_PS_MIN_MODEM){
        err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }else{
        err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    }
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    esp_err_t err = ESP_OK; 
    if(ifx == QM_WIFI_IF_STA){
        err = esp_wifi_set_mac(WIFI_IF_STA, mac);
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    else if(ifx == QM_WIFI_IF_AP){
        err = esp_wifi_set_mac(WIFI_IF_AP, mac);
    }
#endif
    else{
        return -QM_ERR_WIFI_IF;
    }
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    esp_err_t err = ESP_OK; 
    if(ifx == QM_WIFI_IF_STA){
        err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    else if(ifx == QM_WIFI_IF_AP){
        err = esp_wifi_get_mac(WIFI_IF_AP, mac);
    }
#endif
    else{
        return -QM_ERR_WIFI_IF;
    }
    return wifi_err_transform(err);
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    return esp_wifi_sta_get_rssi((int *)rssi);
#else
    return 0;
#endif
}


static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    esp_err_t err;
    wifi_ap_record_t record = {0};

    (void)wifi;

    if (ap_info == NULL) {
        return -QM_EINVAL;
    }

    err = esp_wifi_sta_get_ap_info(&record);
    if (err != ESP_OK) {
        return wifi_err_transform(err);
    }

    memset(ap_info, 0, sizeof(*ap_info));
    ap_info->rssi = record.rssi;
    ap_info->channel = record.primary;
    ap_info->ssid_len = (uint8_t)strlen((char *)record.ssid);
    ap_info->authmode = (qm_wifi_auth_mode_t)record.authmode;
    memcpy(ap_info->bssid, record.bssid, sizeof(ap_info->bssid));
    qm_snprintf((char *)ap_info->ssid, sizeof(ap_info->ssid), "%s", (char *)record.ssid);

    return QM_EOK;
}

static qm_wifi_dev_ops_t wifi_ops =
{
    .wifi_init              = hal_wifi_init,
    .wifi_deinit            = hal_wifi_deinit,
    .wifi_start             = hal_wifi_start,
    .wifi_stop              = hal_wifi_stop,
    .wifi_connect           = hal_wifi_connect,
    .wifi_disconnect        = hal_wifi_disconnet,
    .wifi_scan_start        = hal_wifi_scan_start,
    .wifi_scan_stop         = hal_wifi_scan_stop,
    .wifi_deauth            = hal_wifi_deauth,
    .wifi_set_ps            = hal_wifi_set_ps,
    .wifi_set_mac           = hal_wifi_set_mac,
    .wifi_get_mac           = hal_wifi_get_mac,
    .wifi_get_sta_rssi      = hal_wifi_get_sta_rssi,
    .wifi_get_sta_ap_info   = hal_wifi_get_sta_ap_info,
};


int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&wifi_ops, NULL);
}