#ifndef QB_CLEINT_TRANSPORT_H
#define QB_CLEINT_TRANSPORT_H

#include "qm_ble_client_common.h"
#include "qm_types.h"
#include "qm_kernel.h"

#ifndef CONFIG_QM_BLE_CONN_MAX_NUM 
#define CONFIG_QM_BLE_CONN_MAX_NUM   8
#endif

enum {
    QM_BLE_CLIENT_TX_NOTIFICATION,
    QM_BLE_CLIENT_TX_INDICATION,
};

typedef int (*ble_transport_tx_func_t)(uint8_t conn_id, uint8_t *p_data, uint16_t length);

#define QM_BLE_CLIENT_TX_BUFF_LEN (QM_BLE_MAX_SUPPORTED_MTU - 3)
#define QM_BLE_CLIENT_RX_BUFF_LEN QM_BLE_CLIENT_MAX_PAYLOAD_SIZE
typedef struct transport_s {
    struct {
        uint8_t buff[QM_BLE_CLIENT_TX_BUFF_LEN];
        uint8_t *data;
        uint16_t len;
        uint16_t bytes_sent;
        uint8_t encrypted;
        uint8_t msg_id;
        uint8_t cmd;
        uint8_t total_frame;
        uint8_t frame_seq;
        uint8_t pad_len;
        uint16_t pkt_req;
        uint16_t pkt_cfm;
        qm_timer_t timer;
        ble_transport_tx_func_t active_func;
    } tx;
    struct {
        uint8_t buff[QM_BLE_CLIENT_RX_BUFF_LEN];
        uint16_t buff_size;
        uint16_t bytes_received;
        uint8_t msg_id;
        uint8_t cmd;
        uint8_t total_frame;
        uint8_t frame_seq;
        qm_timer_t timer;
    } rx;
    uint8_t tx_msg_id;
    uint16_t max_pkt_size;
    uint16_t timeout;
} qm_ble_client_transport_t;

typedef struct{
    uint8_t msg_id;
    uint8_t cmd;
    uint8_t frame_seq;
    uint8_t *p_rx_buf;
    uint16_t buf_sz;
}qm_ble_client_rx_cmd_post_t;

typedef struct {

    void *conn_handle;
    int is_connected;
    qm_ble_client_transport_t  ble_transport;
    qm_ble_client_rx_cmd_post_t  rx_cmd_post;

}qm_ble_client_ctx_t;

int qm_ble_client_transport_init(uint8_t conn_id, uint16_t max_mtu, uint32_t transport_timeout);
void qm_ble_client_transport_reset(uint8_t conn_id);
int qm_ble_client_transport_tx(uint8_t conn_id, uint8_t tx_type, uint8_t *msg_id, uint8_t cmd, uint8_t const *const p_data, uint16_t length);
void qm_ble_client_transport_rx(uint8_t conn_id, uint8_t *p_data, uint16_t length);



#endif
