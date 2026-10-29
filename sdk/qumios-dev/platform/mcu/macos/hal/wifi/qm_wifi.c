#include "qm.h"
#include "qm_wifi_corewlan.h"
#include "qm_wifi_dev.h"
#include "qm_errno.h"

#include <ctype.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_TAG "macos wifi"
#define QM_MACOS_WIFI_IF_ENV "QM_MACOS_WIFI_IF"
#define QM_MACOS_WIFI_DEFAULT_IF "en0"
#define QM_MACOS_AIRPORT "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport"
#define QM_MACOS_WIFI_IP_WAIT_MS 10000U
#define QM_MACOS_WIFI_CURRENT_SSID_MARKER "macos-current"

typedef struct {
    char interface_name[32];
    int started;
    int reuse_current_network;
    int adopted_current_network;
    qm_wifi_ap_record_t adopted_ap_info;
    qm_ip_info_t adopted_ip_info;
} mac_wifi_priv_t;

static mac_wifi_priv_t g_wifi_priv;

static const char *wifi_ifname(void)
{
    const char *name = getenv(QM_MACOS_WIFI_IF_ENV);
    return name != NULL && name[0] != '\0' ? name : QM_MACOS_WIFI_DEFAULT_IF;
}

static int shell_quote(const char *input, char *output, size_t output_len)
{
    size_t in = 0;
    size_t out = 0;
    if (input == NULL || output_len < 3) {
        return -QM_EINVAL;
    }
    output[out++] = '\'';
    while (input[in] != '\0') {
        if (input[in] == '\'') {
            if (out + 4 >= output_len) {
                return -QM_EFULL;
            }
            memcpy(output + out, "'\\''", 4);
            out += 4;
        } else {
            if (out + 1 >= output_len) {
                return -QM_EFULL;
            }
            output[out++] = input[in];
        }
        in++;
    }
    if (out + 2 > output_len) {
        return -QM_EFULL;
    }
    output[out++] = '\'';
    output[out] = '\0';
    return QM_EOK;
}

static void post_event(qm_wifi_dev_t *wifi, qm_wifi_event_t event,
                       qm_wifi_event_info_t *info)
{
    if (wifi != NULL && wifi->event_handler != NULL) {
        wifi->event_handler(event, info, wifi->arg);
    }
}

static int set_airport_power(int enabled)
{
    char quoted_if[64];
    char command[192];

    if (access("/usr/sbin/networksetup", X_OK) != 0 ||
        shell_quote(g_wifi_priv.interface_name, quoted_if, sizeof(quoted_if)) != QM_EOK) {
        return -QM_ENOSYS;
    }
    if (snprintf(command, sizeof(command),
                 "/usr/sbin/networksetup -setairportpower %s %s >/dev/null 2>&1",
                 quoted_if, enabled ? "on" : "off") >= (int)sizeof(command)) {
        return -QM_EFULL;
    }
    return system(command) == 0 ? QM_EOK : -QM_EIO;
}

