/*
 * qm_wifi.c - QUMI WiFi HAL for JieLi (WL82/AC79) Platform
 *
 * Maps QUMI WiFi device operations to JieLi WiFi API.
 * Reference: include_lib/net/wifi/wifi_connect.h
 *
 * Key WiFi API:
 *   wifi_on()               - power up WiFi
 *   wifi_off()              - power down WiFi
 *   wifi_set_event_callback() - register event handler
 *   wifi_enter_sta_mode()   - connect to AP as STA
 *   wifi_enter_ap_mode()    - start as soft-AP
 *   wifi_scan_req()         - start async scan
 *   wifi_get_scan_result()  - get scan results (call after SCAN_COMPLETED event)
 *   wifi_clear_scan_result()- release scan result memory
 *   wifi_get_mac()          - get WiFi MAC address
 *   wifi_set_mac()          - set WiFi MAC address
 *   wifi_get_rssi()         - get connected STA RSSI
 *   wifi_disconnect_station()- AP mode: disconnect a client
 */

/* Avoid 'bool' typedef conflict between qm.h (stdbool) and JieLi internals */
#define BOOL_DEFINE_CONFLICT
#include "qm.h"
#include "qm_wifi_dev.h"
#include "qm_wifi_mgnt.h"
#include "qm_errno.h"
#include "net/wifi/wifi_connect.h"
#include "os/os_api.h"
#include "event/net_event.h"
#include "net/config_network.h"
#include "lwip/netif.h"       /* netif_default */
#include "lwip/ip4_addr.h"    /* netif_ip4_addr, ip4_addr_get_u32 */
#include "lwip/prot/ethernet.h" /* struct eth_addr */
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */

static qm_wifi_dev_t *g_jl_wifi_dev;

#define JL_WIFI_SCAN_EVENT_MAX_AP 16
static qm_wifi_ap_record_t g_jl_wifi_scan_records[JL_WIFI_SCAN_EVENT_MAX_AP];
static uint16_t g_jl_wifi_scan_record_count;

/* Forward declaration — defined in the HAL ops section below */
static void qm_wifi_fire_got_ip(qm_wifi_dev_t *wifi);

/*
 * On JieLi, wifi_enter_sta_mode() both starts STA mode and initiates
 * connection in one call.  QUMI's architecture fires QM_WIFI_EVENT_STA_START
 * after hal_wifi_start() and then immediately calls hal_wifi_connect().
 * If hal_wifi_connect() calls wifi_enter_sta_mode() again while the driver
 * is already starting, the SDK performs save→exit_sta→enter_sta, restarting
 * WiFi in a loop.  This flag lets hal_wifi_connect() skip the redundant call
 * that follows hal_wifi_start() while still handling genuine reconnects.
 */
static bool g_sta_start_pending = false;

static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event);
static int jl_wifi_event_callback(void *network_ctx, enum WIFI_EVENT event);

#define JL_WIFI_CONNECT_TIMEOUT_SEC 60
#define JL_WIFI_INIT_MODE SMP_CFG_MODE
#define JL_WIFI_FORCE_DEFAULT_MODE 1

static qm_err_t jl_wifi_status(int status)
{
    return status == 0 ? QM_EOK : -QM_EIO;
}

