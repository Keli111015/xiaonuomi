
#include "qm_ble_client_service.h"
#include "qm_ble_client_common.h"
#include "qm_ble_client_hal_ble.h"
#include "qm_ble_client_transport.h"
#include "qm_ble_client_core.h"

#include "qm_errno.h"

extern int qm_ble_del_conn_id(void *conn_handle);
extern void *qm_ble_conn_handle_get(uint8_t conn_id);
extern int qm_ble_client_connect_set(void *conn_handle, int is_connected);
extern int qm_ble_conn_id_get_by_conn_handle(void *conn_handle, uint8_t *conn_id);

static void connected(void *conn_handle)
{
    qm_err_t ret = QM_EOK;
    uint8_t conn_id = 0;

    ret = qm_ble_conn_id_get_by_conn_handle(conn_handle, &conn_id);
    if(ret != QM_EOK){
        return;
    }

    qm_ble_client_connect_set(conn_handle, 1);

    qm_ble_client_transport_init(conn_id, QM_BLE_CLIENT_GATT_MTU_SIZE_DEFAULT - 3, QM_BLE_CLIENT_TRANSPORT_TIMEOUT);

    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_CONNECTED, NULL, 0);
}

static void disconnected(void *conn_handle)
{
    qm_err_t ret = QM_EOK;
    uint8_t conn_id = 0;

    ret = qm_ble_conn_id_get_by_conn_handle(conn_handle, &conn_id);
    if(ret != QM_EOK){
        return;
    }
    qm_ble_client_connect_set(conn_handle, 0);
    qm_ble_del_conn_id(conn_handle);

    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_DISCONNECTED, NULL, 0);
}

static void connect_failed(void *conn_handle)
{
    qm_err_t ret = QM_EOK;
    uint8_t conn_id = 0;

    ret = qm_ble_conn_id_get_by_conn_handle(conn_handle, &conn_id);
    if(ret != QM_EOK){
        return;
    }
    qm_ble_del_conn_id(conn_handle);
    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_CONNECTE_FAILED, NULL, 0);
}


static int wwnrc_write_handler(void *conn_handle, void *buf, uint16_t len)
{
    qm_err_t ret = QM_EOK;
    uint8_t conn_id = 0;

    ret = qm_ble_conn_id_get_by_conn_handle(conn_handle, &conn_id);
    if(ret != QM_EOK){
        return 0;
    }

    qm_ble_client_transport_rx(conn_id, (uint8_t *)buf, len);
    return len;
}

qm_ble_client_service_init_t g_qm_ble_client_service_init = {
    /* service */
    BLE_UUID_SERVICE,
    /* wwnrc */
    { .uuid          = BLE_UUID_WWNRC,
      .ble_read       = NULL,
      .ble_write      = NULL
    },
    /* nc */
    { .uuid          = BLE_UUID_NC,
      .ble_read       = NULL,
      .ble_write      = wwnrc_write_handler
    },
    connected,
    connect_failed,
    disconnected
};

int qm_ble_client_service_send_notification(uint8_t conn_id, uint8_t *p_data, uint16_t length)
{
    void *conn_handle = NULL;
    conn_handle = qm_ble_conn_handle_get(conn_id);
    if(conn_handle == NULL){
        return 0;
    }
    return qm_ble_client_send_notification(conn_handle, p_data, length);

}

int qm_ble_client_service_send_indication(uint8_t conn_id, uint8_t *p_data, uint16_t length)
{

    return 0;
}

int qm_ble_client_service_init(void)
{
    return qm_ble_client_stack_init(&g_qm_ble_client_service_init);
}
