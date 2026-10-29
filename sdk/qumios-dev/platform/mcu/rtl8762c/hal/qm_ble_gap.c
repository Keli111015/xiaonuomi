#include "qm_ble_gap.h"

#include <gap.h>
#include <gap_adv.h>
#include <gap_scan.h>

#include "gap_scheduler.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_utils_timer.h"
#include "os_msg.h"
#include "miio_user_api.h"

#include "gap_vendor.h"

#define LOG_TAG "hal"
#define BLE_GAP_EVENT_NUM  50

typedef enum 
{
    QM_BLE_GAP_HAL_EVENT_SCAN_SET,
    QM_BLE_GAP_HAL_EVENT_SCAN_START,
    QM_BLE_GAP_HAL_EVENT_SCAN_STOP,
    QM_BLE_GAP_HAL_EVENT_SCAN_INFO,
    QM_BLE_GAP_HAL_EVENT_ADV_START,
    QM_BLE_GAP_HAL_EVENT_ADV_STOP,
    QM_BLE_GAP_HAL_EVENT_ADV_SET,
    QM_BLE_GAP_HAL_EVENT_MAX,
}qm_ble_gap_hal_even_t;


typedef struct 
{
    uint8_t data[31];
    uint16_t len;
}qm_ble_gap_hal_data_t;

typedef struct 
{
    qm_ble_gap_hal_even_t event;
    union 
    {
       qm_ble_gap_hal_data_t data;
       qm_ble_adv_params_t adv_params;
       qm_ble_gap_cb_param_t param_cb;
       qm_ble_scan_params_t scan_params;
    }hal_params;
}qm_ble_gap_hal_queue_t;

typedef struct 
{
    uint8_t xj_event;
    void *event_queue;
    void *gap_queue;

    uint8_t adv_start;
    uint8_t scan_start;
    uint8_t mesh_start;
    qm_gap_ble_cb_t cb;
    qm_timer_t scan_timer;
}qm_ble_gap_hal_t;

extern bool_t mi_pair_complete;
extern uint8_t miio_enter_pair_notify_register(void (*notify)(void));

static qm_ble_gap_hal_t qm_ble_gap_hal_ctx = {0};


static void pair_notify(void)
{
    if(qm_ble_gap_hal_ctx.scan_start){
        miio_mesh_set_scan_level(2);
    }
}

void xj_mesh_start(uint8_t xj_event, void *event_queue)
{
    qm_ble_gap_hal_ctx.xj_event = xj_event;
    qm_ble_gap_hal_ctx.event_queue = event_queue;
    os_msg_queue_create(&qm_ble_gap_hal_ctx.gap_queue, BLE_GAP_EVENT_NUM, sizeof(qm_ble_gap_hal_queue_t));  
    qm_ble_gap_hal_ctx.mesh_start = QM_TRUE;  
    miio_enter_pair_notify_register(pair_notify);
}


void qm_ble_gap_event_notify(void)
{
    qm_ble_gap_hal_queue_t msg = {0};
    if (os_msg_recv(qm_ble_gap_hal_ctx.gap_queue, &msg, 0) != true){
        return;
    }

    switch (msg.event)
    {
        case QM_BLE_GAP_HAL_EVENT_SCAN_SET:
        {
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
            }
        }break;
        case QM_BLE_GAP_HAL_EVENT_SCAN_START:
        {
            miio_mesh_set_scan_level(4);
            qm_ble_gap_hal_ctx.scan_start = QM_TRUE;

            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
            }
        }break;
        case QM_BLE_GAP_HAL_EVENT_SCAN_STOP:
            miio_mesh_set_scan_level(0);
            qm_ble_gap_hal_ctx.scan_start = QM_FALSE;

            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
            }
        break;

        case QM_BLE_GAP_HAL_EVENT_ADV_START:
        {
            static int is_init = 1;
            uint8_t  adv_evt_type = GAP_ADTYPE_ADV_IND;
            uint8_t  adv_chann_map = GAP_ADVCHAN_ALL;
            uint16_t adv_int_min = msg.hal_params.adv_params.adv_int_min;
            uint16_t adv_int_max = msg.hal_params.adv_params.adv_int_max;
            
            if(is_init){
                is_init = 0;
                miio_system_set_adv_timeout(0); //进入产测后用于关闭米家的mesh广播
                le_adv_set_param(GAP_PARAM_SCAN_RSP_DATA, 0, NULL);
            }

            //Set advertising parameters
            le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
            le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
            le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
            le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);

            if(!qm_ble_gap_hal_ctx.adv_start){
                le_adv_start();
                qm_ble_gap_hal_ctx.adv_start = QM_TRUE;
            }

            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
            }
        }
        break;
        case QM_BLE_GAP_HAL_EVENT_ADV_STOP:
            if(qm_ble_gap_hal_ctx.adv_start){
                le_adv_stop();
                qm_ble_gap_hal_ctx.adv_start = QM_FALSE;
            }
            
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
            }
        break;
        case QM_BLE_GAP_HAL_EVENT_ADV_SET:

            le_adv_set_param(GAP_PARAM_ADV_DATA, msg.hal_params.data.len,(void *)msg.hal_params.data.data); 
            
            if(qm_ble_gap_hal_ctx.cb){
                qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
            }
        break;
        default:
        break;
    }
}

