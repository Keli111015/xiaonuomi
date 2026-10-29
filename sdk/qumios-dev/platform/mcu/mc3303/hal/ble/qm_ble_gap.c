#include "qm_ble_gap.h"
#include "qm.h"
#include "qm_log.h"

#include <string.h>

#define LOG_TAG "mc3303 ble"

/*
 * MC3303 combo: BLE lives on the MCU side. Linux only needs GAP symbols for
 * link completeness; provisioning is done over vnet0, not local HCI.
 */

int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    return QM_EOK;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    (void)adv_data;
    return QM_EOK;
}

int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    (void)raw_data;
    (void)raw_data_len;
    return QM_EOK;
}

int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    (void)raw_data;
    (void)raw_data_len;
    return QM_EOK;
}

int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    (void)adv_params;
    return QM_EOK;
}

int qm_ble_gap_stop_advertising(void)
{
    return QM_EOK;
}

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    return QM_EOK;
}

int qm_ble_gap_start_scanning(uint32_t duration)
{
    (void)duration;
    return QM_EOK;
}

int qm_ble_gap_stop_scanning(void)
{
    return QM_EOK;
}

int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    if (addr == NULL) {
        return -QM_EINVAL;
    }
    memset(addr, 0, sizeof(qm_bd_addr_t));
    return QM_EOK;
}
