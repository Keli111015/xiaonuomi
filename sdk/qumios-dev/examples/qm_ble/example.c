#include "qm_ble_export.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"


#define LOG_TAG "EXAMPLE"

#define NOTTY_PRODTST_CLIENT_BLE_ADV_NAME "xjiang_prodtst"

static void qm_apinfo_ready_cb(qm_ble_apinfo_t *ap);
static void qm_ble_status_change_cb(qm_ble_event_t event);
static void qm_extinfo_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t len);
static void qm_ble_response_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);
static void qm_ble_set_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);
static void qm_ble_get_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);


static int qm_ble_prodtst_fn(uint8_t msg_id, uint8_t *buffer, uint32_t length)
{

    if(buffer == NULL || length == 0){
        return -QM_EINVAL;
    }

    QM_HEX_LOGD(LOG_TAG, "Prodtst_DATA RECV ", buffer, length);
    QM_LOGD(LOG_TAG, "Prodtst_DATA RECV %s", buffer);

    qm_ble_prodtst_post(0, buffer, length);
    return QM_EOK;
}

static void qm_ble_status_change_cb(qm_ble_event_t event)
{
    switch (event) {
        case QM_BLE_CONNECTED:{
            QM_BLE_INFO("Prodtst_Connected");
		}break;
        case QM_BLE_DISCONNECTED:
            QM_BLE_INFO("Prodtst_Disconnected RESTART BLE ADV");
            qm_ble_restart_advertising();
        break;
        case QM_BLE_AUTHENTICATED:
            QM_BLE_INFO("Prodtst_Authenticated");
            break;
        default:
            break;
    }
}

static void qm_ble_set_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{

}

static void qm_ble_get_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{

}

static void qm_ble_response_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{
 
}

static void qm_extinfo_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t len)
{

}

static void qm_apinfo_ready_cb(qm_ble_apinfo_t *ap)
{

}   

void qm_application_start(void)
{
    int ret = QM_EOK;
    qm_ble_dev_info_t dinfo = {0};
    uint8_t is_ble_adv = QM_FALSE;
    uint8_t is_ble_init = QM_FALSE;

    char product_secret[] = "838d26a8c9ea421dbd40dad319445b3a";

    dinfo.product_id = 1234;
    dinfo.product_secret = product_secret;
    dinfo.device_id = NULL;
    dinfo.dev_adv_mac = NULL;

    ret = qm_ble_init(&dinfo,
                 qm_ble_status_change_cb,
                 qm_ble_set_dev_status_callback,
                 qm_ble_get_dev_status_callback,
                 qm_ble_response_callback,
                 qm_extinfo_callback,
                 qm_apinfo_ready_cb,
                 NULL);

    if (ret != QM_EOK){
        goto _error;
    }

    is_ble_init = QM_TRUE;
    ret = qm_ble_start_advertising(0, 0, 0, NOTTY_PRODTST_CLIENT_BLE_ADV_NAME);
    if (ret != QM_EOK){
        goto _error;
    }

    is_ble_adv = QM_TRUE;
    qm_ble_prodtst_register_callback(qm_ble_prodtst_fn);

    return ; 

_error: 

    if(is_ble_adv){
        qm_ble_stop_advertising();
    }
    
    if(is_ble_init){
        qm_ble_end();
    }
}