static int get_ipv4_info(qm_ip_info_t *info)
{
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *entry;
    int ret = -QM_ENOENT;

    if (info == NULL || getifaddrs(&interfaces) != 0) {
        return -QM_EIO;
    }
    memset(info, 0, sizeof(*info));
    for (entry = interfaces; entry != NULL; entry = entry->ifa_next) {
        const struct sockaddr_in *address;
        const struct sockaddr_in *netmask;

        if (entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET ||
            strcmp(entry->ifa_name, g_wifi_priv.interface_name) != 0 ||
            (entry->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        address = (const struct sockaddr_in *)entry->ifa_addr;
        if (address->sin_addr.s_addr == htonl(INADDR_ANY)) {
            continue;
        }
        info->ip.addr = address->sin_addr.s_addr;
        if (entry->ifa_netmask != NULL) {
            netmask = (const struct sockaddr_in *)entry->ifa_netmask;
            info->netmask.addr = netmask->sin_addr.s_addr;
        }
        ret = QM_EOK;
        break;
    }
    freeifaddrs(interfaces);
    return ret;
}

static char *trim_text(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static int parse_bssid(const char *text, uint8_t bssid[6])
{
    unsigned int bytes[6];

    if (sscanf(text, "%x:%x:%x:%x:%x:%x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return -QM_EINVAL;
    }
    for (int i = 0; i < 6; ++i) {
        bssid[i] = (uint8_t)bytes[i];
    }
    return QM_EOK;
}

static int current_ap_info_from_airport(qm_wifi_ap_record_t *info)
{
    FILE *pipe;
    char command[320];
    char line[512];
    int found = 0;

    if (access(QM_MACOS_AIRPORT, X_OK) != 0) {
        return -QM_ENOSYS;
    }
    snprintf(command, sizeof(command), "%s -I 2>/dev/null", QM_MACOS_AIRPORT);
    pipe = popen(command, "r");
    if (pipe == NULL) {
        return -QM_EIO;
    }
    while (fgets(line, sizeof(line), pipe) != NULL) {
        char *separator = strchr(line, ':');
        char *key;
        char *value;

        if (separator == NULL) {
            continue;
        }
        *separator = '\0';
        key = trim_text(line);
        value = trim_text(separator + 1);
        if (strcmp(key, "SSID") == 0) {
            size_t len = strlen(value);
            if (len <= CONFIG_QM_WIFI_SSID_MAX_LEN) {
                memcpy(info->ssid, value, len);
                info->ssid[len] = '\0';
                info->ssid_len = (uint8_t)len;
                found = len != 0U;
            }
        } else if (strcmp(key, "BSSID") == 0) {
            (void)parse_bssid(value, info->bssid);
        } else if (strcmp(key, "agrCtlRSSI") == 0) {
            info->rssi = (int8_t)strtol(value, NULL, 10);
        } else if (strcmp(key, "channel") == 0) {
            info->channel = (uint8_t)strtoul(value, NULL, 10);
        } else if (strstr(key, "auth") != NULL) {
            info->authmode = strstr(value, "WPA3") != NULL ? QM_WIFI_AUTH_WPA3_PSK :
                             strstr(value, "WPA") != NULL ? QM_WIFI_AUTH_WPA2_PSK :
                                                            QM_WIFI_AUTH_OPEN;
        }
    }
    (void)pclose(pipe);
    return found ? QM_EOK : -QM_ENOENT;
}

static int current_ssid_from_networksetup(qm_wifi_ap_record_t *info)
{
    char quoted_if[64];
    char command[256];
    char line[512];
    char *separator;
    char *ssid;
    FILE *pipe;
    size_t len;

    if (shell_quote(g_wifi_priv.interface_name, quoted_if, sizeof(quoted_if)) != QM_EOK) {
        return -QM_EINVAL;
    }
    snprintf(command, sizeof(command),
             "/usr/sbin/networksetup -getairportnetwork %s 2>/dev/null", quoted_if);
    pipe = popen(command, "r");
    if (pipe == NULL || fgets(line, sizeof(line), pipe) == NULL) {
        if (pipe != NULL) {
            (void)pclose(pipe);
        }
        return -QM_EIO;
    }
    (void)pclose(pipe);
    separator = strchr(line, ':');
    if (separator == NULL) {
        return -QM_ENOENT;
    }
    ssid = trim_text(separator + 1);
    len = strlen(ssid);
    if (len == 0U || len > CONFIG_QM_WIFI_SSID_MAX_LEN) {
        return -QM_ENOENT;
    }
    memcpy(info->ssid, ssid, len);
    info->ssid[len] = '\0';
    info->ssid_len = (uint8_t)len;
    return QM_EOK;
}

static int get_current_ap_info(qm_wifi_ap_record_t *info)
{
    int ret;

    if (info == NULL) {
        return -QM_EINVAL;
    }

    memset(info, 0, sizeof(*info));
    ret = qm_macos_wifi_get_ap_info(g_wifi_priv.interface_name, info);
    if (ret != QM_EOK) {
        (void)current_ap_info_from_airport(info);
    }
    if (info->ssid_len == 0U) {
        (void)current_ssid_from_networksetup(info);
    }
    return info->ssid_len != 0U ? QM_EOK : ret;
}

static int adopt_current_network(qm_wifi_dev_t *wifi)
{
    qm_wifi_ap_record_t ap_info;
    qm_ip_info_t ip_info;
    const uint8_t *ssid;
    size_t ssid_len;
    int ret;

    if (wifi == NULL || g_wifi_priv.interface_name[0] == '\0') {
        return -QM_EINVAL;
    }

    ret = get_current_ap_info(&ap_info);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = get_ipv4_info(&ip_info);
    if (ret != QM_EOK) {
        return ret;
    }

    g_wifi_priv.adopted_ap_info = ap_info;
    g_wifi_priv.adopted_ip_info = ip_info;
    g_wifi_priv.adopted_current_network = 1;

    if (ap_info.ssid_len != 0U) {
        ssid = ap_info.ssid;
        ssid_len = ap_info.ssid_len;
    } else {
        ssid = (const uint8_t *)QM_MACOS_WIFI_CURRENT_SSID_MARKER;
        ssid_len = sizeof(QM_MACOS_WIFI_CURRENT_SSID_MARKER) - 1U;
    }
    memset(&wifi->wifi_cfg.config.sta, 0, sizeof(wifi->wifi_cfg.config.sta));
    memcpy(wifi->wifi_cfg.config.sta.ssid, ssid, ssid_len);
    wifi->wifi_cfg.config.sta.ssid_len = (uint8_t)ssid_len;

    QM_LOGI(LOG_TAG,
            "adopt current network interface=%s ssid=%s ip=" QM_IPSTR,
            g_wifi_priv.interface_name,
            ap_info.ssid,
            QM_IP2STR(&ip_info.ip));
    return QM_EOK;
}

void qm_macos_wifi_set_reuse_current_network(qm_bool enabled)
{
    g_wifi_priv.reuse_current_network = enabled ? 1 : 0;
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    if (wifi == NULL || wifi->priv == NULL) {
        return -QM_EINVAL;
    }
    strncpy(g_wifi_priv.interface_name, wifi_ifname(), sizeof(g_wifi_priv.interface_name) - 1);
    g_wifi_priv.interface_name[sizeof(g_wifi_priv.interface_name) - 1] = '\0';
    if (g_wifi_priv.reuse_current_network) {
        int ret = adopt_current_network(wifi);

        if (ret != QM_EOK) {
            QM_LOGI(LOG_TAG, "current network unavailable ret=%d", ret);
        }
    }
    post_event(wifi, QM_WIFI_EVENT_READY, NULL);
    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    g_wifi_priv.started = 0;
    g_wifi_priv.adopted_current_network = 0;
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    int ret;

    if (wifi == NULL || wifi->wifi_cfg.mode == QM_WIFI_MODE_NONE) {
        return -QM_ERR_WIFI_MODE;
    }
    if (!g_wifi_priv.adopted_current_network) {
        ret = set_airport_power(1);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    g_wifi_priv.started = 1;
    post_event(wifi, QM_WIFI_EVENT_STA_START, NULL);
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    g_wifi_priv.started = 0;
    post_event(wifi, QM_WIFI_EVENT_STA_STOP, NULL);
    return QM_EOK;
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    char ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
    char password[CONFIG_QM_WIFI_PASSWD_MAX_LEN + 1];
    char quoted_if[64];
    char quoted_ssid[CONFIG_QM_WIFI_SSID_MAX_LEN * 4 + 8];
    char quoted_password[CONFIG_QM_WIFI_PASSWD_MAX_LEN * 4 + 8];
    char command[512];
    qm_wifi_event_info_t info;
    uint32_t waited_ms;
    int rc;

    if (wifi == NULL || !g_wifi_priv.started) {
        return -QM_ERR_WIFI_NOT_STARTED;
    }
    if (g_wifi_priv.adopted_current_network) {
        memset(&info, 0, sizeof(info));
        info.got_ip.ip_info = g_wifi_priv.adopted_ip_info;
        post_event(wifi, QM_WIFI_EVENT_STA_CONNECTED, NULL);
        post_event(wifi, QM_WIFI_EVENT_STA_GOT_IP, &info);
        return QM_EOK;
    }
    if (access("/usr/sbin/networksetup", X_OK) != 0) {
        return -QM_ENOSYS;
    }
    if (wifi->wifi_cfg.config.sta.ssid_len == 0 ||
        wifi->wifi_cfg.config.sta.ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN) {
        return -QM_ERR_WIFI_SSID;
    }
    memcpy(ssid, wifi->wifi_cfg.config.sta.ssid, wifi->wifi_cfg.config.sta.ssid_len);
    ssid[wifi->wifi_cfg.config.sta.ssid_len] = '\0';
    memcpy(password, wifi->wifi_cfg.config.sta.password,
           wifi->wifi_cfg.config.sta.password_len <= CONFIG_QM_WIFI_PASSWD_MAX_LEN ?
               wifi->wifi_cfg.config.sta.password_len : CONFIG_QM_WIFI_PASSWD_MAX_LEN);
    password[wifi->wifi_cfg.config.sta.password_len <= CONFIG_QM_WIFI_PASSWD_MAX_LEN ?
                 wifi->wifi_cfg.config.sta.password_len : CONFIG_QM_WIFI_PASSWD_MAX_LEN] = '\0';
    if (shell_quote(g_wifi_priv.interface_name, quoted_if, sizeof(quoted_if)) != QM_EOK ||
        shell_quote(ssid, quoted_ssid, sizeof(quoted_ssid)) != QM_EOK ||
        shell_quote(password, quoted_password, sizeof(quoted_password)) != QM_EOK) {
        return -QM_EFULL;
    }
    rc = snprintf(command, sizeof(command), "/usr/sbin/networksetup -setairportnetwork %s %s %s >/dev/null 2>&1",
                  quoted_if, quoted_ssid, quoted_password);
    if (rc < 0 || (size_t)rc >= sizeof(command) || system(command) != 0) {
        qm_wifi_event_info_t info;
        memset(&info, 0, sizeof(info));
        info.sta_disconnected.reason = QM_WIFI_REASON_CONNECTION_FAIL;
        post_event(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, &info);
        return -QM_EIO;
    }
    memset(&info, 0, sizeof(info));
    for (waited_ms = 0; waited_ms <= QM_MACOS_WIFI_IP_WAIT_MS; waited_ms += 200U) {
        if (get_ipv4_info(&info.got_ip.ip_info) == QM_EOK) {
            post_event(wifi, QM_WIFI_EVENT_STA_CONNECTED, NULL);
            post_event(wifi, QM_WIFI_EVENT_STA_GOT_IP, &info);
            return QM_EOK;
        }
        qm_msleep(200);
    }
    memset(&info, 0, sizeof(info));
    info.sta_disconnected.reason = QM_WIFI_REASON_CONNECTION_FAIL;
    post_event(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, &info);
    return -QM_ETIMEOUT;
}

static qm_err_t hal_wifi_disconnect(qm_wifi_dev_t *wifi)
{
    int ret;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }
    if (g_wifi_priv.adopted_current_network) {
        g_wifi_priv.adopted_current_network = 0;
        post_event(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, NULL);
        return QM_EOK;
    }
    ret = set_airport_power(0);
    if (ret != QM_EOK) {
        return ret;
    }
    post_event(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, NULL);
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    char quoted_if[64];
    char command[256];
    FILE *pipe;
    char line[512];
    qm_wifi_ap_record_t *records = NULL;
    uint8_t count = 0;
    (void)config;

    if (wifi == NULL || shell_quote(g_wifi_priv.interface_name, quoted_if, sizeof(quoted_if)) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (access(QM_MACOS_AIRPORT, X_OK) != 0) {
        return -QM_ENOSYS;
    }
    snprintf(command, sizeof(command), "%s -I %s -s 2>/dev/null", QM_MACOS_AIRPORT, quoted_if);
    pipe = popen(command, "r");
    if (pipe == NULL) {
        post_event(wifi, QM_WIFI_EVENT_SCAN_DONE, NULL);
        return -QM_EIO;
    }
    while (fgets(line, sizeof(line), pipe) != NULL && count < UINT8_MAX) {
        char bssid[32];
        char security[64];
        int rssi;
        int channel;
        char ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
        if (strstr(line, "BSSID") != NULL) {
            continue;
        }
        memset(ssid, 0, sizeof(ssid));
        if (sscanf(line, "%31s %d %d %63s %31[^\n]", bssid, &rssi, &channel, security, ssid) < 4) {
            continue;
        }
        records = (qm_wifi_ap_record_t *)realloc(records, sizeof(*records) * (count + 1u));
        if (records == NULL) {
            pclose(pipe);
            return -QM_ENOMEM;
        }
        memset(&records[count], 0, sizeof(records[count]));
        sscanf(bssid, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &records[count].bssid[0], &records[count].bssid[1], &records[count].bssid[2],
               &records[count].bssid[3], &records[count].bssid[4], &records[count].bssid[5]);
        records[count].rssi = (int8_t)rssi;
        records[count].channel = (uint8_t)channel;
        if (ssid[0] != '\0') {
            strncpy((char *)records[count].ssid, ssid, CONFIG_QM_WIFI_SSID_MAX_LEN);
            records[count].ssid_len = (uint8_t)strlen((char *)records[count].ssid);
        }
        records[count].authmode = strstr(security, "WPA3") != NULL ? QM_WIFI_AUTH_WPA3_PSK :
                                   strstr(security, "WPA") != NULL ? QM_WIFI_AUTH_WPA2_PSK : QM_WIFI_AUTH_OPEN;
        count++;
    }
    pclose(pipe);
    {
        qm_wifi_event_info_t info;
        memset(&info, 0, sizeof(info));
        info.scan_done.status = 0;
        info.scan_done.ap_num = count;
        info.scan_done.ap_record = records;
        post_event(wifi, QM_WIFI_EVENT_SCAN_DONE, &info);
    }
    free(records);
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi) { (void)wifi; return QM_EOK; }
static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6]) { (void)wifi; (void)mac; return -QM_ENOSYS; }
static qm_err_t hal_wifi_set_ps(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type) { (void)wifi; (void)type; return QM_EOK; }
static qm_err_t hal_wifi_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6]) { (void)wifi; (void)ifx; (void)mac; return -QM_ENOSYS; }

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    char command[160];
    char value[64];
    unsigned int bytes[6];
    FILE *pipe;
    (void)ifx;
    if (wifi == NULL || mac == NULL) {
        return -QM_EINVAL;
    }
    snprintf(command, sizeof(command), "/sbin/ifconfig '%s' 2>/dev/null", g_wifi_priv.interface_name);
    pipe = popen(command, "r");
    if (pipe == NULL) {
        return -QM_EIO;
    }
    while (fgets(value, sizeof(value), pipe) != NULL) {
        if (sscanf(value, "\tether %x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2],
                   &bytes[3], &bytes[4], &bytes[5]) == 6) {
            int i;
            for (i = 0; i < 6; i++) mac[i] = (uint8_t)bytes[i];
            pclose(pipe);
            return QM_EOK;
        }
    }
    pclose(pipe);
    return -QM_EIO;
}

