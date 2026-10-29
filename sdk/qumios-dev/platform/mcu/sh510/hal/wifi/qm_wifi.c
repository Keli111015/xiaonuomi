#include "qm.h"
#include "qm_string.h"
#include "qm_wifi_dev.h"
#include "qm_wifi_mgnt.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_linux_stub.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/wireless.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LOG_TAG "linux wifi"

#ifndef QM_LINUX_WIFI_STA_IF
#define QM_LINUX_WIFI_STA_IF ""
#endif

#ifndef QM_LINUX_WPA_CLI
#define QM_LINUX_WPA_CLI "/app/bin/wpa_cli"
#endif

#ifndef QM_LINUX_WPA_SUPPLICANT
#define QM_LINUX_WPA_SUPPLICANT "/app/bin/wpa_supplicant"
#endif

#ifndef QM_LINUX_WPA_CONF
#define QM_LINUX_WPA_CONF "/data/wifi/wpa_supplicant.conf"
#endif

#ifndef QM_LINUX_WPA_CONF_DIR
#define QM_LINUX_WPA_CONF_DIR "/data/wifi"
#endif

#ifndef QM_LINUX_WPA_CTRL_DIR
#define QM_LINUX_WPA_CTRL_DIR "/var/run/wpa_supplicant"
#endif

#define LINUX_WIFI_LINK_POLL_SEC 1
#define LINUX_WIFI_DHCP_RETRY_POLL 3

typedef struct {
    char sta_if[IFNAMSIZ];
    char ap_if[IFNAMSIZ];
    int started;
    pthread_t scan_thread;
    int scan_running;
    int scan_abort;
    pthread_t link_thread;
    int link_running;
    int link_stop;
    int assoc;
    int had_ip;
    int dhcp_skip;
    qm_wifi_dev_t *wifi;
} linux_wifi_priv_t;

static linux_wifi_priv_t s_wifi_priv;

static int linux_if_is_running(const char *ifname);
static const char *linux_wifi_if_for(qm_wifi_interface_t ifx);
static void linux_wifi_link_monitor_start(qm_wifi_dev_t *wifi);
static void linux_wifi_link_monitor_stop(void);
static int linux_if_get_ipv4(const char *ifname, qm_ip4_addr_t *ip);
static int linux_if_has_default_route(const char *ifname);
static void linux_if_run_dhcp(const char *ifname);
static void linux_wpa_write_skeleton(void);

static int linux_wpa_cli_exec(const char *ifname, const char *cmd, char *out, size_t out_len)
{
    char full_cmd[768];
    FILE *fp;
    size_t total = 0;
    static const char *cli_paths[] = { QM_LINUX_WPA_CLI, "/usr/bin/wpa_cli", "wpa_cli", NULL };
    size_t i;

    if (ifname == NULL || cmd == NULL) {
        return -1;
    }

    for (i = 0; cli_paths[i] != NULL; i++) {
        snprintf(full_cmd, sizeof(full_cmd), "%s -i %s %s 2>/dev/null",
                 cli_paths[i], ifname, cmd);
        fp = popen(full_cmd, "r");
        if (fp == NULL) {
            continue;
        }

        if (out != NULL && out_len > 0) {
            out[0] = '\0';
            while (fgets(out + total, (int)(out_len - total), fp) != NULL) {
                total = strlen(out);
                if (total + 1 >= out_len) {
                    break;
                }
            }
        } else {
            char line[256];
            while (fgets(line, sizeof(line), fp) != NULL) {
            }
        }

        if (pclose(fp) == -1) {
            continue;
        }
        return 0;
    }

    return -1;
}

static qm_wifi_auth_mode_t linux_wifi_parse_auth(const char *flags)
{
    if (flags == NULL || flags[0] == '\0') {
        return QM_WIFI_AUTH_OPEN;
    }
    if (strstr(flags, "WPA3") != NULL) {
        return QM_WIFI_AUTH_WPA3_PSK;
    }
    if (strstr(flags, "WPA2") != NULL && strstr(flags, "WPA") != NULL) {
        return QM_WIFI_AUTH_WPA_WPA2_PSK;
    }
    if (strstr(flags, "WPA2") != NULL) {
        return QM_WIFI_AUTH_WPA2_PSK;
    }
    if (strstr(flags, "WPA") != NULL) {
        return QM_WIFI_AUTH_WPA_PSK;
    }
    if (strstr(flags, "WEP") != NULL) {
        return QM_WIFI_AUTH_WEP;
    }
    return QM_WIFI_AUTH_OPEN;
}

