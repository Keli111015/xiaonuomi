#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"
#include "ls_ble.h"
#include "ls_sys.h"


#define DEL_ADV_DATA_LEN    (2)
#define DEL_ADV_DATA_TYPE   (1)
#define DEL_ADV_DATA_VALUE  (6)

#define ADV_DATA_MAX_LEN    (31)
#define GAP_HAL_WAIT_TIME   (QM_WAIT_FOREVER)

#define LOG_TAG "GAP"
#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)


typedef struct 
{
    uint8_t adv_start;
    uint8_t adv_update;
    uint8_t scan_start;


    uint8_t mac[6];
    qm_gap_ble_cb_t cb;
    qm_ble_adv_params_t adv_params;
    qm_ble_scan_params_t scan_params;
    uint8_t adv_data_len;
    uint8_t adv_data[ADV_DATA_MAX_LEN];
    uint8_t adv_rsp_data_len;
    uint8_t adv_rsp_data[ADV_DATA_MAX_LEN];
}qm_ble_gap_hal_t;

static uint8_t adv_obj_hdl;
static uint8_t scan_obj_hdl;
static qm_ble_gap_hal_t gap_hal_ctx = {0};

extern int dev_manager_cb_register(void (*cb)(enum dev_evt_type type,union dev_evt_u *evt));
extern int dev_manager_cb_unregister(void (*cb)(enum dev_evt_type type,union dev_evt_u *evt));


__attribute__((weak)) void create_adv_obj(void)
{
    struct legacy_adv_obj_param adv_param = {
        .adv_intv_min =  BT_ADV_SCAN_UNIT(100),
        .adv_intv_max = BT_ADV_SCAN_UNIT(100),
        .own_addr_type = PUBLIC_OR_RANDOM_STATIC_ADDR,
        .filter_policy = 0,
        .ch_map = 0x7,
        .disc_mode = ADV_MODE_BEACON,
        .prop = {
            .connectable = 1,
            .scannable = 1,
            .directed = 0,
            .high_duty_cycle = 0,
        },
    };
    
    gap_hal_ctx.adv_params.adv_int_max = BT_ADV_SCAN_UNIT(100);
    gap_hal_ctx.adv_params.adv_int_min = BT_ADV_SCAN_UNIT(100);
    dev_manager_create_legacy_adv_object(&adv_param);

    dev_manager_create_scan_object(PUBLIC_OR_RANDOM_STATIC_ADDR);
}