static qm_err_t hal_wifi_get_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *info)
{
    int corewlan_ret;

    if (wifi == NULL || info == NULL || !g_wifi_priv.started) {
        return -QM_EINVAL;
    }
    if (g_wifi_priv.adopted_current_network) {
        *info = g_wifi_priv.adopted_ap_info;
        return QM_EOK;
    }
    memset(info, 0, sizeof(*info));
    corewlan_ret = qm_macos_wifi_get_ap_info(g_wifi_priv.interface_name, info);
    if (corewlan_ret != QM_EOK) {
        (void)current_ap_info_from_airport(info);
    }

    if (info->ssid[0] == '\0' && current_ssid_from_networksetup(info) != QM_EOK) {
        size_t len = wifi->wifi_cfg.config.sta.ssid_len;
        if (len == 0U || len > CONFIG_QM_WIFI_SSID_MAX_LEN) {
            return corewlan_ret != QM_EOK ? corewlan_ret : -QM_ENOENT;
        }
        memcpy(info->ssid, wifi->wifi_cfg.config.sta.ssid, len);
        info->ssid[len] = '\0';
        info->ssid_len = (uint8_t)len;
    }

    info->authmode = wifi->wifi_cfg.config.sta.password_len == 0U ?
                         QM_WIFI_AUTH_OPEN : QM_WIFI_AUTH_WPA2_PSK;
    return QM_EOK;
}