static uint8_t linux_wifi_freq_to_channel(int freq)
{
    if (freq >= 2412 && freq <= 2484) {
        return (uint8_t)((freq - 2407) / 5);
    }
    if (freq >= 5000 && freq <= 5900) {
        return (uint8_t)((freq - 5000) / 5);
    }
    return 0;
}

static int linux_wifi_parse_scan_results(const char *text, qm_wifi_ap_record_t **records, uint8_t *ap_num)
{
    char *copy;
    char *line;
    char *saveptr;
    qm_wifi_ap_record_t *list = NULL;
    uint8_t count = 0;
    uint8_t cap = 0;

    if (text == NULL || records == NULL || ap_num == NULL) {
        return -1;
    }

    copy = qm_strdup(text);
    if (copy == NULL) {
        return -1;
    }

    line = strtok_r(copy, "\n", &saveptr);
    while (line != NULL) {
        char bssid[32];
        char flags[128];
        char ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
        int freq = 0;
        int signal = 0;
        int matched;

        matched = sscanf(line, " %31s %d %d %127s %32[^\n]",
                         bssid, &freq, &signal, flags, ssid);
        if (matched >= 4) {
            if (count >= cap) {
                uint8_t new_cap = (cap == 0) ? 8 : (uint8_t)(cap + 8);
                qm_wifi_ap_record_t *tmp;

                tmp = (qm_wifi_ap_record_t *)qm_realloc(list, sizeof(qm_wifi_ap_record_t) * new_cap);
                if (tmp == NULL) {
                    break;
                }
                list = tmp;
                cap = new_cap;
            }

            memset(&list[count], 0, sizeof(list[count]));
            sscanf(bssid, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &list[count].bssid[0], &list[count].bssid[1], &list[count].bssid[2],
                   &list[count].bssid[3], &list[count].bssid[4], &list[count].bssid[5]);
            list[count].rssi = (int8_t)signal;
            list[count].channel = linux_wifi_freq_to_channel(freq);
            list[count].authmode = linux_wifi_parse_auth(flags);
            if (matched >= 5 && ssid[0] != '\0') {
                strncpy((char *)list[count].ssid, ssid, CONFIG_QM_WIFI_SSID_MAX_LEN);
                list[count].ssid_len = (uint8_t)strlen((char *)list[count].ssid);
            }
            count++;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    qm_free(copy);
    *records = list;
    *ap_num = count;
    return 0;
}

static void linux_wifi_post_scan_done(qm_wifi_dev_t *wifi, int status, qm_wifi_ap_record_t *records, uint8_t ap_num)
{
    qm_wifi_event_info_t event_info;

    if (wifi == NULL || wifi->event_handler == NULL) {
        qm_free(records);
        return;
    }

    memset(&event_info, 0, sizeof(event_info));
    event_info.scan_done.status = status;
    event_info.scan_done.ap_num = ap_num;
    event_info.scan_done.ap_record = records;
    wifi->event_handler(QM_WIFI_EVENT_SCAN_DONE, &event_info, wifi->arg);
    qm_free(records);
}

static void *linux_wifi_scan_thread(void *arg)
{
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t *)arg;
    const char *ifname;
    char output[8192];

    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    if (linux_wpa_cli_exec(ifname, "scan", NULL, 0) != 0) {
        linux_wifi_post_scan_done(wifi, 1, NULL, 0);
        s_wifi_priv.scan_running = 0;
        return NULL;
    }

    sleep(2);
    if (s_wifi_priv.scan_abort) {
        s_wifi_priv.scan_running = 0;
        return NULL;
    }

    if (linux_wpa_cli_exec(ifname, "scan_results", output, sizeof(output)) != 0) {
        linux_wifi_post_scan_done(wifi, 1, NULL, 0);
        s_wifi_priv.scan_running = 0;
        return NULL;
    }

    {
        qm_wifi_ap_record_t *records = NULL;
        uint8_t ap_num = 0;

        linux_wifi_parse_scan_results(output, &records, &ap_num);
        linux_wifi_post_scan_done(wifi, 0, records, ap_num);
    }

    s_wifi_priv.scan_running = 0;
    return NULL;
}

static void linux_wifi_post(qm_wifi_dev_t *wifi, qm_wifi_event_t event,
                            qm_wifi_event_info_t *info)
{
    if (wifi == NULL || wifi->event_handler == NULL) {
        return;
    }
    wifi->event_handler(event, info, wifi->arg);
}

/* Field must be at start-of-line so "ssid=" is not matched inside "bssid=". */
static const char *linux_wpa_field(const char *text, const char *key)
{
    size_t klen;
    const char *p;

    if (text == NULL || key == NULL) {
        return NULL;
    }
    klen = strlen(key);
    if (strncmp(text, key, klen) == 0) {
        return text + klen;
    }
    p = text;
    while ((p = strstr(p, key)) != NULL) {
        if (p > text && *(p - 1) == '\n') {
            return p + klen;
        }
        p += klen;
    }
    return NULL;
}

static void linux_wpa_copy_field(char *dst, size_t dst_len, const char *val)
{
    size_t n = 0;

    if (dst == NULL || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (val == NULL) {
        return;
    }
    while (val[n] != '\0' && val[n] != '\n' && val[n] != '\r' && n + 1 < dst_len) {
        dst[n] = val[n];
        n++;
    }
    dst[n] = '\0';
}

static int linux_wifi_parse_status(const char *text, int *assoc, qm_ip4_addr_t *ip)
{
    const char *p;
    unsigned a, b, c, d;

    if (assoc) {
        *assoc = 0;
    }
    if (ip) {
        ip->addr = 0;
    }
    if (text == NULL) {
        return -1;
    }

    if (assoc && strstr(text, "wpa_state=COMPLETED") != NULL) {
        *assoc = 1;
    }

    p = linux_wpa_field(text, "ip_address=");
    if (p != NULL && ip != NULL &&
        sscanf(p, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
        a <= 255U && b <= 255U && c <= 255U && d <= 255U) {
        uint32_t packed = (a << 24) | (b << 16) | (c << 8) | d;
        ip->addr = htonl(packed);
    }

    return 0;
}

typedef struct {
    int assoc;
    qm_ip4_addr_t ip;
    char ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
    uint8_t ssid_len;
    uint8_t bssid[6];
} linux_wpa_link_t;

static int linux_wpa_read_link(const char *ifname, linux_wpa_link_t *link)
{
    char status[1024];
    const char *p;
    unsigned b[6];
    int i;

    if (link == NULL) {
        return -1;
    }
    memset(link, 0, sizeof(*link));
    if (ifname == NULL || ifname[0] == '\0') {
        return -1;
    }
    if (linux_wpa_cli_exec(ifname, "status", status, sizeof(status)) != 0) {
        return -1;
    }
    linux_wifi_parse_status(status, &link->assoc, &link->ip);
    p = linux_wpa_field(status, "ssid=");
    linux_wpa_copy_field(link->ssid, sizeof(link->ssid), p);
    link->ssid_len = (uint8_t)strlen(link->ssid);
    p = linux_wpa_field(status, "bssid=");
    if (p != NULL &&
        sscanf(p, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (i = 0; i < 6; i++) {
            link->bssid[i] = (uint8_t)b[i];
        }
    }
    return 0;
}

static void linux_wpa_strip_ssid_token(char *s)
{
    char *src;
    char *dst;
    size_t n;

    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' ||
                      s[n - 1] == '"')) {
        s[--n] = '\0';
    }
    src = s;
    while (*src == '"' || *src == ' ') {
        src++;
    }
    if (src != s) {
        dst = s;
        while (*src != '\0') {
            *dst++ = *src++;
        }
        *dst = '\0';
    }
}

/* Return existing wpa network id for ssid, or -1. */
static int linux_wpa_find_net_id(const char *ifname, const char *ssid)
{
    char cmd[64];
    char out[128];
    int id;

    if (ifname == NULL || ssid == NULL || ssid[0] == '\0') {
        return -1;
    }

    for (id = 0; id < 16; id++) {
        snprintf(cmd, sizeof(cmd), "get_network %d ssid", id);
        if (linux_wpa_cli_exec(ifname, cmd, out, sizeof(out)) != 0) {
            continue;
        }
        if (strncmp(out, "FAIL", 4) == 0) {
            continue;
        }
        linux_wpa_strip_ssid_token(out);
        if (strcmp(out, ssid) == 0) {
            return id;
        }
    }
    return -1;
}

static void linux_wifi_link_apply(qm_wifi_dev_t *wifi, int assoc, int has_ip,
                                  const qm_ip4_addr_t *ip)
{
    linux_wifi_priv_t *priv = &s_wifi_priv;
    qm_wifi_event_info_t info;

    if (!assoc && priv->assoc) {
        memset(&info, 0, sizeof(info));
        info.sta_disconnected.reason = QM_WIFI_REASON_CONNECTION_FAIL;
        QM_LOGW(LOG_TAG, "STA_DISCONNECTED");
        linux_wifi_post(wifi, QM_WIFI_EVENT_STA_DISCONNECTED, &info);
        priv->assoc = 0;
        priv->had_ip = 0;
        return;
    }

    if (assoc && !priv->assoc) {
        QM_LOGI(LOG_TAG, "STA_CONNECTED");
        linux_wifi_post(wifi, QM_WIFI_EVENT_STA_CONNECTED, NULL);
        priv->assoc = 1;
    }

    if (assoc && has_ip && !priv->had_ip) {
        memset(&info, 0, sizeof(info));
        if (ip != NULL) {
            info.got_ip.ip_info.ip = *ip;
        }
        QM_LOGI(LOG_TAG, "STA_GOT_IP " QM_IPSTR,
                QM_IP2STR(&info.got_ip.ip_info.ip));
        linux_wifi_post(wifi, QM_WIFI_EVENT_STA_GOT_IP, &info);
        priv->had_ip = 1;
    } else if (assoc && !has_ip && priv->had_ip) {
        QM_LOGW(LOG_TAG, "STA_LOST_IP");
        linux_wifi_post(wifi, QM_WIFI_EVENT_STA_LOST_IP, NULL);
        priv->had_ip = 0;
    }
}

static void *linux_wifi_link_thread(void *arg)
{
    qm_wifi_dev_t *wifi = (qm_wifi_dev_t *)arg;
    const char *ifname;
    linux_wpa_link_t link;
    int assoc;
    qm_ip4_addr_t ip;
    int has_ip;
    int has_gw;
    int has_l3;

    QM_LOGI(LOG_TAG, "link monitor start");
    while (!s_wifi_priv.link_stop) {
        ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
        assoc = 0;
        has_ip = 0;
        has_gw = 0;
        has_l3 = 0;
        ip.addr = 0;

        if (ifname[0] != '\0' &&
            linux_wpa_read_link(ifname, &link) == 0) {
            assoc = link.assoc;
        }

        if (assoc && ifname[0] != '\0') {
            has_ip = linux_if_get_ipv4(ifname, &ip);
            has_gw = linux_if_has_default_route(ifname);
            if (!has_ip || !has_gw) {
                if (s_wifi_priv.dhcp_skip <= 0) {
                    QM_LOGI(LOG_TAG, "assoc without L3, dhcp if=%s ip=%d gw=%d",
                            ifname, has_ip, has_gw);
                    linux_if_run_dhcp(ifname);
                    s_wifi_priv.dhcp_skip = LINUX_WIFI_DHCP_RETRY_POLL;
                    has_ip = linux_if_get_ipv4(ifname, &ip);
                    has_gw = linux_if_has_default_route(ifname);
                } else {
                    s_wifi_priv.dhcp_skip--;
                }
            } else {
                s_wifi_priv.dhcp_skip = 0;
            }
            has_l3 = (has_ip && has_gw);
        } else {
            s_wifi_priv.dhcp_skip = 0;
        }

        linux_wifi_link_apply(wifi, assoc, has_l3, &ip);
        sleep(LINUX_WIFI_LINK_POLL_SEC);
    }

    s_wifi_priv.link_running = 0;
    QM_LOGI(LOG_TAG, "link monitor stop");
    return NULL;
}

static void linux_wifi_link_monitor_start(qm_wifi_dev_t *wifi)
{
    if (s_wifi_priv.link_running) {
        s_wifi_priv.wifi = wifi;
        return;
    }

    s_wifi_priv.link_stop = 0;
    s_wifi_priv.assoc = 0;
    s_wifi_priv.had_ip = 0;
    s_wifi_priv.dhcp_skip = 0;
    s_wifi_priv.wifi = wifi;
    s_wifi_priv.link_running = 1;
    if (pthread_create(&s_wifi_priv.link_thread, NULL,
                       linux_wifi_link_thread, wifi) != 0) {
        s_wifi_priv.link_running = 0;
        QM_LOGW(LOG_TAG, "link monitor create failed");
        return;
    }
}

static void linux_wifi_link_monitor_stop(void)
{
    if (!s_wifi_priv.link_running) {
        return;
    }
    s_wifi_priv.link_stop = 1;
    pthread_join(s_wifi_priv.link_thread, NULL);
    s_wifi_priv.link_running = 0;
}

static int linux_if_exists(const char *ifname)
{
    int fd;
    struct ifreq ifr;

    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

static int linux_if_is_running(const char *ifname)
{
    int fd;
    struct ifreq ifr;

    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
}

/* Keep iface UP: RTL8723DS coexistence corrupts BLE if wlan0 is down. */
static void linux_if_set_up(const char *ifname)
{
    int fd;
    struct ifreq ifr;

    if (ifname == NULL || ifname[0] == '\0') {
        return;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags |= IFF_UP;
        (void)ioctl(fd, SIOCSIFFLAGS, &ifr);
    }
    close(fd);
}

static void linux_if_flush_addr(const char *ifname)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_in *sin;

    if (ifname == NULL || ifname[0] == '\0') {
        return;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = INADDR_ANY;
    (void)ioctl(fd, SIOCSIFADDR, &ifr);
    close(fd);

    linux_if_set_up(ifname);
}

static int linux_if_get_ipv4(const char *ifname, qm_ip4_addr_t *ip)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_in *sin;

    if (ip != NULL) {
        ip->addr = 0;
    }
    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) != 0) {
        close(fd);
        return 0;
    }
    close(fd);

    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    if (sin->sin_addr.s_addr == 0 || sin->sin_addr.s_addr == INADDR_NONE) {
        return 0;
    }
    if (ip != NULL) {
        ip->addr = sin->sin_addr.s_addr;
    }
    return 1;
}

