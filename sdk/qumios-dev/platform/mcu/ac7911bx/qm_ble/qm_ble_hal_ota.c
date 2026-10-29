#include "system/includes.h"
#include "qm_ble_hal_ota.h"
#include "update/update.h"
#include "qm_ble_ota.h"
#include "qm_ble_log.h"
#include "qm.h"

// JL BLE OTA is intentionally disabled until it is wired to the platform
// update storage. These symbols fail explicitly if linked by mistake.

void qm_ble_ota_start(void)
{
    QM_BLE_LOGE("BLE OTA is not supported on JL platform");
}

int qm_ble_ota_flash_write(unsigned int *offset, unsigned char *buf, int buf_len)
{
    (void)offset;
    (void)buf;
    QM_BLE_LOGE("OTA write rejected, len=%d", buf_len);
    return -QM_ERROR;
}

int qm_ble_ota_flash_read(unsigned int *offset, unsigned char *buf, int buf_len)
{
    (void)offset;
    (void)buf;
    QM_BLE_LOGE("OTA read rejected, len=%d", buf_len);
    return -QM_ERROR;
}

void qm_ble_ota_end(int finish)
{
    QM_BLE_LOGW(QM_BLE_LOG_OTA, "OTA end called, finish=%d", finish);
}

int qm_ble_ota_finish_handle(void)
{
    QM_BLE_LOGE("OTA finish rejected");
    return -QM_ERROR;
}
