#include "qm_ble_gap.h"

#include "qm.h"
#include "qm_log.h"
#include "timer.h"
#include "qm_ringbuf.h"

#include "le/ble_api.h"
#include "le_common.h"

#include "jiffies.h"

#include "os/os_type.h"
#include "os/os_api.h"

#include "cui_env.h"
#include "qm_kv.h"

#include "qm_utils_timer.h"

#define LOG_TAG "hal"

#define CONFIG_BLE_GAP_NUM  (20)

typedef struct 
{
    qm_gap_ble_cb_event_t event;
    uint32_t len;
    uint8_t data[32];
}ble_gap_evet_node_t;

typedef struct 
{
    uint8_t adv_start;
    uint8_t scan_start;
    uint8_t scan_duration;
    qm_gap_ble_cb_t cb;

    qm_utils_time_t     event_timer;
    qm_gap_ble_cb_event_t old_event;
    ble_gap_evet_node_t event_node;

    ble_gap_evet_node_t event_buf[CONFIG_BLE_GAP_NUM];
    qm_ringbuf_t  ringbuf;
}qm_ble_gap_hal_t;

extern OS_MUTEX qm_noos_mutex;
static qm_ble_gap_hal_t gap_hal_ctx = {0};

void yt_gap_aciton(void)
{
    int ret = QM_EOK;
    
    if(gap_hal_ctx.old_event != gap_hal_ctx.event_node.event){
        if(!qm_utils_time_is_expired(&gap_hal_ctx.event_timer)){
            return;
        }
    }

    os_mutex_pend(&qm_noos_mutex, 0);
    ret = qm_ringbuf_pop(&gap_hal_ctx.ringbuf, &gap_hal_ctx.event_node, sizeof(ble_gap_evet_node_t));
    if(ret != sizeof(ble_gap_evet_node_t)){
        ret = qm_ringbuf_flush(&gap_hal_ctx.ringbuf);
        os_mutex_post(&qm_noos_mutex);
        return;
    }
    os_mutex_post(&qm_noos_mutex);

    switch (gap_hal_ctx.event_node.event)
    {
    case QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ble_op_set_adv_data(gap_hal_ctx.event_node.len, gap_hal_ctx.event_node.data);
    break;
    
    case QM_GAP_BLE_ADV_START_COMPLETE_EVT:
        ble_op_adv_enable(1);
    break;

    case QM_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ble_op_adv_enable(0);
    break;

    case QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ble_op_scan_enable2(0, 0);
    break;
#if 0
    case QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if(gap_hal_ctx.cb){
            gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
        }
    break;
#endif
    default:

    break;
    }

    gap_hal_ctx.old_event = gap_hal_ctx.event_node.event;
    qm_utils_time_countdown_ms(&gap_hal_ctx.event_timer, 100);
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


void user_ble_24g_adv_report_handle(const uint8_t* packet, uint16_t size) 
{
    adv_report_t* report_pt = NULL;
    qm_ble_gap_cb_param_t param = {0};
    os_mutex_pend(&qm_noos_mutex, 0);
    if (size == 0 || !gap_hal_ctx.cb || !gap_hal_ctx.scan_start) {
        os_mutex_post(&qm_noos_mutex);
        return;
    }
#if 0
    if(gap_hal_ctx.scan_duration){
        if(qm_utils_time_is_expired(&gap_hal_ctx.scan_timer)){
            gap_hal_ctx.scan_start = QM_FALSE;
            return;
        }
    }
#endif

    report_pt = (adv_report_t*)(&packet[2]);

    if(report_pt->length > (QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX)){
        os_mutex_post(&qm_noos_mutex);
        return;
    }

    param.scan_rst.rssi = report_pt->rssi;
    param.scan_rst.adv_data_len = report_pt->length;
    param.scan_rst.bda[0] = report_pt->address[5];
    param.scan_rst.bda[1] = report_pt->address[4];
    param.scan_rst.bda[2] = report_pt->address[3];
    param.scan_rst.bda[3] = report_pt->address[2];
    param.scan_rst.bda[4] = report_pt->address[1];
    param.scan_rst.bda[5] = report_pt->address[0];
    memcpy(param.scan_rst.ble_adv, report_pt->data, report_pt->length);
    
    
    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
    os_mutex_post(&qm_noos_mutex);
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

    qm_ringbuf_init(&gap_hal_ctx.ringbuf, gap_hal_ctx.event_buf, sizeof(ble_gap_evet_node_t) * CONFIG_BLE_GAP_NUM);
    ble_op_set_adv_param((80 * 8 / 5), ADV_NONCONN_IND, 0x07);//设置广播参数 时间是160*0.625ms
    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = NULL;
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
    ble_gap_evet_node_t event_node = {0};

    QM_RETURN_ON_FALSE(raw_data, -QM_EINVAL, LOG_TAG, "raw_data INVAL");
    QM_RETURN_ON_FALSE(raw_data_len, -QM_EINVAL, LOG_TAG, "raw_data_len INVAL");

    event_node.event = QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT;

    memset(event_node.data, 0, 32);
    event_node.len = raw_data_len;
    memcpy(event_node.data, raw_data, raw_data_len);

    ret = qm_ringbuf_push(&gap_hal_ctx.ringbuf, &event_node, sizeof(ble_gap_evet_node_t));
    if(ret != sizeof(ble_gap_evet_node_t)){
        printf("set_adv_data_raw ringbuf FULL!!");
        return -QM_ENOMEM;
    }

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
    int ret = QM_EOK;
    ble_gap_evet_node_t event_node = {0};

    QM_RETURN_ON_FALSE(adv_params, -QM_EINVAL, LOG_TAG, "ADV PARAM INVAL");

    if(gap_hal_ctx.adv_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.adv_start = QM_TRUE;

    event_node.event = QM_GAP_BLE_ADV_START_COMPLETE_EVT;

    ret = qm_ringbuf_push(&gap_hal_ctx.ringbuf, &event_node, sizeof(ble_gap_evet_node_t));
    if(ret != sizeof(ble_gap_evet_node_t)){
        qm_ringbuf_flush(&gap_hal_ctx.ringbuf);
        return -QM_ENOMEM;
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
    int ret = QM_EOK;
    ble_gap_evet_node_t event_node = {0};

    if(!gap_hal_ctx.adv_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.adv_start = QM_FALSE;


    event_node.event = QM_GAP_BLE_ADV_STOP_COMPLETE_EVT;

    ret = qm_ringbuf_push(&gap_hal_ctx.ringbuf, &event_node, sizeof(ble_gap_evet_node_t));
    if(ret != sizeof(ble_gap_evet_node_t)){
        qm_ringbuf_flush(&gap_hal_ctx.ringbuf);
        return -QM_ENOMEM;
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
#define SET_SCAN_TYPE       SCAN_PASSIVE
//搜索 周期大小
#define SET_SCAN_INTERVAL   (0x4) // unit: ms
//搜索 窗口大小
#define SET_SCAN_WINDOW     (0x4) // unit: ms

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    ble_gap_evet_node_t *event_node = NULL;

    QM_RETURN_ON_FALSE(scan_params, -QM_EINVAL, LOG_TAG, "SCAN PARAM INVAL");

    ble_op_set_scan_param(SET_SCAN_TYPE, SET_SCAN_INTERVAL, SET_SCAN_WINDOW);

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
    if(gap_hal_ctx.scan_start){
        return -QM_EINVAL;
    }

    gap_hal_ctx.scan_start = QM_TRUE;
    gap_hal_ctx.scan_duration = duration;

    ble_op_scan_enable2(1, 1);
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
    int ret = QM_EOK;
    ble_gap_evet_node_t event_node = {0};

    if(!gap_hal_ctx.scan_start){
        return -QM_EINVAL;
    }
    
    gap_hal_ctx.scan_start = QM_FALSE;

    event_node.event = QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT;

    ret = qm_ringbuf_push(&gap_hal_ctx.ringbuf, &event_node, sizeof(ble_gap_evet_node_t));
    if(ret != sizeof(ble_gap_evet_node_t)){
        qm_ringbuf_flush(&gap_hal_ctx.ringbuf);
        return -QM_ENOMEM;
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
    int addr_len = 6;
    uint8_t str_mac[6] = {0};
    
#if YT_BT_BLE_ENABLE
    le_controller_get_mac((void *)str_mac);
#else
	qm_kv_get("102", str_mac, &addr_len);  //BLE MAC KV ADDR
#endif

    addr[0] = str_mac[5];
    addr[1] = str_mac[4];
    addr[2] = str_mac[3];
    addr[3] = str_mac[2];
    addr[4] = str_mac[1];
    addr[5] = str_mac[0];
    return QM_EOK;
}