static int linux_if_has_default_route(const char *ifname)
{
    FILE *fp;
    char line[160];
    char iface[32];
    unsigned int dest;

    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }

    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%31s %x", iface, &dest) != 2) {
            continue;
        }
        if (strcmp(iface, ifname) == 0 && dest == 0U) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static void linux_if_run_dhcp(const char *ifname)
{
    char cmd[160];

    if (ifname == NULL || ifname[0] == '\0') {
        return;
    }

    snprintf(cmd, sizeof(cmd),
             "udhcpc -i %s -n -q -t 5 -A 1 >/dev/null 2>&1", ifname);
    (void)system(cmd);
}

static void linux_wpa_write_skeleton(void)
{
    FILE *fp;

    mkdir(QM_LINUX_WPA_CONF_DIR, 0755);
    fp = fopen(QM_LINUX_WPA_CONF, "w");
    if (fp == NULL) {
        QM_LOGW(LOG_TAG, "write %s failed errno=%d", QM_LINUX_WPA_CONF, errno);
        return;
    }
    fprintf(fp,
            "ctrl_interface=%s\n"
            "update_config=1\n",
            QM_LINUX_WPA_CTRL_DIR);
    fclose(fp);
}

static void linux_wpa_clear_saved(const char *ifname)
{
    int id;
    char cmd[64];

    if (ifname != NULL && ifname[0] != '\0') {
        for (id = 0; id < 16; id++) {
            snprintf(cmd, sizeof(cmd), "remove_network %d", id);
            (void)linux_wpa_cli_exec(ifname, cmd, NULL, 0);
        }
        (void)linux_wpa_cli_exec(ifname, "save_config", NULL, 0);
    }
    linux_wpa_write_skeleton();
}

