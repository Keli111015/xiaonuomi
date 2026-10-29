#include "qm.h"
#include "qm_log.h"
#include "prodtst_ble_server.h"
#include "qm_ble_gap.h"

#define LOG_TAG "example"

static int32_t prodtst_ble_slave_event_notify(prodtst_ble_server_event_type_t event_type, prodtst_ble_server_event_info_t *p_event)
{

    if(p_event == NULL){
        return -QM_EINVAL;
    }

    switch (event_type)
    {
        case PRODTST_BLE_SERVER_CONNECT:
            QM_LOGD(LOG_TAG, "PRODTST_BLE_SERVER_CONNECT ID %d ADDR:%02X:%02X:%02X:%02X:%02X:%02X!!!", 
                    p_event->conn_info.conn_id, 
                    p_event->conn_info.conn_addr[0],p_event->conn_info.conn_addr[1],p_event->conn_info.conn_addr[2],
                    p_event->conn_info.conn_addr[3],p_event->conn_info.conn_addr[4],p_event->conn_info.conn_addr[5]);
        break;
        case PRODTST_BLE_SERVER_TEST:
            QM_LOGD(LOG_TAG, "PRODTST_BLE_CLIENT_TEST: ID %d data: %s !!!", p_event->test_info.gen_info.test_id, p_event->test_info.gen_info.pdata);
        break;
        case PRODTST_BLE_SERVER_REPORT:
            QM_LOGD(LOG_TAG, "PRODTST_BLE_SERVER_REPORT ID %d data: %s !!!", p_event->test_info.gen_info.test_id, p_event->test_info.gen_info.pdata);
        break;
        default:
        break;
    }
    return QM_EOK;
}


void qm_application_start(void)
{
    prodtst_ble_server_param_t server_param = {
        .ble_param = {
            .pid = 1,
            .encrypted = 0,
            .min_rssi  = -60,
            .data_type = PRODTST_BLE_DATA_TYPE_AT,
        },
        .channel_type = PRODTST_BLE_TYPE_CONN,
    };
		
    prodtst_ble_server_init(&server_param);
    prodtst_ble_server_event_register(prodtst_ble_slave_event_notify);

    prodtst_ble_prodtst_t factory = {
        .prodtst_status = 1,
        .prodtst_type = PRODTST_BLE_TYPE_CONN,
    };

    uint8_t mac[6] = {0};
    prodtst_ble_server_prodtst_status_set(mac, factory);
    prodtst_ble_server_prodtst_status_set(mac, factory);
    prodtst_ble_server_prodtst_status_set(mac, factory);

    qm_msleep(1000);
    prodtst_ble_server_test(1, 1, (uint8_t *)"AT+FW_NAME?", strlen("AT+FW_NAME?"));
    prodtst_ble_server_test(2, 1, (uint8_t *)"AT+FW_NAME?", strlen("AT+FW_NAME?"));
    prodtst_ble_server_test(3, 1, (uint8_t *)"AT+FW_NAME?", strlen("AT+FW_NAME?"));
    qm_msleep(1000);

    prodtst_ble_server_test(1, 2, (uint8_t *)"AT+MAC_BLE?", strlen("AT+MAC_BLE?"));
    prodtst_ble_server_test(2, 2, (uint8_t *)"AT+MAC_BLE?", strlen("AT+MAC_BLE?"));
    prodtst_ble_server_test(3, 2, (uint8_t *)"AT+MAC_BLE?", strlen("AT+MAC_BLE?"));
    qm_msleep(1000);
}