static qm_err_t hal_wifi_get_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    qm_wifi_ap_record_t info;
    int ret;

    if (rssi == NULL) {
        return -QM_EINVAL;
    }
    ret = hal_wifi_get_ap_info(wifi, &info);
    if (ret != QM_EOK) {
        return ret;
    }
    *rssi = info.rssi;
    return QM_EOK;
}

static qm_wifi_dev_ops_t g_wifi_ops = {
    .wifi_init = hal_wifi_init,
    .wifi_deinit = hal_wifi_deinit,
    .wifi_start = hal_wifi_start,
    .wifi_stop = hal_wifi_stop,
    .wifi_connect = hal_wifi_connect,
    .wifi_disconnect = hal_wifi_disconnect,
    .wifi_scan_start = hal_wifi_scan_start,
    .wifi_scan_stop = hal_wifi_scan_stop,
    .wifi_deauth = hal_wifi_deauth,
    .wifi_set_ps = hal_wifi_set_ps,
    .wifi_set_mac = hal_wifi_set_mac,
    .wifi_get_mac = hal_wifi_get_mac,
    .wifi_get_sta_rssi = hal_wifi_get_rssi,
    .wifi_get_sta_ap_info = hal_wifi_get_ap_info,
};

int qm_hal_wifi_init(void)
{
    return qm_wifi_dev_register(&g_wifi_ops, &g_wifi_priv);
}