static qm_wifi_auth_mode_t jl_wifi_auth_mode(WIFI_802_11_AUTH_MODE mode)
{
    switch (mode) {
    case WIFI_AUTH_MODE_OPEN:
        return QM_WIFI_AUTH_OPEN;
    case WIFI_AUTH_MODE_WEP:
        return QM_WIFI_AUTH_WEP;
    case WIFI_AUTH_MODE_WAPI:
        return QM_WIFI_AUTH_WAPI_PSK;
    case WIFI_AUTH_MODE_WPA:
        return QM_WIFI_AUTH_WPA_PSK;
    case WIFI_AUTH_MODE_WPA2PSK:
        return QM_WIFI_AUTH_WPA2_PSK;
    case WIFI_AUTH_MODE_WPAWPA2PSK:
        return QM_WIFI_AUTH_WPA_WPA2_PSK;
    case WIFI_AUTH_MODE_WPA3SAE:
        return QM_WIFI_AUTH_WPA3_PSK;
    case WIFI_AUTH_MODE_WPA2PSKWPA3SAE:
        return QM_WIFI_AUTH_WPA2_WPA3_PSK;
    case WIFI_AUTH_MODE_WPA3H2E:
        /* QUMI has no separate WPA3-H2E value. */
        return QM_WIFI_AUTH_WPA3_PSK;
    default:
        return QM_WIFI_AUTH_MAX;
    }
}

/* ------------------------------------------------------------------ */
/* WiFi event callback: JieLi → QUMI event translation                 */
/* ------------------------------------------------------------------ */

