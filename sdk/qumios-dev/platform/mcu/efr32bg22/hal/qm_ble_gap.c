#include "qm.h"
#include "qm_ble_gap.h"

#include "qm_utils_timer.h"
#include "miio_user_api.h"
#include "native_gecko.h"

#define ADV_HANDLE      0

typedef struct 
{
    
    qm_gap_ble_cb_t cb;
}qm_ble_gap_hal_t;

uint8_t qm_adv_start = 0;
uint8_t qm_scan_start = 0;
static qm_ble_gap_hal_t gap_hal_ctx = {0};

/**
 * @brief 该函数由底层调用，使用例子如下：
 *
 *
 *
 */

void qm_ble_gap_hal_scan_info(struct gecko_cmd_packet *p_evt)
{
    qm_ble_gap_cb_param_t param = {0};
    if(p_evt == NULL){
        return;
    }

    if(!gap_hal_ctx.cb || !qm_scan_start){
        return;
    }

    param.scan_rst.rssi = p_evt->data.evt_le_gap_scan_response.rssi;
    param.scan_rst.adv_data_len = p_evt->data.evt_le_gap_scan_response.data.len;
    param.scan_rst.bda[0] = p_evt->data.evt_le_gap_scan_response.address.addr[5];
    param.scan_rst.bda[1] = p_evt->data.evt_le_gap_scan_response.address.addr[4];
    param.scan_rst.bda[2] = p_evt->data.evt_le_gap_scan_response.address.addr[3];
    param.scan_rst.bda[3] = p_evt->data.evt_le_gap_scan_response.address.addr[2];
    param.scan_rst.bda[4] = p_evt->data.evt_le_gap_scan_response.address.addr[1];
    param.scan_rst.bda[5] = p_evt->data.evt_le_gap_scan_response.address.addr[0];
    memcpy(param.scan_rst.ble_adv, p_evt->data.evt_le_gap_scan_response.data.data, param.scan_rst.adv_data_len);
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
    gecko_cmd_le_gap_set_adv_data(0, raw_data_len, raw_data);
    gecko_cmd_le_gap_set_adv_data(1, 0, NULL);
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
    }
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
    if(!qm_adv_start){
        qm_adv_start = QM_TRUE;

        gecko_cmd_le_gap_set_advertise_timing(ADV_HANDLE, adv_params->adv_int_min,
                adv_params->adv_int_max, 0, 0);
        gecko_cmd_le_gap_set_advertise_channel_map(ADV_HANDLE, 7);
        gecko_cmd_le_gap_start_advertising(ADV_HANDLE, le_gap_user_data, le_gap_connectable_scannable);
    }
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
    }
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
    if(qm_adv_start){
        qm_adv_start = QM_FALSE;
        gecko_cmd_le_gap_stop_advertising(ADV_HANDLE);
    }
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
    }
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

extern unsigned char mi_pair_complete;
extern mible_status_t mible_gap_scan_start(mible_gap_scan_type_t scan_type,
        mible_gap_scan_param_t scan_param);
int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    if(qm_scan_start || !mi_pair_complete){
        mible_gap_scan_param_t scan_param = {
            .scan_interval = scan_params->scan_interval,
            .scan_window = scan_params->scan_window,
            .timeout = 0x0000,
        };
        mible_gap_scan_stop();
        mible_gap_scan_start(MIBLE_SCAN_TYPE_PASSIVE, scan_param);
    }
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
    qm_scan_start = QM_TRUE;
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
    qm_scan_start = QM_FALSE;
    if(!mi_pair_complete){
        gecko_cmd_le_gap_end_procedure();
    }else{
        mible_gap_scan_stop();
        miio_mesh_set_scan_level(2);
    }
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
    mible_addr_t str_mac;
    mible_gap_address_get(str_mac);

    addr[0] = str_mac[5];
    addr[1] = str_mac[4];
    addr[2] = str_mac[3];
    addr[3] = str_mac[2];
    addr[4] = str_mac[1];
    addr[5] = str_mac[0];

    return QM_EOK;
}
