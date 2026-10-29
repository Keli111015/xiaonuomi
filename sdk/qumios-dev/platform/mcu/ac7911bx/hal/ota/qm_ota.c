#include "qm_ota.h"
#include "net_update.h"
#include "qm_log.h"

#define LOG_TAG "qm_ota"

static void *s_update_fd = NULL;

int qm_ota_start(void *arg)
{
    // Close any existing opened update handle just in case
    if (s_update_fd) {
        net_fclose(s_update_fd, 1);
        s_update_fd = NULL;
    }

    QM_LOGI(LOG_TAG, "OTA start");

    // Open the OTA file handle for JieLi network update
    s_update_fd = net_fopen(CONFIG_UPGRADE_OTA_FILE_NAME, "w");
    if (!s_update_fd) {
        QM_LOGE(LOG_TAG, "Failed to open OTA file");
        return -1;
    }

    return 0; // Success
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    if (!s_update_fd) {
        QM_LOGE(LOG_TAG, "OTA not started");
        return -1;
    }

    // Write to flash
    int ret = net_fwrite(s_update_fd, in_buf, in_buf_len, 0);
    if (ret != (int)in_buf_len) {
        QM_LOGE(LOG_TAG, "OTA write failed ret=%d len=%u", ret, in_buf_len);
        return -1;
    }

    if (off_set) {
        *off_set += in_buf_len;
    }

    return 0; // Success
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    // If QUMI needs to read back OTA data for verification, we map to net_fread
    // Note: Depends on whether net_fread behaves seamlessly after net_fwrite.
    // If JieLi's net_update doesn't support reading during OTA writing, this may return error.
    if (!s_update_fd) {
        return -1;
    }

    int ret = net_fread(s_update_fd, (char *)out_buf, out_buf_len);
    if (ret <= 0) {
        return -1;
    }

    if (off_set) {
        *off_set += ret;
    }

    return 0;
}

int qm_ota_end(void *arg)
{
    if (!s_update_fd) {
        QM_LOGE(LOG_TAG, "OTA not started");
        return -1;
    }

    QM_LOGI(LOG_TAG, "OTA end");

    // Tell net_fclose that this is the final packet and trigger verification
    // Passing 0 normally triggers the success flow inside JieLi SDK.
    int ret = net_fclose(s_update_fd, 0);
    s_update_fd = NULL;

    if (ret != 0) {
        QM_LOGE(LOG_TAG, "OTA end / verify failed");
        return -1;
    }
    
    // Some logic like qm_reboot() might be needed afterwards depending on if 
    // QUMI auto-triggers a reboot, or if net_fclose auto-reboots on success.
    // JieLi's updater usually triggers a system soft reset implicitly after a successful net_fclose

    return 0; // Success
}