static int linux_wpa_ctrl_exists(const char *ifname)
{
    char path[160];

    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s", QM_LINUX_WPA_CTRL_DIR, ifname);
    return access(path, F_OK) == 0;
}

/* Start wpa only if ctrl socket is missing. Never kill wifiNetd/wpa:
 * manager treats wifiNetd death as a required-app crash and reboots. */
static void linux_wpa_ensure(const char *ifname)
{
    char cmd[320];
    int i;

    if (ifname == NULL || ifname[0] == '\0') {
        return;
    }

    mkdir(QM_LINUX_WPA_CTRL_DIR, 0755);
    mkdir(QM_LINUX_WPA_CONF_DIR, 0755);
    linux_if_set_up(ifname);

    if (access(QM_LINUX_WPA_CONF, F_OK) != 0) {
        linux_wpa_write_skeleton();
    }

    if (linux_wpa_ctrl_exists(ifname)) {
        return;
    }

    snprintf(cmd, sizeof(cmd),
             "%s -Dnl80211 -i%s -c%s -B >/dev/null 2>&1",
             QM_LINUX_WPA_SUPPLICANT, ifname, QM_LINUX_WPA_CONF);
    if (system(cmd) != 0) {
        QM_LOGW(LOG_TAG, "wpa_supplicant start failed if=%s", ifname);
    } else {
        QM_LOGI(LOG_TAG, "wpa_supplicant started if=%s", ifname);
    }

    for (i = 0; i < 20; i++) {
        if (linux_wpa_ctrl_exists(ifname)) {
            return;
        }
        usleep(100000);
    }
    QM_LOGW(LOG_TAG, "wpa ctrl not ready if=%s", ifname);
}

