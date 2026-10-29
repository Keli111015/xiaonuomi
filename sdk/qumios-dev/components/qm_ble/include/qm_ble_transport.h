#ifndef QB_TRANSPORT_H
#define QB_TRANSPORT_H

#include "qm_ble_common.h"
#include "qm_ble_hal_os.h"
#include "qm_ble_bzopt.h"

#ifdef __cplusplus
extern "C"
{
#endif

enum {
    QB_TX_NOTIFICATION,
    QB_TX_INDICATION,
};

typedef uint32_t (*transport_tx_func_t)(uint8_t *p_data, uint16_t length);

#define TX_BUFF_LEN (QB_MAX_SUPPORTED_MTU - 3)
#define RX_BUFF_LEN QB_MAX_PAYLOAD_SIZE
typedef struct transport_s {
    struct {
        uint8_t buff[TX_BUFF_LEN];
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
        qm_ble_timer_t timer;
        transport_tx_func_t active_func;
        qm_ble_mutex_t mutex_indicate_done;
    } tx;
    struct {
        uint8_t buff[RX_BUFF_LEN];
        uint16_t buff_size;
        uint16_t bytes_received;
        uint8_t msg_id;
        uint8_t cmd;
        uint8_t total_frame;
        uint8_t frame_seq;
        qm_ble_timer_t timer;
    } rx;
    uint8_t tx_msg_id;
    uint16_t max_pkt_size;
    void *p_key;
    uint16_t timeout;
    void *p_aes_ctx;
} qm_ble_transport_t;

typedef struct{
    uint8_t msg_id;
    uint8_t cmd;
    uint8_t frame_seq;
    uint8_t *p_rx_buf;
    uint16_t buf_sz;
}qm_ble_rx_cmd_post_t;

qm_ble_ret_code_t qm_ble_transport_deinit(void);
qm_ble_ret_code_t qm_ble_transport_init(qm_ble_init_t const *p_init);
void qm_ble_transport_reset(void);
qm_ble_ret_code_t qm_ble_transport_tx(uint8_t tx_type, uint8_t *msg_id, uint8_t cmd,
                        uint8_t const *const p_data, uint16_t length);
void qm_ble_transport_txdone(uint16_t pkt_sent);
void qm_ble_transport_rx(uint8_t *p_data, uint16_t length);
uint32_t qm_ble_transport_update_key(uint8_t *p_key);
uint8_t qm_ble_tx_msg_id_get(void);
#ifdef EN_QM_BLE_LONG_MTU
uint32_t qm_ble_trans_update_mtu(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // QB_TRANSPORT_H
