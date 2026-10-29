#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_utils_timer.h"

#include "bluetooth.h"
#include "hci_driver.h"
#include "hci_core.h"
#include "bl_wifi.h"

#define LOG_TAG "hal"

#define QM_MIBLE_MAX_UAER_NUM   (2)
#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

typedef struct 
{
    uint8_t scan_start;
    uint8_t adv_start;
    uint32_t duration;
    qm_ble_scan_params_t scan_params;
    qm_gap_ble_cb_t qm_cb[QM_MIBLE_MAX_UAER_NUM];
}qm_ble_gap_hal_t;

static qm_ble_gap_hal_t gap_hal_ctx = {0};

extern void qm_ble_hal_comm_init(void);
extern void qm_ble_hal_comm_deinit(void);

static void qm_gap_event_post(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    int i=0;
    while(i < QM_MIBLE_MAX_UAER_NUM){
        if(gap_hal_ctx.qm_cb[i]){
            gap_hal_ctx.qm_cb[i](event, param);
        }
        i++;
    }
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
    struct net_buf_simple *ad)
{
    qm_ble_gap_cb_param_t qm_param = {0};
    
    if(gap_hal_ctx.scan_start == QM_FALSE){
       return;
    }

    if(ad->len > (QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX)){
        return;
    }

    qm_param.scan_rst.rssi = rssi;
    qm_param.scan_rst.adv_data_len = ad->len;
    qm_param.scan_rst.bda[0] = addr->a.val[5];
    qm_param.scan_rst.bda[1] = addr->a.val[4];
    qm_param.scan_rst.bda[2] = addr->a.val[3];
    qm_param.scan_rst.bda[3] = addr->a.val[2];
    qm_param.scan_rst.bda[4] = addr->a.val[1];
    qm_param.scan_rst.bda[5] = addr->a.val[0];
    memcpy(qm_param.scan_rst.ble_adv, ad->data, ad->len);
    qm_gap_event_post(QM_GAP_BLE_SCAN_RESULT_EVT, &qm_param);

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
    int i=0;
    uint8_t is_use = QM_FALSE;
    while(i < QM_MIBLE_MAX_UAER_NUM){
        if(gap_hal_ctx.qm_cb[i] == NULL){
            is_use = QM_TRUE;
            gap_hal_ctx.qm_cb[i] = callback;
            break;
        }
        i++;
    }

    if(!is_use){
        return -QM_EINVAL;
    }
    
    qm_ble_hal_comm_init();

    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    int i=0;
    uint8_t is_use = QM_FALSE;
    while(i < QM_MIBLE_MAX_UAER_NUM){
        if(gap_hal_ctx.qm_cb[i] == callback){
            is_use = QM_TRUE;
            gap_hal_ctx.qm_cb[i] = NULL;
            break;
        }
        i++;
    }

    if(!is_use){
        return -QM_EINVAL;
    }

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

    return -QM_EINVAL;
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
    int ret = QM_EOK;
    QM_RETURN_ON_FALSE(raw_data, -QM_EINVAL, LOG_TAG, "raw_data inval");
    QM_RETURN_ON_FALSE(raw_data_len, -QM_EINVAL, LOG_TAG, "raw_data_len inval");
	

    qm_gap_event_post(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);

    return -QM_EINVAL;
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
    int ret = QM_EOK;

    QM_RETURN_ON_FALSE(adv_params, -QM_EINVAL, LOG_TAG, "adv params inval");

    if(gap_hal_ctx.adv_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.adv_start = QM_TRUE;

    qm_gap_event_post(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);

    return -QM_EINVAL;
}

/**
 * @brief           This function is called to stop advertising.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_advertising(void)
{
    if(!gap_hal_ctx.adv_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.adv_start = QM_FALSE;
    // qm_gap_event_post(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
    return -QM_EINVAL;
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
    QM_RETURN_ON_FALSE(scan_params, -QM_EINVAL, LOG_TAG, "scan_params inval");

    memcpy(&gap_hal_ctx.scan_params, scan_params, sizeof(qm_ble_scan_params_t));
    qm_gap_event_post(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);

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
    struct bt_le_scan_param scan_param = {0};

    if(gap_hal_ctx.scan_start){
        return -QM_EINVAL;
    }

    switch (gap_hal_ctx.scan_params.scan_type)
    {
        case QM_BLE_SCAN_TYPE_PASSIVE:
            scan_param.type = BT_LE_SCAN_TYPE_PASSIVE;
        break;

        case QM_BLE_SCAN_TYPE_ACTIVE:
            scan_param.type = BT_LE_SCAN_TYPE_ACTIVE;
        break;

        default:
        break;
    }
    
    gap_hal_ctx.duration = duration;

    scan_param.window = BT_ADV_SCAN_UNIT(gap_hal_ctx.scan_params.scan_window);
    scan_param.interval = BT_ADV_SCAN_UNIT(gap_hal_ctx.scan_params.scan_interval);
    
    gap_hal_ctx.scan_start = QM_TRUE;

    bt_le_scan_start(&scan_param, device_found);

    qm_gap_event_post(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
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
    if(!gap_hal_ctx.scan_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.scan_start = QM_FALSE;
    bt_le_scan_stop();
    qm_gap_event_post(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
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
    uint8_t bt_addr[6] = {0};
    bl_wifi_mac_addr_get(&bt_addr);
    addr[0] = bt_addr[0];
    addr[1] = bt_addr[1];
    addr[2] = bt_addr[2];
    addr[3] = bt_addr[3];
    addr[4] = bt_addr[4];
    addr[5] = bt_addr[5] + 1;
    return QM_EOK;
}

