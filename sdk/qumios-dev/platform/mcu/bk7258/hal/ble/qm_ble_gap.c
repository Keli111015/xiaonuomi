#include "qm_ble_gap.h"

#include <string.h>

#include <os/os.h>
#include <components/bluetooth/bk_ble.h>
#include <components/bluetooth/bk_dm_bluetooth.h>

#include "qm.h"

#define QM_GAP_ADV_ACTV_IDX     0
#define QM_GAP_SYNC_TIMEOUT_MS  4000

#define ADV_TYPE_FLAGS                   0x01
#define ADV_ALL_CHNLS                    0x07

static uint8_t s_adv_data[QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX];
static uint16_t s_adv_len;
static uint8_t s_scan_rsp[QM_BLE_SCAN_RSP_DATA_LEN_MAX];
static uint16_t s_scan_rsp_len;
static uint8_t s_adv_running;
static beken_semaphore_t s_gap_sem;
static qm_gap_ble_cb_t s_gap_cb;

static void qm_gap_ble_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t *param)
{
    (void)param;

    switch (cmd) {
    case BLE_CREATE_ADV:
    case BLE_SET_ADV_DATA:
    case BLE_SET_RSP_DATA:
    case BLE_START_ADV:
    case BLE_STOP_ADV:
    case BLE_DELETE_ADV:
        if (s_gap_sem != NULL) {
            rtos_set_semaphore(&s_gap_sem);
        }
        break;
    default:
        break;
    }
}

static int qm_gap_sem_wait(void)
{
    if (s_gap_sem == NULL) {
        return -QM_ERROR;
    }
    return (rtos_get_semaphore(&s_gap_sem, QM_GAP_SYNC_TIMEOUT_MS) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    s_gap_cb = callback;
    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    s_gap_cb = NULL;
    return QM_EOK;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    (void)adv_data;
    return -QM_ERROR;
}

int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (raw_data == NULL || raw_data_len == 0 || raw_data_len > sizeof(s_adv_data)) {
        return -QM_EINVAL;
    }

    memcpy(s_adv_data, raw_data, raw_data_len);
    s_adv_len = (uint16_t)raw_data_len;

    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (raw_data_len > sizeof(s_scan_rsp)) {
        return -QM_EINVAL;
    }

    s_scan_rsp_len = 0;
    if (raw_data != NULL && raw_data_len > 0) {
        memcpy(s_scan_rsp, raw_data, raw_data_len);
        s_scan_rsp_len = (uint16_t)raw_data_len;
    }
    return QM_EOK;
}

static int qm_gap_adv_teardown(void)
{
    ble_err_t ret;

    /* Best-effort stop+delete so reconnect can create a fresh activity. */
    ret = bk_ble_stop_advertising(QM_GAP_ADV_ACTV_IDX, qm_gap_ble_cmd_cb);
    if (ret == BK_ERR_BLE_SUCCESS) {
        (void)qm_gap_sem_wait();
    }

    ret = bk_ble_delete_advertising(QM_GAP_ADV_ACTV_IDX, qm_gap_ble_cmd_cb);
    if (ret == BK_ERR_BLE_SUCCESS) {
        (void)qm_gap_sem_wait();
    }

    s_adv_running = 0;
    return QM_EOK;
}

int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    ble_adv_param_t param;
    ble_err_t ret;

    if (adv_params == NULL) {
        return -QM_EINVAL;
    }

    if (s_gap_sem == NULL) {
        rtos_init_semaphore(&s_gap_sem, 1);
    }

    /* After a connection the controller stops ADV but keep create/start state
     * dirty. Always tear down then recreate — avoids 4s semaphore timeout. */
    (void)qm_gap_adv_teardown();

    memset(&param, 0, sizeof(param));
    param.chnl_map = ADV_ALL_CHNLS;
    param.adv_intv_min = adv_params->adv_int_min ? adv_params->adv_int_min : 0x00A0;
    param.adv_intv_max = adv_params->adv_int_max ? adv_params->adv_int_max : 0x00F0;
    param.own_addr_type = OWN_ADDR_TYPE_PUBLIC_ADDR;
    param.adv_type = ADV_TYPE_LEGACY;
    param.adv_prop = ADV_PROP_CONNECTABLE_BIT | ADV_PROP_SCANNABLE_BIT;
    param.prim_phy = INIT_PHY_TYPE_LE_1M;
    param.second_phy = INIT_PHY_TYPE_LE_1M;

    ret = bk_ble_create_advertising(QM_GAP_ADV_ACTV_IDX, &param, qm_gap_ble_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS) {
        QM_LOGE("GAP", "create adv %#x", (unsigned)ret);
        return -QM_ERROR;
    }
    if (qm_gap_sem_wait() != QM_EOK) {
        QM_LOGE("GAP", "create adv timeout");
        return -QM_ERROR;
    }

    if (s_adv_len > 0) {
        ret = bk_ble_set_adv_data(QM_GAP_ADV_ACTV_IDX, s_adv_data, s_adv_len, qm_gap_ble_cmd_cb);
        if (ret != BK_ERR_BLE_SUCCESS || qm_gap_sem_wait() != QM_EOK) {
            QM_LOGE("GAP", "set adv data fail %#x", (unsigned)ret);
            return -QM_ERROR;
        }
    }

    if (s_scan_rsp_len > 0) {
        ret = bk_ble_set_scan_rsp_data(QM_GAP_ADV_ACTV_IDX, s_scan_rsp, s_scan_rsp_len, qm_gap_ble_cmd_cb);
        if (ret != BK_ERR_BLE_SUCCESS || qm_gap_sem_wait() != QM_EOK) {
            QM_LOGE("GAP", "set scan rsp fail %#x", (unsigned)ret);
            return -QM_ERROR;
        }
    }

    ret = bk_ble_start_advertising(QM_GAP_ADV_ACTV_IDX, 0, qm_gap_ble_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS || qm_gap_sem_wait() != QM_EOK) {
        QM_LOGE("GAP", "start adv fail %#x", (unsigned)ret);
        return -QM_ERROR;
    }

    s_adv_running = 1;
    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

int qm_ble_gap_stop_advertising(void)
{
    if (!s_adv_running) {
        return QM_EOK;
    }

    (void)qm_gap_adv_teardown();

    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    /* TODO: BLE central scan for prodtst / combo-net client role */
    return -QM_ERROR;
}

int qm_ble_gap_start_scanning(uint32_t duration)
{
    (void)duration;
    return -QM_ERROR;
}

int qm_ble_gap_stop_scanning(void)
{
    return -QM_ERROR;
}

int qm_ble_gap_get_local_addr(uint8_t *mac)
{
    if (mac == NULL) {
        return -QM_EINVAL;
    }

    if (bk_bluetooth_get_address(mac) != BT_OK) {
        return -QM_ERROR;
    }
    return QM_EOK;
}
