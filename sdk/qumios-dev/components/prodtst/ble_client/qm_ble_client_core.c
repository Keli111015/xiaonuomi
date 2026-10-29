#include "qm_ble_client_core.h"
#include "qm_ble_client_transport.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG  "client core"

static qm_ble_client_event_handler_t client_event_handler = NULL;

void qm_ble_client_core_event_notify(uint8_t conn_id, uint8_t event_type, uint8_t *data, uint16_t length)
{
    qm_ble_client_event_info_t event;

    event.type = event_type;
    event.rx_data.p_data = data;
    event.rx_data.length = length;
    client_event_handler(conn_id, &event);
}

int qm_ble_client_core_init(qm_ble_client_init_t *ble_client_init)
{
    client_event_handler = ble_client_init->event_handler;
    return qm_ble_client_service_init();
}


void qm_ble_client_core_reset(uint8_t conn_id)
{
    qm_ble_client_transport_reset(conn_id);
}

void qm_ble_client_core_handle_err(uint8_t conn_id, uint8_t src, uint8_t code)
{
    QM_LOGE(LOG_TAG, "conn_id: %d, err at 0x%04x, code 0x%04x", conn_id, src, code);
    switch (src & QM_BLE_CLIENT_ERR_MASK) {
        case QM_BLE_CLIENT_TRANS_ERR:
            if (code != QM_BLE_CLIENT_EINTERNAL) {
                if (src == QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_FW_DATA_DISC) {
                    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_DISCONNECTED, NULL, 0);
                }
                if(src == QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_TX_TIMER){
                    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_TX_DONE_TIMEOUT, NULL, 0);
                }
            }
            break;
        case QM_BLE_CLIENT_EXTCMD_ERR:
            QM_LOGE(LOG_TAG, "QB_EXTCMD_ERR");
            break;

        default:
            QM_LOGE(LOG_TAG, "unknow bz err\r\n");
            break;
    }
}