static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    qm_wifi_event_info_t event_info;
    memset(&event_info, 0, sizeof(event_info));

    QM_LOGI("WIFI", "[WIFI_TRACE][QM] jl event=%d", (int)event);

    qm_wifi_dev_t *wifi = g_jl_wifi_dev;
    if (!wifi || !wifi->event_handler) {
        QM_LOGI("WIFI", "[WIFI_TRACE][QM] DROP jl event=%d (wifi=%p handler=%p)",
                (int)event, (void *)wifi, (wifi ? (void *)wifi->event_handler : NULL));
        return 0;
    }

    if (network_ctx != NULL && event == WIFI_EVENT_AP_ON_ASSOC) {
        memcpy(event_info.ap_staconnected.mac,
               ((struct eth_addr *)network_ctx)->addr,
               sizeof(event_info.ap_staconnected.mac));
    } else if (network_ctx != NULL && event == WIFI_EVENT_AP_ON_DISCONNECTED) {
        memcpy(event_info.ap_stadisconnected.mac,
               ((struct eth_addr *)network_ctx)->addr,
               sizeof(event_info.ap_stadisconnected.mac));
    }

    switch (event) {
    /* ---------- Module ready ---------- */
    case WIFI_EVENT_MODULE_START:
        wifi->event_handler(QM_WIFI_EVENT_READY, NULL, wifi->arg);
        break;

    /* ---------- STA events ---------- */
    case WIFI_EVENT_STA_START:
        /*
         * Set the pending flag here (not only in hal_wifi_start) so that
         * paths which restart STA mode without going through hal_wifi_start
         * — e.g. netmgr_reconnect_wifi() called from the BLE config flow —
         * are also covered.  hal_wifi_connect() will consume the flag and
         * skip the redundant wifi_enter_sta_mode() call.
         */
        g_sta_start_pending = true;
        wifi->event_handler(QM_WIFI_EVENT_STA_START, NULL, wifi->arg);
        break;

    case WIFI_EVENT_STA_STOP:
        g_sta_start_pending = false;
        wifi->event_handler(QM_WIFI_EVENT_STA_STOP, NULL, wifi->arg);
        break;

    case WIFI_EVENT_STA_CONNECT_SUCC:
        wifi->event_handler(QM_WIFI_EVENT_STA_CONNECTED, NULL, wifi->arg);
        break;

    case WIFI_EVENT_STA_DISCONNECT:
        g_sta_start_pending = false;
        event_info.sta_disconnected.reason = QM_WIFI_REASON_CONNECTION_FAIL;
        QM_LOGW("WIFI", "[WIFI_TRACE][QM] sta disconnect raw=%d reason=%d",
                (int)event, event_info.sta_disconnected.reason);
        wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);
        break;

    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        g_sta_start_pending = false;
        event_info.sta_disconnected.reason = QM_WIFI_REASON_NO_AP_FOUND;
        QM_LOGW("WIFI", "[WIFI_TRACE][QM] sta connect failure raw=%d reason=%d",
                (int)event, event_info.sta_disconnected.reason);
        wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        g_sta_start_pending = false;
        event_info.sta_disconnected.reason = QM_WIFI_REASON_ASSOC_TIMEOUT;
        QM_LOGW("WIFI", "[WIFI_TRACE][QM] sta connect failure raw=%d reason=%d",
                (int)event, event_info.sta_disconnected.reason);
        wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
        g_sta_start_pending = false;
        event_info.sta_disconnected.reason = QM_WIFI_REASON_ASSOC_FAIL;
        QM_LOGW("WIFI", "[WIFI_TRACE][QM] sta connect failure raw=%d reason=%d",
                (int)event, event_info.sta_disconnected.reason);
        wifi->event_handler(QM_WIFI_EVENT_STA_DISCONNECTED, &event_info, wifi->arg);
        break;

    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC: {
        /*
         * DHCP succeeded — yield briefly so the lwIP DHCP task finishes
         * writing the lease (IP/mask/GW) into netif_default before we read
         * it.  Without this, netif_default can carry stale or zero values
         * from the previous interface state (observed as garbage netmask).
         */
        QM_LOGI("WIFI", "[WIFI_TRACE][QM] JL DHCP_SUCC, delay 50 ms");
        os_time_dly(5);   /* 5 ticks × 10 ms = 50 ms */
        qm_wifi_fire_got_ip(wifi);
        break;
    }

    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        g_sta_start_pending = false;
        wifi->event_handler(QM_WIFI_EVENT_STA_LOST_IP, NULL, wifi->arg);
        break;

    /* ---------- AP events ---------- */
    case WIFI_EVENT_AP_START:
        wifi->event_handler(QM_WIFI_EVENT_AP_START, NULL, wifi->arg);
        break;

    case WIFI_EVENT_AP_STOP:
        wifi->event_handler(QM_WIFI_EVENT_AP_STOP, NULL, wifi->arg);
        break;

    case WIFI_EVENT_AP_ON_ASSOC:
        wifi->event_handler(QM_WIFI_EVENT_AP_STACONNECTED, &event_info, wifi->arg);
        break;

    case WIFI_EVENT_AP_ON_DISCONNECTED:
        wifi->event_handler(QM_WIFI_EVENT_AP_STADISCONNECTED, &event_info, wifi->arg);
        break;

    /* ---------- Scan ---------- */
    case WIFI_EVENT_STA_SCAN_COMPLETED: {
        u32 sta_number = 0;
        u32 copy_count;
        struct wifi_scan_ssid_info *ap_list = wifi_get_scan_result(&sta_number);
        qm_wifi_event_scan_done_t *scan_done = &event_info.scan_done;

        scan_done->status = 0;
        scan_done->ap_num = 0;
        scan_done->ap_record = NULL;
        g_jl_wifi_scan_record_count = 0;
        copy_count = sta_number;
        if (copy_count > JL_WIFI_SCAN_EVENT_MAX_AP) {
            copy_count = JL_WIFI_SCAN_EVENT_MAX_AP;
        }

        if (copy_count > 0 && ap_list != NULL) {
            memset(g_jl_wifi_scan_records, 0,
                   sizeof(g_jl_wifi_scan_records));
            for (u32 i = 0; i < copy_count; i++) {
                size_t ssid_len = ap_list[i].ssid_len;

                if (ssid_len >= sizeof(g_jl_wifi_scan_records[i].ssid)) {
                    ssid_len = sizeof(g_jl_wifi_scan_records[i].ssid) - 1;
                }
                g_jl_wifi_scan_records[i].rssi = ap_list[i].rssi;
                g_jl_wifi_scan_records[i].channel =
                    (uint8_t)ap_list[i].channel_number;
                g_jl_wifi_scan_records[i].ssid_len = (uint8_t)ssid_len;
                g_jl_wifi_scan_records[i].authmode =
                    jl_wifi_auth_mode(ap_list[i].auth_mode);
                memcpy(g_jl_wifi_scan_records[i].bssid,
                       ap_list[i].mac_addr,
                       sizeof(g_jl_wifi_scan_records[i].bssid));
                memcpy(g_jl_wifi_scan_records[i].ssid,
                       ap_list[i].ssid,
                       ssid_len);
                g_jl_wifi_scan_records[i].ssid[ssid_len] = '\0';
            }
            scan_done->ap_num = (uint8_t)copy_count;
            scan_done->ap_record = g_jl_wifi_scan_records;
            g_jl_wifi_scan_record_count = (uint16_t)copy_count;
        } else if (sta_number > 0) {
            scan_done->status = 1;
        }
        if (ap_list != NULL) {
            wifi_clear_scan_result();
            free(ap_list);
        }

        wifi->event_handler(QM_WIFI_EVENT_SCAN_DONE, &event_info, wifi->arg);
        break;
    }

    default:
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* HAL operations                                                       */
/* ------------------------------------------------------------------ */

