
#include "qm_ble_hal_ble.h"
#include "qm_ble_service.h"
#include "qm_ble_common.h"
#include "qm_ble_core.h"
#include "qm_ble_bzopt.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_utils.h"


qm_ble_service_t g_qms;

#if QB_ENABLE_AUTH
extern qm_ble_auth_t  g_am_auth;
#endif

static void service_enabled(void)
{
    if (g_qms.is_indication_enabled && g_qms.is_notification_enabled) {
        QM_BLE_LOGD(QM_BLE_LOG_LIFECYCLE, "BLE service enabled");
#ifdef EN_QM_BLE_LONG_MTU
        qm_ble_trans_update_mtu();
#endif
#if QB_ENABLE_AUTH
    if( g_am_auth.auth_type != QB_AUTH_TYPE_NONE){
        qm_ble_auth_service_enabled();
    }else{
        qm_ble_core_event_notify(QB_EVENT_AUTHENTICATED, NULL, 0);
    }
#else
        qm_ble_core_event_notify(QB_EVENT_AUTHENTICATED, NULL, 0);
#endif
    }
}

static void connected()
{
    g_qms.conn_handle = BLE_CONN_HANDLE_MAGIC;
    g_qms.is_indication_enabled = false;
    g_qms.is_notification_enabled = false;

#if QB_ENABLE_AUTH
    if( g_am_auth.auth_type != QB_AUTH_TYPE_NONE){
        qm_ble_auth_connected();
    }
#endif
    qm_ble_core_event_notify(QB_EVENT_CONNECTED, NULL, 0);
}

static void disconnected()
{
    g_qms.conn_handle = BLE_CONN_HANDLE_INVALID;
    g_qms.is_indication_enabled = false;
    g_qms.is_notification_enabled = false;
    qm_ble_core_event_notify(QB_EVENT_DISCONNECTED, NULL, 0);
}

static void ic_ccc_handler(qms_ccc_value_t val)
{
    g_qms.is_indication_enabled = (val == QMS_CCC_VALUE_INDICATE ? true : false);
    if(g_qms.is_indication_enabled){
        service_enabled();
    }
}

static void nc_ccc_handler(qms_ccc_value_t val)
{
    g_qms.is_notification_enabled = (val == QMS_CCC_VALUE_NOTIFY ? true : false);
    if(g_qms.is_notification_enabled){
        service_enabled();
    }
}

static size_t wc_write_handler(void *buf, uint16_t len)
{
    qm_ble_transport_rx((uint8_t *)buf, len);
    return len;
}

static size_t wwnrc_write_handler(void *buf, uint16_t len)
{
    qm_ble_transport_rx((uint8_t *)buf, len);
    return len;
}

static size_t rc_read_handler(void *buf, uint16_t len)
{
    uint8_t mac[6] = {0};
    qm_ble_get_mac(mac);
    memcpy(buf, mac, 6);
    return 6;
}

qms_bt_init_t qms_attr_info = {
    /* service */
    QMS_UUID_DECLARE_16(BLE_UUID_QMS_SERVICE),
    /* rc */
    { .uuid          = QMS_UUID_DECLARE_16(BLE_UUID_QMS_RC),
      .prop          = QMS_GATT_CHRC_READ,
      .perm          = QMS_GATT_PERM_READ,
      .on_read       = rc_read_handler,
      .on_write      = NULL,
      .on_ccc_change = NULL },
    /* wc */
    { .uuid          = QMS_UUID_DECLARE_16(BLE_UUID_QMS_WC),
      .prop          = QMS_GATT_CHRC_READ | QMS_GATT_CHRC_WRITE,
      .perm          = QMS_GATT_PERM_READ | QMS_GATT_PERM_WRITE,
      .on_read       = NULL,
      .on_write      = wc_write_handler,
      .on_ccc_change = NULL },
    /* ic */
    { .uuid          = QMS_UUID_DECLARE_16(BLE_UUID_QMS_IC),
      .prop          = QMS_GATT_CHRC_READ | QMS_GATT_CHRC_INDICATE,
      .perm          = QMS_GATT_PERM_READ,
      .on_read       = NULL,
      .on_write      = NULL,
      .on_ccc_change = ic_ccc_handler },
    /* wwnrc */
    { .uuid          = QMS_UUID_DECLARE_16(BLE_UUID_QMS_WWNRC),
      .prop          = QMS_GATT_CHRC_READ | QMS_GATT_CHRC_WRITE_WITHOUT_RESP,
      .perm          = QMS_GATT_PERM_READ | QMS_GATT_PERM_WRITE,
      .on_read       = NULL,
      .on_write      = wwnrc_write_handler,
      .on_ccc_change = NULL },
    /* nc */
    { .uuid          = QMS_UUID_DECLARE_16(BLE_UUID_QMS_NC),
      .prop          = QMS_GATT_CHRC_READ | QMS_GATT_CHRC_NOTIFY,
      .perm          = QMS_GATT_PERM_READ,
      .on_read       = NULL,
      .on_write      = NULL,
      .on_ccc_change = nc_ccc_handler },
    connected,
    disconnected
};

uint32_t qm_ble_service_init(const qm_ble_service_init_t *p_qms_init)
{
    memset(&g_qms, 0, sizeof(qm_ble_service_t));
    g_qms.conn_handle = BLE_CONN_HANDLE_INVALID;
    g_qms.is_indication_enabled = false;
    g_qms.is_notification_enabled = false;
    g_qms.max_pkt_size = p_qms_init->mtu - 3;

    return qm_ble_stack_init(&qms_attr_info);
}

uint32_t qm_ble_service_send_notification(uint8_t *p_data, uint16_t length)
{
    int err;

    if (g_qms.conn_handle == BLE_CONN_HANDLE_INVALID ||
        g_qms.is_notification_enabled == false) {
        return QB_EINVALIDSTATE;
    }

    if (length > g_qms.max_pkt_size) {
        return QB_EDATASIZE;
    }

    err = qm_ble_send_notification(p_data, length);
    if (err) {
        return QB_EGATTNOTIFY;
    } else {
        return QB_SUCCESS;
    }
}

extern qm_ble_transport_t  g_qm_transport;
static void send_indication_done(uint8_t res)
{
    qm_ble_mutex_lock(&( g_qm_transport.tx.mutex_indicate_done), 1000);
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "sending indication done %d", res);
    if (res == QB_SUCCESS) {
        qm_ble_transport_txdone(1);
    }
    qm_ble_mutex_unlock(&( g_qm_transport.tx.mutex_indicate_done));
}

uint32_t qm_ble_service_send_indication(uint8_t *p_data, uint16_t length)
{
    int err;

    if (g_qms.conn_handle == BLE_CONN_HANDLE_INVALID ||
        g_qms.is_indication_enabled == false) {
        return QB_EINVALIDSTATE;
    }

    if (length > g_qms.max_pkt_size) {
        return QB_EDATASIZE;
    }
    err = qm_ble_send_indication(p_data, length, send_indication_done);
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "sending indication");
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "ble tx", p_data, length);

    if (err) {
        return QB_EGATTINDICATE;
    } else {
        return QB_SUCCESS;
    }
}
