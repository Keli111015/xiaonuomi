#include "qm_ble_gap.h"

#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#endif

#include "qm.h"
#include "qm_log.h"
#include "esp_mac.h"

#define LOG_TAG "qm ble gap"

#ifdef CONFIG_BT_BLUEDROID_ENABLED

typedef struct 
{
    uint8_t scan_start;
    qm_gap_ble_cb_t cb;
}qm_ble_gap_hal_t;

static qm_ble_gap_hal_t qm_ble_gap_hal_ctx = {0};


static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: 
            //the unit of the duration is second, 0 means scan permanently
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
            }
        break;
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            //scan start complete event to indicate scan start successfully or failed
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
            }
        break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: 
        {
            qm_ble_gap_cb_param_t gap_param = {0};
            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
            switch (scan_result->scan_rst.search_evt) {
                case ESP_GAP_SEARCH_INQ_RES_EVT:
                    if(qm_ble_gap_hal_ctx.cb){
                        gap_param.scan_rst.rssi = scan_result->scan_rst.rssi;
                        gap_param.scan_rst.adv_data_len = scan_result->scan_rst.adv_data_len;
                        gap_param.scan_rst.scan_rsp_len = scan_result->scan_rst.scan_rsp_len;
                        memcpy(gap_param.scan_rst.bda, scan_result->scan_rst.bda, sizeof(scan_result->scan_rst.bda));
                        memcpy(gap_param.scan_rst.ble_adv, scan_result->scan_rst.ble_adv, sizeof(scan_result->scan_rst.ble_adv));
                        qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &gap_param);
                    }
                break;
                default:
                break;
            }
        }break;

        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, NULL);
            }
        break;
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
            }
        break;

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
            }
        break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
            }
        break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
            }    
        break;
        default:

        break;
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
    if(esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

        esp_bt_controller_init(&bt_cfg);

        esp_bt_controller_enable(ESP_BT_MODE_BLE);

        esp_bluedroid_init();

        esp_bluedroid_enable();

        esp_ble_gap_register_callback(gap_event_handler);
    }

    qm_ble_gap_hal_ctx.cb = callback;
   
    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    qm_ble_gap_hal_ctx.cb = NULL;
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
    esp_ble_adv_data_t adv_config = {0};

    if(adv_data == NULL){
        return -QM_EINVAL;
    }
    
    adv_config.min_interval = adv_data->min_interval;
    adv_config.max_interval = adv_data->max_interval;
    adv_config.manufacturer_len = adv_data->manufacturer_len;
    adv_config.p_manufacturer_data = adv_data->p_manufacturer_data;
    return esp_ble_gap_config_adv_data(&adv_config);
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
    if(raw_data == NULL || raw_data_len == 0){
        return -QM_EINVAL;
    }
    return esp_ble_gap_config_adv_data_raw(raw_data, raw_data_len);
}

/**
 * @brief           This function is called to set raw rsp advertising data. User need to fill
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
    if(raw_data == NULL || raw_data_len == 0){
        return -QM_EINVAL;
    }
    return esp_ble_gap_config_scan_rsp_data_raw(raw_data, raw_data_len);       
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
    esp_ble_adv_params_t esp_adv_params = {0};

    if(adv_params == NULL){
        return -QM_EINVAL;
    }

    esp_adv_params.adv_int_min = adv_params->adv_int_min;
    esp_adv_params.adv_int_max = adv_params->adv_int_max;
    esp_adv_params.channel_map = ADV_CHNL_ALL,
    esp_adv_params.adv_type = ADV_TYPE_IND,
    esp_adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    esp_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    return esp_ble_gap_start_advertising(&esp_adv_params);
}

/**
 * @brief           This function is called to stop advertising.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_advertising(void)
{
    return esp_ble_gap_stop_advertising();
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
    esp_ble_scan_params_t esp_scan_params = {0};

    if(scan_params == NULL){
        return -QM_EINVAL;
    }

    esp_scan_params.scan_type = scan_params->scan_type;
    esp_scan_params.scan_window = scan_params->scan_window;
    esp_scan_params.scan_interval = scan_params->scan_interval;
    esp_scan_params.own_addr_type          = BLE_ADDR_TYPE_PUBLIC;
    esp_scan_params.scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL;
    esp_scan_params.scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE;
    
    return esp_ble_gap_set_scan_params(&esp_scan_params);
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
    return esp_ble_gap_start_scanning(duration);
}

/**
 * @brief    This function call to stop the device scanning the peer device which advertising on the air
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_scanning(void)
{
    return esp_ble_gap_stop_scanning();
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
    esp_read_mac(addr, ESP_MAC_BT);
    return QM_EOK;
}
#else
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
    int ret = QM_EOK;
    uint8_t str_mac[6] = {0};
    ret = esp_base_mac_addr_get(addr);
    if (ret != QM_EOK){
        return -QM_EINVAL;
    }
    
    addr[5] += 2;

    return QM_EOK;
}
#endif