/* Read current IP/mask/GW from netif_default and fire QM_WIFI_EVENT_STA_GOT_IP. */
static void qm_wifi_fire_got_ip(qm_wifi_dev_t *wifi)
{
    qm_wifi_event_info_t event_info;
    memset(&event_info, 0, sizeof(event_info));
    qm_ip_info_t *ip = &event_info.got_ip.ip_info;
    if (!netif_default) {
        return;
    }
    ip->ip.addr      = ip4_addr_get_u32(netif_ip4_addr(netif_default));
    ip->netmask.addr = ip4_addr_get_u32(netif_ip4_netmask(netif_default));
    ip->gw.addr      = ip4_addr_get_u32(netif_ip4_gw(netif_default));
    if (ip->ip.addr == 0) {
        return;
    }
    if (wifi && wifi->event_handler) {
        wifi->event_handler(QM_WIFI_EVENT_STA_GOT_IP, &event_info, wifi->arg);
    }
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    g_jl_wifi_dev = wifi;
    g_jl_wifi_scan_record_count = 0;
    /* Configure the sole JieLi callback. QUMI KV is the only persistent
     * credential store, so disable native SSID history. */
    wifi_set_store_ssid_cnt(0);
    wifi_set_sta_connect_timeout(JL_WIFI_CONNECT_TIMEOUT_SEC);
    wifi_set_event_callback(jl_wifi_event_callback);

    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    int status;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (!wifi_is_on()) {
        g_jl_wifi_dev = NULL;
        g_jl_wifi_scan_record_count = 0;
        g_sta_start_pending = false;
        return QM_EOK;
    }
    status = wifi_off();
    g_jl_wifi_dev = NULL;
    g_jl_wifi_scan_record_count = 0;
    g_sta_start_pending = false;
    return jl_wifi_status(status);
}