static void linux_wifi_pick_ifname(char *dst, size_t dst_len, const char **candidates)
{
    size_t i;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    dst[0] = '\0';
    for (i = 0; candidates[i] != NULL; i++) {
        if (linux_if_exists(candidates[i])) {
            strncpy(dst, candidates[i], dst_len - 1);
            dst[dst_len - 1] = '\0';
            return;
        }
    }
}

static void linux_wifi_detect_ifaces(linux_wifi_priv_t *priv)
{
    static const char *sta_candidates[] = {
        QM_LINUX_WIFI_STA_IF,
        "wlan0", "wlan1", "wlp2s0", "wlp3s0", "mlan0", "eth0", NULL
    };
    static const char *ap_candidates[] = {
        "ap0", "wlan0", "wlan1", NULL
    };

    if (priv == NULL) {
        return;
    }

    if (priv->sta_if[0] != '\0') {
        return;
    }

    if (QM_LINUX_WIFI_STA_IF[0] != '\0') {
        strncpy(priv->sta_if, QM_LINUX_WIFI_STA_IF, sizeof(priv->sta_if) - 1);
    } else {
        linux_wifi_pick_ifname(priv->sta_if, sizeof(priv->sta_if), sta_candidates);
    }

    linux_wifi_pick_ifname(priv->ap_if, sizeof(priv->ap_if), ap_candidates);
    if (priv->ap_if[0] == '\0') {
        strncpy(priv->ap_if, priv->sta_if, sizeof(priv->ap_if) - 1);
    }
}

