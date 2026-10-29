#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"

#include "include.h"
#include "hal_clock.h"
#include "api_rf_2g4.h"

#define TX_MODE                0
#define RX_MODE                1

#define DEL_ADV_DATA_LEN    (2)
#define DEL_ADV_DATA_TYPE   (1)
#define DEL_ADV_DATA_VALUE  (6)

#define ADV_DATA_MAX_LEN    (31)
#define GAP_HAL_WAIT_TIME   (QM_WAIT_FOREVER)

#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

#define BT_MAC_FLASH_ADDR    (0x1003f000)

typedef struct 
{
    int adv_enable;
    int scan_enable;
    qm_gap_ble_cb_t cb;
}qm_ble_gap_hal_t;

    
static qm_ble_gap_hal_t gap_hal_ctx = {0};

static uint8_t tx_ble_adv_data_buf[MAX_RF_LENGTH] = {

    // //BLE ADV HDR
    0x42, 0x25,   //ADV TYPE, RFU, TX/RX ADDR, LEN = 6(DEVICE ADDR) + PAYLOAD LEN
    0x05, 0x04, 0x03, 0x02, 0x01, 0x00,  //DEVICE ADDR
};



__RAM_CODE_SECTION void rf_2g4_rx_handler(uint8_t *rx_data ,uint8_t rx_len)
{
    qm_ble_gap_cb_param_t param = {0};

    if(rx_len < (20) || rx_data == NULL){
        return;
    }

    if((rx_data[1] < 6) || ((rx_data[1] - 6) > QM_BLE_ADV_DATA_LEN_MAX)){
        return;
    }

    param.scan_rst.rssi = 0;
    param.scan_rst.adv_data_len = (rx_data[1] - 6);
    memcpy(param.scan_rst.bda, &rx_data[2], 6);
    memcpy(param.scan_rst.ble_adv, &rx_data[8], (rx_data[1] - 6));
    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
}

void qm_ble_gap_hal_scan_info(void)
{
    uint32_t recv = 0;
    static uint32_t channel = RF_CH37;
    static uint32_t t_2g4_tick = 0;

    if (hal_clock_time_exceed(t_2g4_tick, 10 * 1000))
    {
        t_2g4_tick = hal_clock_get_system_tick();
        if(gap_hal_ctx.adv_enable){
            rf_2g4_tx_data_switch_chl(tx_ble_adv_data_buf,tx_ble_adv_data_buf[8] + 9);//RF_CH37\RF_CH38\RF_CH39
        }

        if (gap_hal_ctx.scan_enable)
        {
            if(channel == RF_CH37){
                channel = RF_CH38;
            }else if(channel == RF_CH38){
                channel = RF_CH39;
            }else{
                channel = RF_CH37;
            }
            rf_2g4_rx_data(rf_2g4_rx_data_buf,MAX_RF_LENGTH,channel);
        }


    }
    if (gap_hal_ctx.scan_enable)
    {
        rf_2g4_rx_start(channel);
    }
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
    memcpy(&tx_ble_adv_data_buf[2], (uint8_t*)BT_MAC_FLASH_ADDR, 6);
    return QM_EOK;
}

/**
 * @brief           This function is called to occur gap event, such as scan result
 *
 * @param[in]       callback: callback function
 *
 * @return  0 on success, otherwise failured
 *
 */
int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = NULL;
    return QM_EOK;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    int ret = QM_EOK;
    int index = 0;
    QM_RETURN_ON_FALSE(adv_data, -QM_EINVAL, LOG_TAG, "raw_data inval");


    QM_HEX_LOGD(LOG_TAG, "ADV ", adv_data->p_manufacturer_data, adv_data->manufacturer_len);


    if(adv_data->manufacturer_len > QM_BLE_ADV_DATA_LEN_MAX)
    {
        return -QM_EINVAL;
    }

    tx_ble_adv_data_buf[8] = adv_data->manufacturer_len + 1;
    tx_ble_adv_data_buf[9] = 0xFF;
    memcpy(&tx_ble_adv_data_buf[10],adv_data->p_manufacturer_data, adv_data->manufacturer_len);

    gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, NULL);

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
 * @brief           This function is called to set raw advertising data. User need to fill
 *                  ADV data by self.
 *
 * @param[in]       raw_data : raw advertising data
 * @param[in]       raw_data_len : raw advertising data length , less than 31 bytes
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT, NULL);

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
    QM_RETURN_ON_FALSE(adv_params, -QM_EINVAL, LOG_TAG, "adv params inval");

    gap_hal_ctx.adv_enable = QM_TRUE;

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

    gap_hal_ctx.adv_enable = QM_FALSE;

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
    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
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
    gap_hal_ctx.scan_enable = QM_TRUE;
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
    gap_hal_ctx.scan_enable = QM_FALSE;
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
    addr[0] = tx_ble_adv_data_buf[7];
    addr[1] = tx_ble_adv_data_buf[6];
    addr[2] = tx_ble_adv_data_buf[5];
    addr[3] = tx_ble_adv_data_buf[4];
    addr[4] = tx_ble_adv_data_buf[3];
    addr[5] = tx_ble_adv_data_buf[2];

    return QM_EOK;
}
