#include "qm.h"
#include "qm_wifi_dev.h"

#include <string.h>

#include <components/event.h>
#include <components/netif.h>
#include <components/netif_types.h>
#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include <bk_wifi_adapter.h>

#include <lwip/inet.h>

#define LOG_TAG "HAL WIFI"

typedef struct {
    uint8_t inited;
    uint8_t sta_started;
} qm_wifi_hal_ctx_t;

static qm_wifi_hal_ctx_t g_wifi_hal;

static void qm_wifi_notify(qm_wifi_dev_t *wifi, qm_wifi_event_t event,
                           qm_wifi_event_info_t *info)
{
    if (wifi != NULL && wifi->event_handler != NULL) {
        wifi->event_handler(event, info, wifi->arg);
    }
}

static void qm_ip4_str_to_addr(const char *ip_str, qm_ip4_addr_t *out)
{
    ip4_addr_t ip = {0};

    if (ip_str == NULL || out == NULL) {
        return;
    }

    if (ip4addr_aton(ip_str, &ip)) {
        out->addr = ip.addr;
    }
}

static int bk_wifi_event_cb(void *arg, event_module_t event_module, int event_id, void *event_data)
{
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t *)arg;
    qm_wifi_event_info_t event_info = {0};

    (void)event_module;

    if (wifi == NULL || wifi->event_handler == NULL) {
        return BK_OK;
    }

    switch (event_id) {
    case EVENT_WIFI_STA_CONNECTED:
        qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_CONNECTED, NULL);
        break;

    case EVENT_WIFI_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

        if (disc != NULL && disc->disconnect_reason == WIFI_REASON_NO_AP_FOUND) {
            event_info.sta_disconnected.reason = QM_WIFI_REASON_NO_AP_FOUND;
        } else {
            event_info.sta_disconnected.reason = QM_WIFI_REASON_CONNECTION_FAIL;
        }
        /* IP is gone with link; keep upper layer consistent with ESP32 path. */
        qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_LOST_IP, NULL);
        qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, &event_info);
        break;
    }

    default:
        break;
    }

    return BK_OK;
}

