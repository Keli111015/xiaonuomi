
#include "qm.h"
#include "qm_ble_gap.h"


#define LOG_TAG "EXAMPLE"

#define ADV_TIMEOUT_30S     (10 * 1000)
#define ADV_INTERVAL_100MS   (160 * 625 / 1000)

#define SCAN_MIN_RSSI       (-60)
#define SCAN_WINDOWS_MS     (100 * 8 / 5)  // 100MS
#define SCAN_INTERVAL_MS    (200 * 8 / 5)


static qm_task_t adv_task = {0};


static void adv_thread(void *arg)
{
    int count = 0;
    uint8_t adv_data[] = {0x04,0xff, 0x65, 0x43, 0x21};

    qm_msleep(10 * 1000);
    qm_ble_gap_stop_advertising();
    qm_msleep(10 * 1000);
    qm_ble_gap_set_adv_data_raw(adv_data, sizeof(adv_data));
    qm_msleep(10 * 1000);
    qm_ble_gap_stop_advertising();
    
    while (1)
    {
       qm_msleep(10 * 1000);
    }

}

static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    qm_ble_adv_params_t adv_param = {
        .adv_int_max = ADV_INTERVAL_100MS,
        .adv_int_min = ADV_INTERVAL_100MS,
    };

    switch (event)
    {
        case QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            QM_LOGD(LOG_TAG, "QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
            qm_ble_gap_start_scanning(0);
        break; 
        case QM_GAP_BLE_SCAN_RESULT_EVT:
        {   
            QM_LOGD(LOG_TAG, "QM_GAP_BLE_SCAN_RESULT_EVT");
            if(param->scan_rst.rssi < SCAN_MIN_RSSI){
                return;
            }
            QM_HEX_LOGD(LOG_TAG, "ADDR ", param->scan_rst.bda, 6);
            QM_HEX_LOGD(LOG_TAG, "DATA ", param->scan_rst.ble_adv, param->scan_rst.adv_data_len);
        }
        break;

        case QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        case QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            QM_LOGD(LOG_TAG, "QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT");
            qm_ble_gap_start_advertising(&adv_param);
        break;

        default:
        
        break;
    }
}


void qm_application_start(void)
{
    int ret = QM_EOK;
    uint8_t adv_data[] = {0x06,0xff, 0x11, 0x22, 0x33, 0x44, 0x55};

    qm_ble_scan_params_t scan_param = {
        .scan_type = QM_BLE_SCAN_TYPE_PASSIVE,
        .scan_interval = SCAN_INTERVAL_MS,
        .scan_window = SCAN_WINDOWS_MS,
    };

    ret = qm_ble_gap_register_callback(qm_gap_ble_callback);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "qm_ble_gap_register_callback FAIL %d", ret);
        while (1);
    }

    ret = qm_ble_gap_set_scan_params(&scan_param);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "qm_ble_gap_set_scan_params FAIL %d", ret);
        while (1);
    }

    ret = qm_ble_gap_set_adv_data_raw(adv_data, sizeof(adv_data));
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "qm_ble_gap_set_scan_params FAIL %d", ret);
        while (1);
    }

    qm_task_new(&adv_task, "ADV", adv_thread, NULL, 2048, 4);
}

