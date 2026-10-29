#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_utils_timer.h"
#include "miio_user_api.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/mesh.h>

#include <../../subsys/bluetooth/host/hci_core.h>

typedef struct 
{
    uint8_t adv_start;
    uint8_t scan_start;
    uint8_t scan_duration;
    qm_gap_ble_cb_t cb;
    qm_utils_time_t scan_timer;
}qm_ble_gap_hal_t;

static qm_ble_gap_hal_t gap_hal_ctx = {0};

/**
 * @brief 该函数由底层调用，使用例子如下：
 *
 *
 *
 */
void qm_ble_gap_hal_scan_info(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
    qm_ble_gap_cb_param_t param = {0};
    if(info == NULL || buf == NULL){
        return;
    }

    if(!gap_hal_ctx.cb || !gap_hal_ctx.scan_start){
        return;
    }

    if(gap_hal_ctx.scan_duration){
        if(qm_utils_time_is_expired(&gap_hal_ctx.scan_timer)){
            gap_hal_ctx.scan_start = QM_FALSE;
            return;
        }
    }

    if(buf->len > (QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX)){
        return;
    }


    param.scan_rst.rssi = info->rssi;
    param.scan_rst.adv_data_len = buf->len;
    memcpy(param.scan_rst.bda, info->addr->a.val, 6);
    memcpy(param.scan_rst.ble_adv, buf->data, buf->len);
    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
}

/**
 * @brief           This function is called to occur gap event, such as scan result
 *
 * @param[in]       callback: callback function
 *
 * @return  0 on success, otherwise failured
 *
 */
int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = callback;
    return QM_EOK;
}

/**
 * @brief           This function is called to override the BTA default ADV parameters.
 *
 * @param[in]       adv_data: Pointer to User defined ADV data structure: @ref qm_ble_adv_data_t
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    return QM_EOK;
}

/**
 * @brief           This function is called to set raw advertising data. User need to fill
 *                  ADV data by self.
 *
 * @param[in]       raw_data : raw advertising data
 * @param[in]       raw_data_len : raw advertising data length , less than 31 bytes
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{

    return QM_EOK;
}

/**
 * @brief           This function is called to start advertising.
 *
 * @param[in]       adv_params: pointer to User defined adv_params data structure: @ref qm_ble_adv_params_t

 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    return QM_EOK;
}

/**
 * @brief           This function is called to stop advertising.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_advertising(void)
{
    return QM_EOK;
}

/**
 * @brief           This function is called to set scan parameters
 *
 * @param[in]       scan_params: Pointer to User defined scan_params data structure: @ref qm_ble_scan_params_t
 *
 * @return  0 on success, otherwise failure.
 *
 */

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

/**
 * @brief           This procedure keep the device scanning the peer device which advertising on the air
 *
 * @param[in]       duration: Keeping the scanning time, the unit is second.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_start_scanning(uint32_t duration)
{
    gap_hal_ctx.scan_start = QM_TRUE;
    gap_hal_ctx.scan_duration = duration;

    if(duration){
        qm_utils_time_countdown_ms(&gap_hal_ctx.scan_timer, duration * 1000);
    }

    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

/**
 * @brief    This function call to stop the device scanning the peer device which advertising on the air
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_scanning(void)
{
    gap_hal_ctx.scan_duration = 0;
    gap_hal_ctx.scan_start = QM_FALSE;
   
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
    }

    return QM_EOK;
}

/**
 * @brief          This function is called to get local used address and adress type.
 *
 * @param[in]       addr - current local ble address (six bytes)
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    uint8_t mac[6] = {0};
    mible_gap_address_get(mac);

    addr[0] = mac[5];
    addr[1] = mac[4];
    addr[2] = mac[3];
    addr[3] = mac[2];
    addr[4] = mac[1];
    addr[5] = mac[0];

    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = NULL;
    return QM_EOK;
}
