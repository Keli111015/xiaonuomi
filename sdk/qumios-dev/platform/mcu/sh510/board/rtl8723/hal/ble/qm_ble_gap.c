#include "qm_ble_gap.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_linux_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "linux ble"

#ifndef QM_LINUX_BLE_HCI_WAIT_SEC
#define QM_LINUX_BLE_HCI_WAIT_SEC 20
#endif

#ifndef QM_LINUX_BLE_HCI_START
#define QM_LINUX_BLE_HCI_START "/etc/init.d/S55bluetooth start >/dev/null 2>&1"
#endif

static int linux_ble_read_hci_addr(qm_bd_addr_t addr)
{
    FILE *fp;
    char line[128];
    unsigned int b[6];
    int i;

    fp = popen("hciconfig hci0 2>/dev/null", "r");
    if (fp == NULL) {
        return -QM_EIO;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = strstr(line, "BD Address:");
        if (p == NULL) {
            continue;
        }
        if (sscanf(p, "BD Address: %02x:%02x:%02x:%02x:%02x:%02x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            for (i = 0; i < 6; i++) {
                addr[i] = (uint8_t)b[i];
            }
            pclose(fp);
            return QM_EOK;
        }
    }

    pclose(fp);
    return -QM_ENOENT;
}

int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    (void)adv_data;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    (void)raw_data;
    (void)raw_data_len;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    (void)raw_data;
    (void)raw_data_len;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    (void)adv_params;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_stop_advertising(void)
{
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_start_scanning(uint32_t duration)
{
    (void)duration;
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_stop_scanning(void)
{
    QM_LINUX_STUB_WARN(LOG_TAG, __func__);
    return -QM_ENOSYS;
}

int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    int i;

    if (addr == NULL) {
        return -QM_EINVAL;
    }

    if (linux_ble_read_hci_addr(addr) == QM_EOK) {
        return QM_EOK;
    }

    (void)system(QM_LINUX_BLE_HCI_START);
    for (i = 0; i < QM_LINUX_BLE_HCI_WAIT_SEC; i++) {
        if (linux_ble_read_hci_addr(addr) == QM_EOK) {
            QM_LOGI(LOG_TAG, "hci0 ready (%ds) mac=%02X:%02X:%02X:%02X:%02X:%02X",
                    i, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
            return QM_EOK;
        }
        if ((i % 3) == 0) {
            QM_LOGI(LOG_TAG, "wait hci0 %ds...", i);
            (void)system(QM_LINUX_BLE_HCI_START);
        }
        sleep(1);
    }

    QM_LOGE(LOG_TAG, "hci0 not ready after %ds", QM_LINUX_BLE_HCI_WAIT_SEC);
    return -QM_ENOENT;
}