static qm_err_t jl_wifi_start_sta(qm_wifi_dev_t *wifi)
{
    int ret;

    if (wifi->wifi_cfg.config.sta.ssid[0] == '\0') {
        return -QM_EINVAL;
    }
    if (!wifi_is_on()) {
        g_sta_start_pending = false;
        return -QM_EIO;
    }

    wifi_set_connect_sta_block(0);
    g_sta_start_pending = true;
    ret = wifi_enter_sta_mode((char *)wifi->wifi_cfg.config.sta.ssid,
                              (char *)wifi->wifi_cfg.config.sta.password);
    /* AC79 returns -1 until DHCP succeeds, even after submitting the STA
     * request. Completion/failure comes through events and manager timeouts. */
    if ((ret != 0 && ret != -1) || !wifi_is_on()) {
        g_sta_start_pending = false;
        QM_LOGE("WIFI", "STA request failed ret=%d", ret);
        return -QM_EIO;
    }
    QM_LOGI("WIFI", "STA request submitted ret=%d state=%d", ret,
            (int)wifi_get_sta_connect_state());
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    if (wifi->wifi_cfg.mode == QM_WIFI_MODE_STA) {
        if (wifi->wifi_cfg.config.sta.ssid[0] == '\0') {
            return -QM_EINVAL;
        }
    } else if (wifi->wifi_cfg.mode == QM_WIFI_MODE_AP) {
        if (wifi->wifi_cfg.config.ap.ssid[0] == '\0') {
            return -QM_EINVAL;
        }
    } else {
        return -QM_ERR_WIFI_MODE;
    }

    if (!wifi_is_on()) {
        if (wifi_on() != 0) {
            return -QM_EIO;
        }
    }

    if (wifi->wifi_cfg.mode == QM_WIFI_MODE_STA) {
        /*
         * If the SDK is already in STA mode with the same SSID and is still
         * connected, calling wifi_enter_sta_mode() again triggers a
         * save→exit_sta→enter_sta restart cycle.  Query both mode and link
         * state so a disconnected STA is actually restarted.
         */
        struct wifi_mode_info cur = { .mode = NONE_MODE, .ssid = NULL, .pwd = NULL };
        enum wifi_sta_connect_state state;
        wifi_get_mode_cur_info(&cur);
        state = wifi_get_sta_connect_state();
        if (cur.mode == STA_MODE &&
            cur.ssid != NULL &&
            strcmp(cur.ssid, (char *)wifi->wifi_cfg.config.sta.ssid) == 0 &&
            ((cur.pwd == NULL &&
              wifi->wifi_cfg.config.sta.password[0] == '\0') ||
             (cur.pwd != NULL &&
              strcmp(cur.pwd,
                     (char *)wifi->wifi_cfg.config.sta.password) == 0)) &&
            (state == WIFI_STA_CONNECT_SUCC ||
             state == WIFI_STA_NETWORK_STACK_DHCP_SUCC)) {
            if (state == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
                qm_wifi_fire_got_ip(wifi);
            }
            return QM_EOK;
        }

        return jl_wifi_start_sta(wifi);
    } else {
        if (wifi_enter_ap_mode(
            (char *)wifi->wifi_cfg.config.ap.ssid,
            (char *)wifi->wifi_cfg.config.ap.password) != 0) {
            return -QM_EIO;
        }
    }
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    int status;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (!wifi_is_on()) {
        g_sta_start_pending = false;
        return QM_EOK;
    }
    status = wifi_off();
    g_jl_wifi_scan_record_count = 0;
    g_sta_start_pending = false;
    return jl_wifi_status(status);
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    /*
     * QUMI calls this after QM_WIFI_EVENT_STA_START (first connect) and after
     * QM_WIFI_EVENT_STA_DISCONNECTED (reconnect).
     *
     * On JieLi, wifi_enter_sta_mode() already starts AND connects in
     * hal_wifi_start(), so the post-STA_START call here is redundant.
     * Calling wifi_enter_sta_mode() again while the driver is already in STA
     * mode triggers a save→exit_sta→enter_sta restart cycle.
     *
     * g_sta_start_pending is set by hal_wifi_start() and consumed here once,
     * so the very first call (which follows start) is skipped, while any
     * subsequent call (after a real disconnect) proceeds normally.
     */
    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    if (wifi->wifi_cfg.mode != QM_WIFI_MODE_STA) {
        return -QM_ERR_WIFI_MODE;
    }

    if (wifi->wifi_cfg.config.sta.ssid[0] == '\0') {
        return -QM_EINVAL;
    }

    if (!wifi_is_on()) {
        g_sta_start_pending = false;
        return -QM_EIO;
    }

    if (g_sta_start_pending) {
        g_sta_start_pending = false;
        QM_LOGI("WIFI", "[WIFI_TRACE][QM] hal_wifi_connect: skip (start pending consumed)");
        return QM_EOK;
    }

    return jl_wifi_start_sta(wifi);
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    /*
     * JL has no STA-only disconnect API.  wifi_off() powers down the whole
     * module (RTUSB teardown) and must be reserved for qm_wifi_stop().
     * SSID changes go through wifi_enter_sta_mode() in hal_wifi_start().
     */
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    return -QM_ENOSYS;
}

