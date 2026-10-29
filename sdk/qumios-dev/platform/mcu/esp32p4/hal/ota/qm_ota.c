#include "qm_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "qm_log.h"

#define platform_log(M, ...)       QM_LOGD("ota", M, ##__VA_ARGS__)

static uint32_t offset_addr = 0;
static esp_partition_t operate_partition;
static esp_ota_handle_t out_handle;
static esp_err_t esp_write_error;
static bool ota_in_progress = false;

static bool esp_ota_prepare(void)
{
    esp_err_t err;
    esp_partition_t find_partition;
    const esp_partition_t *esp_current_partition;
    const esp_partition_t *partition;

    offset_addr = 0;
    esp_current_partition = esp_ota_get_boot_partition();
    if (esp_current_partition == NULL) {
        platform_log("esp_ota_get_boot_partition got null prt");
        return false;
    }

    if (esp_current_partition->type != ESP_PARTITION_TYPE_APP) {
        platform_log("Error esp_current_partition->type != ESP_PARTITION_TYPE_APP");
        return false;
    }

    switch (esp_current_partition->subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:
            find_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
            break;
        case ESP_PARTITION_SUBTYPE_APP_OTA_0:
            find_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
            break;
        case ESP_PARTITION_SUBTYPE_APP_OTA_1:
            find_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
            break;
        default:
            find_partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
            break;
    }

    find_partition.type = ESP_PARTITION_TYPE_APP;
    partition = esp_partition_find_first(find_partition.type, find_partition.subtype, NULL);
    if (partition == NULL) {
        platform_log("esp_partition_find_first failed");
        return false;
    }

    memset(&operate_partition, 0, sizeof(operate_partition));
    esp_write_error = ESP_OK;
    err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &out_handle);
    if (err != ESP_OK) {
        platform_log("esp_ota_begin failed err=0x%x!", err);
        return false;
    }

    memcpy(&operate_partition, partition, sizeof(operate_partition));
    ota_in_progress = true;
    platform_log("esp_ota_begin init OK");
    return true;
}

int qm_ota_start(void *arg)
{
    (void)arg;
    if (!esp_ota_prepare()) {
        return -QM_EIO;
    }
    return QM_EOK;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    esp_err_t err;

    if (!ota_in_progress || !in_buf || in_buf_len == 0) {
        return -QM_EINVAL;
    }

    if (off_set == NULL) {
        err = esp_ota_write(out_handle, (const void *)in_buf, in_buf_len);
    } else {
        err = esp_ota_write_with_offset(out_handle, (const void *)in_buf, in_buf_len, offset_addr);
        offset_addr += in_buf_len;
    }

    if (err != ESP_OK) {
        esp_write_error = err;
        platform_log("Error: esp_ota_write failed! err=%x", err);
        return -QM_EIO;
    }

    if (off_set) {
        *off_set = offset_addr;
    }
    return QM_EOK;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    size_t src_offset;

    if (off_set == NULL || out_buf == NULL || out_buf_len == 0) {
        return -QM_EINVAL;
    }

    src_offset = *off_set;
    if (esp_partition_read(&operate_partition, src_offset, out_buf, out_buf_len) != ESP_OK) {
        return -QM_EIO;
    }

    *off_set += out_buf_len;
    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    esp_err_t err;

    (void)arg;
    if (!ota_in_progress) {
        return QM_EOK;
    }

    ota_in_progress = false;
    err = esp_ota_abort(out_handle);
    if (err != ESP_OK) {
        platform_log("esp_ota_abort failed! err=0x%x", err);
        return -QM_EIO;
    }

    offset_addr = 0;
    esp_write_error = ESP_OK;
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    esp_err_t err;

    (void)arg;
    if (!ota_in_progress) {
        return -QM_EINVAL;
    }

    ota_in_progress = false;
    err = esp_ota_end(out_handle);
    if (err != ESP_OK) {
        platform_log("esp_ota_end failed! err=0x%x", err);
        return -QM_EIO;
    }

    if (esp_write_error != ESP_OK) {
        platform_log("esp_ota write_error %d!", esp_write_error);
        return -QM_EIO;
    }

    err = esp_ota_set_boot_partition(&operate_partition);
    if (err != ESP_OK) {
        platform_log("esp_ota_set_boot_partition failed! err=0x%x", err);
        return -QM_EIO;
    }

    platform_log("OTA image ready, reboot is left to caller");
    return QM_EOK;
}
