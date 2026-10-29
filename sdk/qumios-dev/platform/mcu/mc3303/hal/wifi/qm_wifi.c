
#include "qm_wifi_dev.h"
#include "qm.h"

#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#if defined(__linux__)
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

#ifndef VIRTUAL_NETIF_NAME
#define VIRTUAL_NETIF_NAME "vnet0"
#endif

static void linux_wifi_emit(qm_wifi_dev_t *wifi, qm_wifi_event_t ev, qm_wifi_event_info_t *inf)
{
    if (wifi != NULL && wifi->event_handler != NULL) {
        wifi->event_handler(ev, inf, wifi->arg);
    }
}

#if defined(__linux__)
static int linux_if_name_score(const char *name)
{
    static const char *prefer[] = { "vnet0", "eth0", "enp0s3", "ens33", "wlan0", "wl" };
    size_t i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(prefer) / sizeof(prefer[0]); i++) {
        if (strncmp(name, prefer[i], strlen(prefer[i])) == 0) {
            return (int)(100 - i);
        }
    }
    return 0;
}

static void linux_fallback_ip(qm_ip_info_t *ip_info)
{
    memset(ip_info, 0, sizeof(*ip_info));
    ip_info->ip.addr = htonl((192u << 24) | (168u << 16) | (1u << 8) | 20u);
    ip_info->netmask.addr = htonl((255u << 24) | (255u << 16) | (255u << 8) | 0u);
    ip_info->gw.addr = htonl((192u << 24) | (168u << 16) | (1u << 8) | 1u);
}

/*
 * 主机已由 systemd/NetworkManager 配好网；这里把当前 IPv4 填给 IoT 栈，并合成 WiFi 事件。
 */
static void linux_fill_sta_ip(qm_ip_info_t *ip_info)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;
    const char *best_name = NULL;
    struct sockaddr_in *best_sin = NULL;
    struct sockaddr_in *best_mask = NULL;
    int best_score = -999;

    memset(ip_info, 0, sizeof(*ip_info));

    if (getifaddrs(&ifaddr) != 0) {
        linux_fallback_ip(ip_info);
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        struct sockaddr_in *sin;
        int score;

        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            continue;
        }
        score = linux_if_name_score(ifa->ifa_name);
        if (score > best_score) {
            best_score = score;
            best_name = ifa->ifa_name;
            best_sin = sin;
            if (ifa->ifa_netmask != NULL && ifa->ifa_netmask->sa_family == AF_INET) {
                best_mask = (struct sockaddr_in *)ifa->ifa_netmask;
            } else {
                best_mask = NULL;
            }
        }
    }

    if (best_sin != NULL) {
        ip_info->ip.addr = best_sin->sin_addr.s_addr;
        if (best_mask != NULL) {
            ip_info->netmask.addr = best_mask->sin_addr.s_addr;
        } else {
            ip_info->netmask.addr = htonl(0xffffff00);
        }
        {
            uint32_t ip_h = ntohl(ip_info->ip.addr);
            uint32_t nm_h = ntohl(ip_info->netmask.addr);
            uint32_t net_h = ip_h & nm_h;
            uint32_t gw_h = net_h | 0x01u;

            ip_info->gw.addr = htonl(gw_h);
        }
        (void)best_name;
    } else {
        linux_fallback_ip(ip_info);
    }

    freeifaddrs(ifaddr);
}
#else
static void linux_fill_sta_ip(qm_ip_info_t *ip_info)
{
    memset(ip_info, 0, sizeof(*ip_info));
}
#endif

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (wifi->wifi_cfg.mode == QM_WIFI_MODE_STA) {
        linux_wifi_emit(wifi, QM_WIFI_EVENT_STA_START, NULL);
    } else if (wifi->wifi_cfg.mode == QM_WIFI_MODE_AP) {
        linux_wifi_emit(wifi, QM_WIFI_EVENT_AP_START, NULL);
    }
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return QM_EOK;
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    qm_wifi_event_info_t inf;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    memset(&inf, 0, sizeof(inf));
    linux_wifi_emit(wifi, QM_WIFI_EVENT_STA_CONNECTED, &inf);
    linux_fill_sta_ip(&inf.got_ip.ip_info);
    linux_wifi_emit(wifi, QM_WIFI_EVENT_STA_GOT_IP, &inf);
    return QM_EOK;
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    qm_wifi_event_info_t inf;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    memset(&inf, 0, sizeof(inf));
    inf.sta_disconnected.reason = 0;
    linux_wifi_emit(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, &inf);
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    (void)wifi;
    (void)config;
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_get_ap_records(qm_wifi_dev_t *wifi,
                                             uint16_t *number,
                                             qm_wifi_ap_record_t *ap_records)
{
    (void)wifi;
    (void)ap_records;
    if (number == NULL) {
        return -QM_EINVAL;
    }
    *number = 0;
    return QM_EOK;
}

static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{
    (void)wifi;
    (void)mac;
    return QM_EOK;
}

static qm_err_t hal_wifi_set_ps(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type)
{
    (void)wifi;
    (void)type;
    return QM_EOK;
}

static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    (void)wifi;
    (void)ifx;
    (void)mac;
    return QM_EOK;
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    int sock;
    struct ifreq ifr;

    (void)wifi;
    (void)ifx;
    if (mac == NULL) {
        return -QM_EINVAL;
    }
    memset(mac, 0, 6);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -QM_EIO;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, VIRTUAL_NETIF_NAME, sizeof(ifr.ifr_name) - 1);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) != 0) {
        close(sock);
        return -QM_EIO;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(sock);
    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    (void)wifi;
    if (rssi == NULL) {
        return -QM_EINVAL;
    }
    *rssi = -55;
    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    size_t n;

    if (wifi == NULL || ap_info == NULL) {
        return -QM_EINVAL;
    }
    memset(ap_info, 0, sizeof(*ap_info));
    n = wifi->wifi_cfg.config.sta.ssid_len;
    if (n > CONFIG_QM_WIFI_SSID_MAX_LEN) {
        n = CONFIG_QM_WIFI_SSID_MAX_LEN;
    }
    memcpy(ap_info->ssid, wifi->wifi_cfg.config.sta.ssid, n);
    ap_info->ssid_len = (uint8_t)n;
    ap_info->rssi = -55;
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
    .wifi_scan_get_ap_records = hal_wifi_scan_get_ap_records,
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