static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    int ret;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    (void)config;
    g_jl_wifi_scan_record_count = 0;
    memset(g_jl_wifi_scan_records, 0, sizeof(g_jl_wifi_scan_records));
    ret = wifi_scan_req();
    return ret == 0 ? QM_EOK : -QM_EIO;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    /* JieLi does not expose an explicit scan-stop API */
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    return -QM_ENOSYS;
}

static qm_err_t hal_wifi_scan_get_ap_records(qm_wifi_dev_t *wifi,
                                             uint16_t *number,
                                             qm_wifi_ap_record_t *ap_records)
{
    uint16_t copy_count;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (number == NULL || (*number > 0 && ap_records == NULL)) {
        return -QM_EINVAL;
    }

    copy_count = *number;
    if (copy_count > g_jl_wifi_scan_record_count) {
        copy_count = g_jl_wifi_scan_record_count;
    }
    if (copy_count > 0) {
        memcpy(ap_records,
               g_jl_wifi_scan_records,
               sizeof(g_jl_wifi_scan_records[0]) * copy_count);
    }
    *number = copy_count;
    return QM_EOK;
}

static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    /* reason=8: "Deauthenticated because sending station is leaving" */
    /* A NULL MAC means all associated stations per the QUMI API contract. */
    wifi_disconnect_station((char *)mac, 8);
    return QM_EOK;
}

static qm_err_t hal_wifi_set_ps(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type)
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (type > QM_WIFI_PS_MAX_MODEM) {
        return -QM_EINVAL;
    }

    (void)type;
    /* wifi_set_pwr() controls RF power, not modem sleep semantics. */
    return -QM_ENOSYS;
}

static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (mac == NULL) {
        return -QM_EINVAL;
    }
    if (ifx != QM_WIFI_IF_STA) {
        return -QM_ERR_WIFI_IF;
    }

    return jl_wifi_status(wifi_set_mac((char *)mac));
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (mac == NULL) {
        return -QM_EINVAL;
    }
    if (ifx != QM_WIFI_IF_STA) {
        return -QM_ERR_WIFI_IF;
    }

    memset(mac, 0, 6);
    return jl_wifi_status(wifi_get_mac(mac));
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (rssi == NULL) {
        return -QM_EINVAL;
    }

    *rssi = wifi_get_rssi();
    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    struct wifi_mode_info cur = { .mode = NONE_MODE, .ssid = NULL, .pwd = NULL };

    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    if (ap_info == NULL) {
        return -QM_EINVAL;
    }

    memset(ap_info, 0, sizeof(*ap_info));
    if (wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        return -QM_EINVAL;
    }
    wifi_get_mode_cur_info(&cur);
    if (cur.mode != STA_MODE || cur.ssid == NULL || cur.ssid[0] == '\0') {
        return -QM_EINVAL;
    }

    strncpy((char *)ap_info->ssid, cur.ssid, sizeof(ap_info->ssid) - 1);
    ap_info->ssid_len = (uint8_t)strlen((char *)ap_info->ssid);
    wifi_get_bssid(ap_info->bssid);
    ap_info->channel = (uint8_t)wifi_get_channel();
    ap_info->rssi = (int8_t)wifi_get_rssi();
    return QM_EOK;
}

/* JieLi exposes one native callback slot.  The platform adapter owns it and
 * fans events into both QUMI and the legacy NET_EVENT consumers. */