static const char *linux_wifi_if_for(qm_wifi_interface_t ifx)
{
    linux_wifi_detect_ifaces(&s_wifi_priv);
    if (ifx == QM_WIFI_IF_AP) {
        return s_wifi_priv.ap_if;
    }
    return s_wifi_priv.sta_if;
}

static qm_err_t linux_wifi_get_mac_from_if(const char *ifname, uint8_t mac[6])
{
    int fd;
    struct ifreq ifr;

    if (ifname == NULL || ifname[0] == '\0' || mac == NULL) {
        return -QM_EINVAL;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -QM_EIO;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        close(fd);
        QM_LOGW(LOG_TAG, "get mac failed if=%s errno=%d", ifname, errno);
        return -QM_EIO;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(fd);
    return QM_EOK;
}

static int linux_wifi_read_rssi(const char *ifname, int8_t *rssi)
{
    FILE *fp;
    char line[256];
    char name[IFNAMSIZ];

    if (ifname == NULL || rssi == NULL) {
        return -1;
    }

    fp = fopen("/proc/net/wireless", "r");
    if (fp == NULL) {
        return -1;
    }

    if (fgets(line, sizeof(line), fp) == NULL ||
        fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        int status;
        int level;

        if (sscanf(line, " %15[^:]: %d %*d %d", name, &status, &level) != 3) {
            continue;
        }

        if (strcmp(name, ifname) == 0) {
            *rssi = (int8_t)level;
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

static qm_err_t linux_wifi_read_wireless_ap_info(const char *ifname, qm_wifi_ap_record_t *ap_info)
{
    int fd;
    struct iwreq wrq;
    char essid[IW_ESSID_MAX_SIZE + 1];
    uint8_t bssid[6];

    if (ifname == NULL || ap_info == NULL) {
        return -QM_EINVAL;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -QM_EIO;
    }

    memset(ap_info, 0, sizeof(*ap_info));
    memset(essid, 0, sizeof(essid));
    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ - 1);
    wrq.u.essid.pointer = essid;
    wrq.u.essid.length = sizeof(essid);
    wrq.u.essid.flags = 0;
    if (ioctl(fd, SIOCGIWESSID, &wrq) == 0 && essid[0] != '\0') {
        ap_info->ssid_len = (uint8_t)strlen(essid);
        if (ap_info->ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN) {
            ap_info->ssid_len = CONFIG_QM_WIFI_SSID_MAX_LEN;
        }
        memcpy(ap_info->ssid, essid, ap_info->ssid_len);
    }

    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIWAP, &wrq) == 0) {
        memcpy(bssid, wrq.u.ap_addr.sa_data, sizeof(bssid));
        memcpy(ap_info->bssid, bssid, sizeof(ap_info->bssid));
    }

    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIWFREQ, &wrq) == 0 && wrq.u.freq.m > 0) {
        ap_info->channel = (uint8_t)((wrq.u.freq.m / 100000) - 2407) / 5;
    }

    close(fd);

    if (linux_wifi_read_rssi(ifname, &ap_info->rssi) != 0) {
        ap_info->rssi = 0;
    }

    if (ap_info->ssid_len == 0 && ap_info->bssid[0] == 0 && ap_info->bssid[1] == 0 &&
        ap_info->bssid[2] == 0 && ap_info->bssid[3] == 0 && ap_info->bssid[4] == 0 &&
        ap_info->bssid[5] == 0) {
        return -QM_EIO;
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_init(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    linux_wifi_detect_ifaces(&s_wifi_priv);
    if (s_wifi_priv.sta_if[0] == '\0') {
        QM_LOGW(LOG_TAG, "no network interface found");
        return -QM_EIO;
    }

    QM_LOGI(LOG_TAG, "use sta_if=%s ap_if=%s", s_wifi_priv.sta_if, s_wifi_priv.ap_if);
    linux_wifi_link_monitor_start(wifi);
    return QM_EOK;
}

static qm_err_t hal_wifi_deinit(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    linux_wifi_link_monitor_stop();
    s_wifi_priv.started = 0;
    return QM_EOK;
}

static qm_err_t hal_wifi_start(qm_wifi_dev_t *wifi)
{
    const char *ifname;

    (void)wifi;
    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    linux_if_set_up(ifname);
    linux_wpa_ensure(ifname);
    s_wifi_priv.started = 1;
    return QM_EOK;
}

static qm_err_t hal_wifi_stop(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    s_wifi_priv.started = 0;
    return QM_EOK;
}

static qm_err_t linux_wpa_select_network(const char *ifname, int net_id)
{
    char cmd[64];

    snprintf(cmd, sizeof(cmd), "enable_network %d", net_id);
    linux_wpa_cli_exec(ifname, cmd, NULL, 0);
    snprintf(cmd, sizeof(cmd), "select_network %d", net_id);
    linux_wpa_cli_exec(ifname, cmd, NULL, 0);
    linux_wpa_cli_exec(ifname, "reassociate", NULL, 0);
    return QM_EOK;
}

static qm_err_t hal_wifi_connect(qm_wifi_dev_t *wifi)
{
    const char *ifname;
    qm_sta_config_t *sta;
    linux_wpa_link_t link;
    char ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];
    char cmd[512];
    char output[64];
    int net_id = -1;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    sta = &wifi->wifi_cfg.config.sta;
    if (sta->ssid_len == 0 && sta->ssid[0] == '\0') {
        return -QM_ERR_WIFI_SSID;
    }

    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    if (ifname[0] == '\0') {
        return -QM_EIO;
    }

    linux_wpa_ensure(ifname);

    memset(ssid, 0, sizeof(ssid));
    memcpy(ssid, sta->ssid, sta->ssid_len);

    /* Already on this AP: do not add_network (that tears down RTL assoc). */
    if (linux_wpa_read_link(ifname, &link) == 0 && link.assoc &&
        link.ssid[0] != '\0' && strcmp(link.ssid, ssid) == 0) {
        QM_LOGI(LOG_TAG, "already associated ssid=%s", ssid);
        return QM_EOK;
    }

    net_id = linux_wpa_find_net_id(ifname, ssid);
    if (net_id >= 0) {
        QM_LOGI(LOG_TAG, "reassociate ssid=%s net_id=%d", ssid, net_id);
        return linux_wpa_select_network(ifname, net_id);
    }

    if (linux_wpa_cli_exec(ifname, "add_network", output, sizeof(output)) != 0) {
        QM_LOGW(LOG_TAG, "wpa_cli add_network failed");
        return -QM_EIO;
    }

    net_id = atoi(output);
    if (net_id < 0) {
        return -QM_EIO;
    }

    snprintf(cmd, sizeof(cmd), "set_network %d ssid '\"%s\"'", net_id, ssid);
    if (linux_wpa_cli_exec(ifname, cmd, NULL, 0) != 0) {
        return -QM_EIO;
    }

    if (sta->password_len > 0 || sta->password[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "set_network %d psk '\"%s\"'", net_id, sta->password);
        linux_wpa_cli_exec(ifname, cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt WPA-PSK", net_id);
    } else {
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt NONE", net_id);
    }
    linux_wpa_cli_exec(ifname, cmd, NULL, 0);

    linux_wpa_cli_exec(ifname, "save_config", NULL, 0);
    QM_LOGI(LOG_TAG, "connect ssid=%s net_id=%d", ssid, net_id);
    return linux_wpa_select_network(ifname, net_id);
}

static qm_err_t hal_wifi_disconnet(qm_wifi_dev_t *wifi)
{
    const char *ifname;

    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    if (ifname[0] == '\0') {
        return -QM_EIO;
    }

    /* Disassociate but keep iface UP. Do not kill wifiNetd/wpa. */
    (void)linux_wpa_cli_exec(ifname, "disconnect", NULL, 0);
    linux_if_flush_addr(ifname);

    /* qm_wifi_clear_config zeros sta ssid first; drop wpa networks too. */
    if (wifi != NULL &&
        wifi->wifi_cfg.config.sta.ssid_len == 0 &&
        wifi->wifi_cfg.config.sta.ssid[0] == '\0') {
        QM_LOGI(LOG_TAG, "empty sta cfg, clear wpa networks");
        linux_wpa_clear_saved(ifname);
    }
    return QM_EOK;
}

static qm_err_t hal_wifi_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    (void)config;

    if (wifi == NULL) {
        return -QM_EINVAL;
    }

    if (s_wifi_priv.scan_running) {
        return -QM_EBUSY;
    }

    s_wifi_priv.scan_abort = 0;
    s_wifi_priv.scan_running = 1;
    if (pthread_create(&s_wifi_priv.scan_thread, NULL, linux_wifi_scan_thread, wifi) != 0) {
        s_wifi_priv.scan_running = 0;
        return -QM_EIO;
    }
    pthread_detach(s_wifi_priv.scan_thread);

    return QM_EOK;
}