/**
 * @brief 该函数由底层调用，使用例子如下：
 *
    //Received advertising or scan rsp data will be handled in app_gap_callback
    T_APP_RESULT app_gap_callback(uint8_t cb_type, void *p_cb_data)
    {
        T_APP_RESULT result = APP_RESULT_SUCCESS;
        T_LE_CB_DATA cb_data;
        memcpy(&cb_data, p_cb_data, sizeof(T_LE_CB_DATA));
        APP_PRINT_TRACE1("app_gap_callback: cb_type = %d", cb_type);
        switch (cb_type)
        {
        case GAP_MSG_LE_SCAN_INFO:
            APP_PRINT_INFO5("GAP_MSG_LE_SCAN_INFO:adv_type 0x%x, bd_addr %s, remote_addr_type %d, rssi %d, data_len %d",
                            cb_data.p_le_scan_info->adv_type,
                            TRACE_BDADDR(cb_data.p_le_scan_info->bd_addr),
                            cb_data.p_le_scan_info->remote_addr_type,
                            cb_data.p_le_scan_info->rssi,
                            cb_data.p_le_scan_info->data_len);
           qm_ble_gap_hal_scan_info(cb_data.p_le_scan_info);
           break;
        }
    }
 *
 *
 */

void qm_ble_gap_hal_scan_info(T_LE_SCAN_INFO *ble_scan_info)
{

    qm_ble_gap_hal_queue_t queue = {0};


    if(qm_ble_gap_hal_ctx.cb && qm_ble_gap_hal_ctx.scan_start){
        if(!qm_ble_gap_hal_ctx.mesh_start){
            return;
        }

        queue.hal_params.param_cb.scan_rst.rssi = ble_scan_info->rssi;
        queue.hal_params.param_cb.scan_rst.adv_data_len = ble_scan_info->data_len;
        queue.hal_params.param_cb.scan_rst.bda[0] = ble_scan_info->bd_addr[5];
        queue.hal_params.param_cb.scan_rst.bda[1] = ble_scan_info->bd_addr[4];
        queue.hal_params.param_cb.scan_rst.bda[2] = ble_scan_info->bd_addr[3];
        queue.hal_params.param_cb.scan_rst.bda[3] = ble_scan_info->bd_addr[2];
        queue.hal_params.param_cb.scan_rst.bda[4] = ble_scan_info->bd_addr[1];
        queue.hal_params.param_cb.scan_rst.bda[5] = ble_scan_info->bd_addr[0];
        memcpy(queue.hal_params.param_cb.scan_rst.ble_adv, ble_scan_info->data, ble_scan_info->data_len);
        
        qm_ble_gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &queue.hal_params.param_cb);
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
    return -QM_EINVAL; //暂不实现
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
    qm_ble_gap_hal_queue_t queue = {0};

    if(raw_data == NULL || raw_data_len == 0){
        return -QM_EINVAL;
    }

    queue.event = QM_BLE_GAP_HAL_EVENT_ADV_SET;
    queue.hal_params.data.len = raw_data_len;
    memcpy(&queue.hal_params.data.data[0], &raw_data[0], raw_data_len);

    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }

    return ret;
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
    qm_ble_gap_hal_queue_t queue = {0};

    if(adv_params == NULL){
        return -QM_EINVAL;
    }

    queue.event = QM_BLE_GAP_HAL_EVENT_ADV_START;
    memcpy(&queue.hal_params.adv_params, adv_params, sizeof(qm_ble_adv_params_t));

    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }
    return ret;
}

/**
 * @brief           This function is called to stop advertising.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_advertising(void)
{
    int ret = QM_EOK;
    qm_ble_gap_hal_queue_t queue = {0};

    queue.event = QM_BLE_GAP_HAL_EVENT_ADV_STOP;
    
    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }

    return ret;
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
    int ret = QM_EOK;
    qm_ble_gap_hal_queue_t queue = {0};

    if(scan_params == NULL){
        return -QM_EINVAL;
    }

    if(mi_pair_complete){
        return QM_EOK;
    }

    queue.event = QM_BLE_GAP_HAL_EVENT_SCAN_SET;
    memcpy(&queue.hal_params.scan_params, scan_params, sizeof(qm_ble_scan_params_t));

    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }

    return ret;
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
    int ret = QM_EOK;
    qm_ble_gap_hal_queue_t queue = {0};

    if(mi_pair_complete){
        return QM_EOK;
    }

    queue.event = QM_BLE_GAP_HAL_EVENT_SCAN_START;
    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }

    return ret;
}

/**
 * @brief    This function call to stop the device scanning the peer device which advertising on the air
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_scanning(void)
{
    int ret = QM_EOK;
    qm_ble_gap_hal_queue_t queue = {0};

    if(mi_pair_complete){
        return QM_EOK;
    }
    
    queue.event = QM_BLE_GAP_HAL_EVENT_SCAN_STOP;
    if (!os_msg_send(qm_ble_gap_hal_ctx.gap_queue, &queue, 0)){
        return -QM_EINVAL;
    }

    /* send event to notify app task */
    if (!os_msg_send(qm_ble_gap_hal_ctx.event_queue, &qm_ble_gap_hal_ctx.xj_event, 0)){
        return -QM_EINVAL;
    }
    
    return ret;
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
    uint8_t str_mac[6] = {0};
    gap_get_param(GAP_PARAM_BD_ADDR, str_mac);
    addr[0] = str_mac[5];
    addr[1] = str_mac[4];
    addr[2] = str_mac[3];
    addr[3] = str_mac[2];
    addr[4] = str_mac[1];
    addr[5] = str_mac[0];
    return QM_EOK;
}