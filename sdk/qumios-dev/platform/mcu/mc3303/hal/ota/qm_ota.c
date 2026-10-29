/*
 * Linux OTA Implementation for MC632X
 * 
 * Implements qm_ota_* interfaces using standard Linux file I/O.
 * Target: /mnt/data/audio_main.bin (via temp file /mnt/data/audio_main.bin.tmp)
 * Strategy: Write to temp file; app layer detects package type, applies, and reboots via combo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "qm_ota.h"
#include "qm_log.h"

extern void xjiang_cloud_ota_apply_and_reboot(void);

#define LOG_TAG "ota"

static int ota_fd = -1;
static char ota_path[128] = {0};
static uint32_t ota_type;

static int __attribute__((unused))
xjiang_cloud_combo_priv_cmd_send(const char *cmd, const char *log_ctx)
{
    struct ifreq ifr;
    int ret, sock;
    char buf[MAX_COMBO_CMD_SIZE];
    combo_priv_cmd_t priv_cmd;

    if (cmd == NULL || log_ctx == NULL) {
        return -1;
    }

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        QM_LOGE(LOG_TAG, "%s: bad sock", log_ctx);
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, VIRTUAL_NETIF_NAME);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) != 0) {
        QM_LOGE(LOG_TAG, "%s: Could not read interface %s flags: %s",
                log_ctx, VIRTUAL_NETIF_NAME, strerror(errno));
        close(sock);
        return -1;
    }

    if (!(ifr.ifr_flags & IFF_UP)) {
        QM_LOGE(LOG_TAG, "%s: %s is not up", log_ctx, VIRTUAL_NETIF_NAME);
        close(sock);
        return -1;
    }

    memset(&priv_cmd, 0, sizeof(priv_cmd));
    memset(buf, 0, sizeof(buf));

    ret = snprintf(buf, sizeof(buf), "%s", cmd);
    if (ret < 0 || ret >= (int)sizeof(buf)) {
        QM_LOGE(LOG_TAG, "%s: snprintf overflow", log_ctx);
        close(sock);
        return -1;
    }

    priv_cmd.buf       = buf;
    priv_cmd.used_len  = (int)strlen(buf);
    priv_cmd.total_len = (int)sizeof(buf);
    ifr.ifr_data       = (void *)&priv_cmd;

    /* 驱动常在同一 buffer 写回 CFM-MSG 等应答，须在 ioctl 前记录已发送内容 */
    QM_LOGD(LOG_TAG, "%s: sent=\"%s\"", log_ctx, buf);

    ret = ioctl(sock, SIOCDEVPRIVATE + 2, &ifr);
    if (ret < 0) {
        QM_LOGE(LOG_TAG, "%s: ioctl fail, ret = %d", log_ctx, ret);
        close(sock);
        return ret;
    }

    close(sock);
    return 0;
}

int qm_ota_start(void *arg)
{
    ota_type = *(uint32_t *)arg;

    memset(ota_path, 0, sizeof(ota_path));
    if(ota_type){
        strcpy(ota_path, WIFI_UPGRADE_PATH);
        if(access(ota_path, F_OK) == 0){
            remove(ota_path);
        }
    }else{
        strcpy(ota_path, OTA_TEMP_PATH);
    }
    
    QM_LOGD(LOG_TAG, "Opening OTA target: %s", ota_path);
    
    // Open with O_TRUNC to free space immediately. 
    // This is necessary because partition size (2MB) ~= OTA file size (1.9MB).
    // We cannot hold two copies.
    ota_fd = open(ota_path, O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (ota_fd < 0) {
        QM_LOGE(LOG_TAG, "Failed to open OTA temp file: %s, errno=%d", ota_path, errno);
        return -1;
    }
    
    QM_LOGD(LOG_TAG, "OTA Start: Opened %s, fd=%d", ota_path, ota_fd);
    return QM_EOK;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    if (ota_fd < 0) {
        QM_LOGE(LOG_TAG, "OTA Write error: fd invalid");
        return -1;
    }
    
    // Write data at current file position
    ssize_t written = write(ota_fd, in_buf, in_buf_len);
    if (written != in_buf_len) {
        QM_LOGE(LOG_TAG, "OTA Write failed: written=%zd, expected=%u, errno=%d", written, in_buf_len, errno);
        return -1;
    }
    
    // Update offset if pointer provided
    if (off_set) {
        *off_set += in_buf_len;
    }
    
    return QM_EOK;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    if (ota_fd < 0 || off_set == NULL) {
         return -1;
    }
    
    // Use pread to read from specific offset without changing the file pointer used by write()
    // This allows verification reads (if any) to happen concurrently with sequential writes
    ssize_t r = pread(ota_fd, out_buf, out_buf_len, *off_set);
    if (r < 0) {
        QM_LOGE(LOG_TAG, "OTA Read failed: offset=%u, errno=%d", *off_set, errno);
        return -1;
    }
    
    *off_set += r;
    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    (void)arg;

    if (ota_fd >= 0) {
        close(ota_fd);
        ota_fd = -1;
    }
    if (ota_path[0] != '\0') {
        unlink(ota_path);
    }
    QM_LOGI(LOG_TAG, "OTA abort");
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    char cmd[128] = {0};
    QM_LOGD(LOG_TAG, "OTA End called");
    
    if (ota_fd >= 0) {
        if (fsync(ota_fd) < 0) {
            QM_LOGE(LOG_TAG, "fsync failed: errno=%d", errno);
        }
        close(ota_fd);
        ota_fd = -1;
    }
    
    if (chmod(ota_path, 0755) < 0) {
        QM_LOGE(LOG_TAG, "chmod failed: errno=%d", errno);
    }
    
    sync();
    QM_LOGD(LOG_TAG, "OTA temp file ready at %s, app layer will apply and reboot", ota_path);
    
    // 是网卡做OTA升级，需要使用custom_msg
    if(ota_type){
        QM_LOGI(LOG_TAG, "WIFI OTA DONE");
        sprintf(cmd, "custom_msg %s %d %s", VIRTUAL_NETIF_NAME, VIRTUAL_NETIF_OTA_CHANID, WIFI_UPGRADE_PATH);
        system(cmd);
    }else{
        QM_LOGI(LOG_TAG, "LOCAL OTA DONE");
        xjiang_cloud_ota_apply_and_reboot();
        // xjiang_cloud_combo_priv_cmd_send(COMBO_CMD_REQ_REBOOT, "send_reboot_to_combo");
    }

    return QM_EOK;
}