static int jl_wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    struct net_event net = {0};

    if (g_jl_wifi_dev) {
        wifi_event_callback(network_ctx, event);
    }

    switch (event) {
    case WIFI_EVENT_MODULE_INIT: {
        struct wifi_store_info defaults = {0};
        defaults.mode = JL_WIFI_INIT_MODE;
        defaults.connect_best_network = 0;
        wifi_set_default_mode(&defaults, JL_WIFI_FORCE_DEFAULT_MODE, 0);
        break;
    }
    case WIFI_EVENT_MODULE_START: {
        struct wifi_mode_info info = {0};
        u32 rates = 0;
        int i;

        info.mode = NONE_MODE;
        wifi_get_mode_cur_info(&info);
        if (info.mode == SMP_CFG_MODE) {
            wifi_set_smp_cfg_just_monitor_mode(1);
            net.arg = "net";
            net.event = NET_EVENT_SMP_CFG_FIRST;
            net_event_notify(NET_EVENT_FROM_USER, &net);
        }
        for (i = 0; i < 20; ++i) {
            rates |= BIT(i);
        }
        wifi_set_tx_rate_control_tab(rates);
        break;
    }
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        net.arg = "net";
        net.event = NET_CONNECT_TIMEOUT_NOT_FOUND_SSID;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
        net.arg = "net";
        net.event = NET_CONNECT_ASSOCIAT_FAIL;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        config_network_broadcast();
        net.arg = "net";
        net.event = NET_EVENT_CONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_STA_DISCONNECT:
        net.arg = "net";
        net.event = NET_EVENT_DISCONNECTED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_SMP_CFG_TIMEOUT:
        net.arg = "net";
        net.event = NET_EVENT_SMP_CFG_TIMEOUT;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    case WIFI_EVENT_SMP_CFG_COMPLETED:
        net.arg = "net";
        net.event = NET_SMP_CFG_COMPLETED;
        net_event_notify(NET_EVENT_FROM_USER, &net);
        break;
    default:
        break;
    }
    return 0;
}

void wifi_sta_connect(char *ssid, char *pwd, char save)
{
    qm_wifi_config_t config = {0};
    int ret;
    size_t ssid_len;
    size_t password_len;

    if (ssid == NULL || pwd == NULL) {
        return;
    }
    (void)save;
    ssid_len = strlen(ssid);
    password_len = strlen(pwd);
    if (ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN ||
        password_len > CONFIG_QM_WIFI_PASSWD_MAX_LEN) {
        return;
    }
    memcpy(config.sta.ssid, ssid, ssid_len);
    config.sta.ssid_len = (uint8_t)ssid_len;
    memcpy(config.sta.password, pwd, password_len);
    config.sta.password_len = (uint8_t)password_len;
    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        return;
    }
    ret = qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    if (ret == QM_EOK) {
        (void)qm_wifi_start();
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                         */
/* ------------------------------------------------------------------ */

static qm_wifi_dev_ops_t wifi_ops = {
    .wifi_init         = hal_wifi_init,
    .wifi_deinit       = hal_wifi_deinit,
    .wifi_start        = hal_wifi_start,
    .wifi_stop         = hal_wifi_stop,
    .wifi_connect      = hal_wifi_connect,
    .wifi_disconnect   = hal_wifi_disconnet,
    .wifi_scan_start   = hal_wifi_scan_start,
    .wifi_scan_stop    = hal_wifi_scan_stop,
    .wifi_scan_get_ap_records = hal_wifi_scan_get_ap_records,
    .wifi_deauth       = hal_wifi_deauth,
    .wifi_set_ps       = hal_wifi_set_ps,
    .wifi_set_mac      = hal_wifi_set_mac,
    .wifi_get_mac      = hal_wifi_get_mac,
    .wifi_get_sta_rssi = hal_wifi_get_sta_rssi,
    .wifi_get_sta_ap_info = hal_wifi_get_sta_ap_info,
};

int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&wifi_ops, NULL);
}
