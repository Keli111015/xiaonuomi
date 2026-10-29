#include "qm_ble_client_core.h"
#include "qm_ble_client_transport.h"
#include "qm_ble_client_export.h"
#include "qm_ble_client_hal_ble.h"
#include "qm_ble_client_common.h"
#include "qm_errno.h"

extern int qm_ble_get_remain_conn_id(void *conn_handle, uint8_t *conn_id);
extern int qm_ble_is_connnected(uint8_t conn_id);
extern void *qm_ble_conn_handle_get(uint8_t conn_id);

static qm_ble_client_prodtst_cb m_prodtst_handler = NULL;

static qm_ble_client_status_changed_cb m_status_handler = NULL;

static void notify_status(uint8_t conn_id, qm_ble_client_event_t event)
{
    if (m_status_handler != NULL) {
        m_status_handler(conn_id, event);
    }
}

static void rx_handler(uint8_t conn_id, qm_ble_client_rx_cmd_post_t *rx_cmd)
{
    uint8_t cmd = rx_cmd ->cmd;
    switch(cmd){
    case QM_BLE_CLIENT_CMD_PRODTST:

        if(m_prodtst_handler != NULL){
            m_prodtst_handler(conn_id, rx_cmd->msg_id, rx_cmd->p_rx_buf + 1, rx_cmd->buf_sz - 1);
        }

    break;
    default:
        break;
    }
}

static void event_handler(uint8_t conn_id, qm_ble_client_event_info_t *p_event)
{
    switch (p_event->type) {
        case QM_BLE_CLIENT_EVENT_CONNECTED:
            notify_status(conn_id, p_event->type);
            break;

        case QM_BLE_CLIENT_EVENT_CONNECTE_FAILED:
            notify_status(conn_id, p_event->type);
            break;

        case QM_BLE_CLIENT_EVENT_DISCONNECTED:
            qm_ble_client_core_reset(conn_id);
            notify_status(conn_id, p_event->type);
            break;

        case QM_BLE_CLIENT_EVENT_RX_INFO:
            if(p_event->rx_data.p_data != NULL){
                qm_ble_client_rx_cmd_post_t *r_cmd = (qm_ble_client_rx_cmd_post_t*) p_event->rx_data.p_data;
                rx_handler(conn_id, r_cmd);
            }
            notify_status(conn_id, p_event->type);
            break;

        default:
            break;
    }
}


int qm_ble_client_init( qm_ble_client_status_changed_cb status_change_cb)
{
    qm_ble_client_init_t ble_client_init = {0};
    m_status_handler = status_change_cb;

    ble_client_init.event_handler = event_handler;

    return qm_ble_client_core_init(&ble_client_init);
}

int qm_ble_client_open(uint8_t mac[BD_ADDR_LEN], uint8_t *conn_id)
{   
    qm_err_t ret = QM_EOK;
    void *conn_handle = NULL;

    if(conn_id == NULL){
        return -QM_EINVAL;
    }

    conn_handle = qm_ble_client_connect(mac);
    if(conn_handle == NULL){
        return -QM_EIO;
    }

    ret = qm_ble_get_remain_conn_id(conn_handle, conn_id);
    if(ret != QM_EOK){
        return ret;
    }

    return QM_EOK;
}

void qm_ble_client_close(uint8_t conn_id)
{
    void *conn_handle = NULL;
    conn_handle = qm_ble_conn_handle_get(conn_id);
    if(conn_handle == NULL){
        return;
    }
    return qm_ble_client_disconnect(conn_handle);
}

int qm_ble_client_prodtst_register_callback(qm_ble_client_prodtst_cb prodtst_cb)
{
    m_prodtst_handler = prodtst_cb;
    return QM_EOK;
}

int qm_ble_client_prodtst_post(uint8_t conn_id, uint8_t *msg_id, uint8_t *buffer, uint32_t length)
{
    if(msg_id == NULL || buffer == NULL || length == 0){
        return -QM_EINVAL;
    }

    if(!qm_ble_is_connnected(conn_id)){
        return -QM_EIO;
    }

    return  qm_ble_client_transport_tx(conn_id, QM_BLE_CLIENT_TX_NOTIFICATION, msg_id, QM_BLE_CLIENT_CMD_PRODTST, buffer, length);
}