static qm_err_t hal_wifi_scan_stop(qm_wifi_dev_t *wifi)
{
    (void)wifi;
    s_wifi_priv.scan_abort = 1;
    return QM_EOK;
}

static qm_err_t hal_wifi_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{
    (void)wifi;
    (void)mac;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
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
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

static qm_err_t hal_wifi_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    const char *ifname;

    (void)wifi;

    if (mac == NULL) {
        return -QM_EINVAL;
    }

    ifname = linux_wifi_if_for(ifx);
    return linux_wifi_get_mac_from_if(ifname, mac);
}

static qm_err_t hal_wifi_get_sta_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    const char *ifname;

    (void)wifi;

    if (rssi == NULL) {
        return -QM_EINVAL;
    }

    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    if (!linux_if_is_running(ifname)) {
        return -QM_EIO;
    }

    if (linux_wifi_read_rssi(ifname, rssi) != 0) {
        *rssi = 0;
        return -QM_EIO;
    }

    return QM_EOK;
}

static qm_err_t hal_wifi_get_sta_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    const char *ifname;
    linux_wpa_link_t link;
    qm_err_t ret;

    (void)wifi;

    if (ap_info == NULL) {
        return -QM_EINVAL;
    }

    ifname = linux_wifi_if_for(QM_WIFI_IF_STA);
    memset(ap_info, 0, sizeof(*ap_info));

    /* RTL: IFF_RUNNING / SIOCGIWESSID miss COMPLETED. Use wpa_cli status. */
    if (linux_wpa_read_link(ifname, &link) == 0 && link.assoc &&
        link.ssid_len > 0) {
        ap_info->ssid_len = link.ssid_len;
        if (ap_info->ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN) {
            ap_info->ssid_len = CONFIG_QM_WIFI_SSID_MAX_LEN;
        }
        memcpy(ap_info->ssid, link.ssid, ap_info->ssid_len);
        memcpy(ap_info->bssid, link.bssid, sizeof(ap_info->bssid));
        if (linux_if_is_running(ifname)) {
            (void)linux_wifi_read_rssi(ifname, &ap_info->rssi);
        }
        return QM_EOK;
    }

    if (!linux_if_is_running(ifname)) {
        return -QM_EIO;
    }

    ret = linux_wifi_read_wireless_ap_info(ifname, ap_info);
    if (ret == QM_EOK) {
        return QM_EOK;
    }

    memset(ap_info, 0, sizeof(*ap_info));
    if (strncmp(ifname, "eth", 3) == 0) {
        memcpy(ap_info->ssid, "wired", 5);
        ap_info->ssid_len = 5;
        ap_info->rssi = 0;
        return QM_EOK;
    }

    return ret;
}

static qm_wifi_dev_ops_t wifi_ops = {
    .wifi_init            = hal_wifi_init,
    .wifi_deinit          = hal_wifi_deinit,
    .wifi_start           = hal_wifi_start,
    .wifi_stop            = hal_wifi_stop,
    .wifi_connect         = hal_wifi_connect,
    .wifi_disconnect      = hal_wifi_disconnet,
    .wifi_scan_start      = hal_wifi_scan_start,
    .wifi_scan_stop       = hal_wifi_scan_stop,
    .wifi_deauth          = hal_wifi_deauth,
    .wifi_set_ps          = hal_wifi_set_ps,
    .wifi_set_mac         = hal_wifi_set_mac,
    .wifi_get_mac         = hal_wifi_get_mac,
    .wifi_get_sta_rssi    = hal_wifi_get_sta_rssi,
    .wifi_get_sta_ap_info = hal_wifi_get_sta_ap_info,
};

int qm_hal_wifi_init(void)
{
    QM_LOGI(LOG_TAG, "register wifi ops (system network on linux)");
    return qm_wifi_dev_register(&wifi_ops, &s_wifi_priv);
}