static int bk_netif_event_cb(void *arg, event_module_t event_module, int event_id, void *event_data)
{
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t *)arg;
    qm_wifi_event_info_t event_info = {0};
    netif_event_got_ip4_t *got_ip;

    (void)event_module;

    if (wifi == NULL || wifi->event_handler == NULL) {
        return BK_OK;
    }

    if (event_id == EVENT_NETIF_DHCP_TIMEOUT) {
        qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_LOST_IP, NULL);
        return BK_OK;
    }

    if (event_id != EVENT_NETIF_GOT_IP4) {
        return BK_OK;
    }

    got_ip = (netif_event_got_ip4_t *)event_data;
    if (got_ip == NULL || got_ip->netif_if != NETIF_IF_STA) {
        return BK_OK;
    }

    qm_ip4_str_to_addr(got_ip->ip, &event_info.got_ip.ip_info.ip);
    qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_GOT_IP, &event_info);

    return BK_OK;
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    bk_err_t ret;

    if (g_wifi_hal.inited) {
        return QM_EOK;
    }

    /*
     * bk_init() already called bk_event_init / bk_netif_init / bk_wifi_init.
     * Calling bk_netif_init again re-registers the same netif callback and
     * returns BK_ERR_EVENT_CB_EXIST (-0x1b04). Only register QM callbacks here.
     */
    ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, bk_wifi_event_cb, wifi);
    if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST) {
        QM_LOGE(LOG_TAG, "reg wifi cb failed %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    ret = bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL, bk_netif_event_cb, wifi);
    if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST) {
        QM_LOGE(LOG_TAG, "reg netif cb failed %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    g_wifi_hal.inited = 1;
    qm_wifi_notify(wifi, QM_WIFI_EVENT_READY, NULL);

    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    wifi_sta_config_t sta_config = WIFI_DEFAULT_STA_CONFIG();
    bk_err_t ret;

    if (wifi->wifi_cfg.mode != QM_WIFI_MODE_STA) {
        return -QM_ERR_WIFI_MODE;
    }

    memset(sta_config.ssid, 0, sizeof(sta_config.ssid));
    memset(sta_config.password, 0, sizeof(sta_config.password));
    memcpy(sta_config.ssid, wifi->wifi_cfg.config.sta.ssid, sizeof(sta_config.ssid));
    memcpy(sta_config.password, wifi->wifi_cfg.config.sta.password, sizeof(sta_config.password));

    /* 0/0: let BK supplicant auto-reconnect; app need not call qm_wifi_connect(). */
    sta_config.auto_reconnect_count = 0;
    sta_config.auto_reconnect_timeout = 0;
    sta_config.disable_auto_reconnect_after_disconnect = false;

    ret = bk_wifi_sta_set_config(&sta_config);
    if (ret != BK_OK) {
        QM_LOGE(LOG_TAG, "sta set_config %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    ret = bk_wifi_sta_start();
    if (ret != BK_OK) {
        QM_LOGE(LOG_TAG, "sta start %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    g_wifi_hal.sta_started = 1;
    qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_START, NULL);

    /* First connect is HAL responsibility (ESP-style STA_START→connect lives here). */
    ret = bk_wifi_sta_connect();
    if (ret != BK_OK) {
        QM_LOGE(LOG_TAG, "sta connect %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    if (!g_wifi_hal.sta_started) {
        return QM_EOK;
    }

    bk_wifi_sta_stop();
    g_wifi_hal.sta_started = 0;
    qm_wifi_notify(wifi, QM_WIFI_EVENT_STA_STOP, NULL);
    return QM_EOK;
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    (void)wifi;

    /* Optional: app may still call this; BK treats it as reconnect. */
    return (bk_wifi_sta_connect() == BK_OK) ? QM_EOK : -QM_ERROR;
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return (bk_wifi_sta_disconnect() == BK_OK) ? QM_EOK : -QM_ERROR;
}

static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    (void)wifi;
    (void)ifx;
    (void)mac;
    return -QM_ERR_WIFI_IF;
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    (void)wifi;

    if (ifx != QM_WIFI_IF_STA || mac == NULL) {
        return -QM_ERR_WIFI_IF;
    }

    return (bk_wifi_sta_get_mac(mac) == BK_OK) ? QM_EOK : -QM_ERROR;
}

static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    wifi_link_status_t link = {0};

    (void)wifi;

    if (ap_info == NULL) {
        return -QM_EINVAL;
    }

    if (bk_wifi_sta_get_link_status(&link) != BK_OK) {
        return -QM_ERROR;
    }

    memset(ap_info, 0, sizeof(*ap_info));
    ap_info->rssi = link.rssi;
    ap_info->channel = link.channel;
    ap_info->ssid_len = (uint8_t)strlen((char *)link.ssid);
    memcpy(ap_info->ssid, link.ssid, sizeof(ap_info->ssid));
    memcpy(ap_info->bssid, link.bssid, sizeof(ap_info->bssid));

    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    wifi_link_status_t link = {0};

    (void)wifi;

    if (rssi == NULL) {
        return -QM_EINVAL;
    }

    if (bk_wifi_sta_get_link_status(&link) != BK_OK) {
        return -QM_ERROR;
    }

    *rssi = link.rssi;
    return QM_EOK;
}

static qm_wifi_dev_ops_t wifi_ops = {
    .wifi_init = hal_wifi_init,
    .wifi_deinit = hal_wifi_deinit,
    .wifi_start = hal_wifi_start,
    .wifi_stop = hal_wifi_stop,
    .wifi_connect = hal_wifi_connect,
    .wifi_disconnect = hal_wifi_disconnet,
    .wifi_set_mac = hal_wifi_set_mac,
    .wifi_get_mac = hal_wifi_get_mac,
    .wifi_get_sta_ap_info = hal_wifi_get_sta_ap_info,
    .wifi_get_sta_rssi = hal_wifi_get_sta_rssi,
};

int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&wifi_ops, NULL);
}