static void dev_manager_main_callback(enum dev_evt_type type,union dev_evt_u *evt)
{
    qm_ble_gap_cb_param_t param = {0};

    switch(type)
    {
        case STACK_READY:
        {
            bool type;
            dev_manager_get_identity_bdaddr(gap_hal_ctx.mac,&type);
            create_adv_obj();  
        }
        break;
        case ADV_OBJ_CREATED:
        {
            adv_obj_hdl = evt->obj_created.handle;
        }
        break;
        case SCAN_OBJ_CREATED:
        {
            scan_obj_hdl = evt->obj_created.handle;            
        }
        break;
        case ADV_STARTED:
            gap_hal_ctx.adv_start = QM_TRUE;
            gap_hal_ctx.cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
        break;
        case ADV_UPDATED:

        break;
        case ADV_STOPPED:

            gap_hal_ctx.adv_start = QM_FALSE;
            gap_hal_ctx.cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
        break;
        case SCAN_STARTED:

            gap_hal_ctx.cb(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
        break;
        case SCAN_STOPPED:
     
            gap_hal_ctx.cb(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
        break;
        case ADV_REPORT:
        {
            if(evt->adv_report.length > (QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX)){
                break;
            }
            param.scan_rst.rssi = evt->adv_report.rssi;
            param.scan_rst.adv_data_len = evt->adv_report.length;
            memcpy(param.scan_rst.bda, evt->adv_report.adv_addr->addr, 6);
            memcpy(param.scan_rst.ble_adv, evt->adv_report.data, evt->adv_report.length);
            gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
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
    gap_hal_ctx.cb = callback;
    dev_manager_cb_register(dev_manager_main_callback);
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
    dev_manager_cb_unregister(dev_manager_main_callback);
    return QM_EOK;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    int ret = QM_EOK;
    int index = 0;
    int bt_num = 0;
    QM_RETURN_ON_FALSE(adv_data, -QM_EINVAL, LOG_TAG, "raw_data inval");

    if( adv_data->manufacturer_len == 0 || 
        adv_data->p_manufacturer_data == NULL){
        gap_hal_ctx.adv_data_len = 0;
        memset(gap_hal_ctx.adv_data, 0, ADV_DATA_MAX_LEN);
        return QM_EOK;
    }

    gap_hal_ctx.adv_data_len = adv_data->manufacturer_len + 2;
    gap_hal_ctx.adv_data[0] = adv_data->manufacturer_len + 1;
    gap_hal_ctx.adv_data[1] = GAP_ADV_TYPE_MANU_SPECIFIC_DATA;
    memcpy(gap_hal_ctx.adv_data + 2, adv_data->p_manufacturer_data, adv_data->manufacturer_len);

    QM_HEX_LOGD(LOG_TAG, "ADV ", gap_hal_ctx.adv_data, gap_hal_ctx.adv_data_len);

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
    if(raw_data_len == 0 || raw_data == NULL){
        gap_hal_ctx.adv_data_len = 0;
        memset(gap_hal_ctx.adv_data, 0, ADV_DATA_MAX_LEN);
        return QM_EOK;
    }else{
        gap_hal_ctx.adv_data_len = raw_data_len;
        memset(gap_hal_ctx.adv_data, 0, ADV_DATA_MAX_LEN);
        memcpy(gap_hal_ctx.adv_data, raw_data, raw_data_len);
        QM_HEX_LOGD(LOG_TAG, "ADV ", gap_hal_ctx.adv_data, raw_data_len);
    }

    gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);

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
    if(raw_data_len == 0 || raw_data == NULL){
        gap_hal_ctx.adv_rsp_data_len = 0;
        memset(gap_hal_ctx.adv_rsp_data, 0, ADV_DATA_MAX_LEN);
        return QM_EOK;
    }else{
        gap_hal_ctx.adv_rsp_data_len = raw_data_len;
        memcpy(gap_hal_ctx.adv_rsp_data, raw_data, raw_data_len);
        QM_HEX_LOGD(LOG_TAG, "RSP ",gap_hal_ctx.adv_rsp_data, gap_hal_ctx.adv_rsp_data_len);
    }

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

    memcpy(&gap_hal_ctx.adv_params, adv_params, sizeof(qm_ble_adv_params_t));

    if(!gap_hal_ctx.adv_start){
        dev_manager_update_adv_interval(adv_obj_hdl, 
                                        BT_ADV_SCAN_UNIT(adv_params->adv_int_min),
                                        BT_ADV_SCAN_UNIT(adv_params->adv_int_max));

        dev_manager_start_adv(adv_obj_hdl, gap_hal_ctx.adv_data, gap_hal_ctx.adv_data_len, 
                                        gap_hal_ctx.adv_rsp_data, gap_hal_ctx.adv_rsp_data_len);
    }else{
        gap_hal_ctx.adv_update = QM_TRUE;
        dev_manager_update_adv_data(adv_obj_hdl, 
                            gap_hal_ctx.adv_data, gap_hal_ctx.adv_data_len, 
                            gap_hal_ctx.adv_rsp_data, gap_hal_ctx.adv_rsp_data_len);
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
    dev_manager_stop_adv(adv_obj_hdl);
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

    memcpy(&gap_hal_ctx.scan_params,scan_params, sizeof(qm_ble_scan_params_t));
    
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
    struct start_scan_param scan_param = {
        .scan_intv =  BT_ADV_SCAN_UNIT(gap_hal_ctx.scan_params.scan_interval),
        .scan_window = BT_ADV_SCAN_UNIT(gap_hal_ctx.scan_params.scan_window),
        .duration = duration / 10,
        .period = 0,
        .type = OBSERVER,
        .active = gap_hal_ctx.scan_params.scan_type,
        .filter_duplicates = 0,
    };
    dev_manager_start_scan(scan_obj_hdl, &scan_param);
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
    dev_manager_stop_scan(scan_obj_hdl);

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
    bool type;
    qm_bd_addr_t del_addr = {0};

    if(!memcmp(addr, del_addr, 6)){
        dev_manager_get_identity_bdaddr(gap_hal_ctx.mac,&type);
    }
    
    addr[0] = gap_hal_ctx.mac[5];
    addr[1] = gap_hal_ctx.mac[4];
    addr[2] = gap_hal_ctx.mac[3];
    addr[3] = gap_hal_ctx.mac[2];
    addr[4] = gap_hal_ctx.mac[1];
    addr[5] = gap_hal_ctx.mac[0];

    return QM_EOK;
}
