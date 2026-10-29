
#include "qm_ble_bzopt.h"
#include "qm_ble_transport.h"
#include "qm_ble_core.h"
#include "qm_ble_common.h"
#include "qm_ble_utils.h"
#include "qm_ble_service.h"

#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_sec.h"
#include "qm_ble_hal_os.h"

#define HEADER_SIZE 4
#define AES_BLK_SIZE 16

#define IS_ENC(data) ((data[0] & 0x10) != 0)
#define MSG_ID(data) (data[0] & 0xf)
#define CMD_TYPE(data) (data[1])
#define TOTAL_FRAME(data) ((data[2] >> 4) & 0x0f)
#define FRAME_SEQ(data) (data[2] & 0x0f)
#define FRAME_LEN(data) (data[3])

qm_ble_transport_t  g_qm_transport;
qm_ble_rx_cmd_post_t  qm_rx_cmd_post;

static void reset_tx(void)
{
     g_qm_transport.tx.len = 0;
     g_qm_transport.tx.bytes_sent = 0;
     g_qm_transport.tx.msg_id = 0;
     g_qm_transport.tx.cmd = 0;
     g_qm_transport.tx.total_frame = 0;
     g_qm_transport.tx.frame_seq = 0;
     g_qm_transport.tx.pkt_req = 0;
     g_qm_transport.tx.pkt_cfm = 0;
    if ( g_qm_transport.timeout != 0) {
        qm_ble_timer_stop(& g_qm_transport.tx.timer);
    }
}

static void reset_rx(void)
{
     g_qm_transport.rx.cmd = 0;
     g_qm_transport.rx.total_frame = 0;
     g_qm_transport.rx.frame_seq = 0;
     g_qm_transport.rx.bytes_received = 0;
    if ( g_qm_transport.timeout != 0) {
        qm_ble_timer_stop(& g_qm_transport.rx.timer);
    }
}

static void on_tx_timeout(void *arg)
{
    QM_BLE_LOGE("tx timeout");
    reset_tx();
    qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_TX_TIMER, QB_ETIMEOUT);
}

static void on_rx_timeout(void *arg)
{
    QM_BLE_LOGE("rx timeout");
    reset_rx();
}

static bool_t is_valid_rx_command(uint8_t cmd) {
    if (cmd == QM_BLE_CMD_AUTH ||
        cmd == QM_BLE_CMD_INIT ||
        cmd == QM_BLE_CMD_SET ||
        cmd == QM_BLE_CMD_GET ||
        cmd == QM_BLE_CMD_ENTER_BACKGROUD ||
        cmd == QM_BLE_CMD_GET_SNAPSHOT ||
        cmd == QM_BLE_CMD_REPORT ||
        cmd == QM_BLE_CMD_SET_RAND ||
        cmd == QM_BLE_CMD_BIND_NOTIFY ||
        cmd == QM_BLE_CMD_APINFO ||
        cmd == QM_BLE_CMD_OTA_QUERY_VER ||
        cmd == QM_BLE_CMD_OTA_REQUEST ||
        cmd == QM_BLE_CMD_OTA_DATA ||
        cmd == QM_BLE_CMD_OTA_VERIFY ||
        cmd == QM_BLE_CMD_PRODTST) {
        return true;
    }
    return false;
}

static bool_t is_valid_tx_command(uint8_t cmd) {
    if (cmd == QM_BLE_CMD_AUTH ||
        cmd == QM_BLE_CMD_INIT ||
        cmd == QM_BLE_CMD_SET ||
        cmd == QM_BLE_CMD_REPORT ||
        cmd == QM_BLE_CMD_GET ||
        cmd == QM_BLE_CMD_ENTER_BACKGROUD ||
        cmd == QM_BLE_CMD_GET_SNAPSHOT||
        cmd == QM_BLE_CMD_BIND_NOTIFY ||
        cmd == QM_BLE_CMD_APINFO ||
        cmd == QM_BLE_CMD_OTA_QUERY_VER ||
        cmd == QM_BLE_CMD_OTA_REQUEST ||
        cmd == QM_BLE_CMD_OTA_DATA ||
        cmd == QM_BLE_CMD_OTA_VERIFY ||
        cmd == QM_BLE_CMD_PRODTST) {
        return true;
    }
    return false;
}

static void do_encrypt(uint8_t *data, uint16_t len)
{
    uint16_t bytes_to_pad, blk_num = len >> 4;
    uint8_t encrypt_data[QB_FRAME_SIZE_MAX];
    if(len > QB_FRAME_SIZE_MAX){
        QM_BLE_LOGE("[BZ encry] data PDU length exceed");
        return;
    }

    bytes_to_pad = (AES_BLK_SIZE - len % AES_BLK_SIZE) % AES_BLK_SIZE;
    if (bytes_to_pad) {
        memset(data + len, bytes_to_pad, bytes_to_pad);
         g_qm_transport.tx.pad_len = bytes_to_pad;
        blk_num++;
         g_qm_transport.tx.buff[3] += bytes_to_pad;
    }
    
    QM_BLE_LOGD(QM_BLE_LOG_PAYLOAD, "aes input blocks: %d", blk_num);
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "aes input", data, len);
    qm_ble_aes128_cbc_encrypt( g_qm_transport.p_aes_ctx, data, blk_num, encrypt_data);
    memcpy(data, encrypt_data, blk_num << 4);
    QM_BLE_LOGD(QM_BLE_LOG_PAYLOAD, "aes output ready");
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "aes output", encrypt_data, blk_num << 4);
}

static void do_decrypt(uint8_t *data, uint16_t *len)
{
    uint16_t blk_num = *len >> 4;
    uint16_t pad_len;
    uint8_t decrypt_data[QB_FRAME_SIZE_MAX] = {0};
    
    if(*len > QB_FRAME_SIZE_MAX){
        QM_BLE_LOGE("[BZ decry] data PDU length exceed");
        return;
    }
    
    qm_ble_aes128_cbc_decrypt( g_qm_transport.p_aes_ctx, data, blk_num, decrypt_data);
    pad_len = (uint16_t)decrypt_data[*len - 1];

    if (pad_len < 1 || pad_len > 16) {
        QM_BLE_LOGE("[BZ decry] pad len 1<-<16");
    } else {
        *len -= pad_len;
    }
    memcpy(data, decrypt_data, *len);
   
}

static uint32_t build_packet(uint8_t *data, uint16_t len)
{
    uint32_t ret = QB_SUCCESS;

     g_qm_transport.tx.pad_len = 0;
     g_qm_transport.tx.buff[0] = ((QB_TRANSPORT_VER & 0x7) << 5) |
                             (( g_qm_transport.tx.encrypted & 0x1) << 4) |
                             ( g_qm_transport.tx.msg_id & 0xF);
     g_qm_transport.tx.buff[1] =  g_qm_transport.tx.cmd;
     g_qm_transport.tx.buff[2] = (( g_qm_transport.tx.total_frame & 0x0F) << 4) |
                             ( g_qm_transport.tx.frame_seq & 0x0F);
     g_qm_transport.tx.buff[3] = len;

    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "frame len (%d)",  g_qm_transport.tx.buff[3]);

    /* Payload */
    if (len != 0) {
        memcpy( g_qm_transport.tx.buff + HEADER_SIZE, data, len);
        if ( g_qm_transport.tx.encrypted != 0) {
            do_encrypt( g_qm_transport.tx.buff + HEADER_SIZE, len);
        }
    }
    
    return ret;
}

static uint16_t tx_bytes_left(void)
{
    return ( g_qm_transport.tx.len -  g_qm_transport.tx.bytes_sent);
}

static bool_t rx_frames_left(void)
{
    return ( g_qm_transport.rx.total_frame !=  g_qm_transport.rx.frame_seq);
}

static qm_ble_ret_code_t send_fragment(void)
{
    qm_ble_ret_code_t ret = QB_SUCCESS;
    uint16_t len, pkt_len, bytes_left;
    uint16_t payload_max_len =  g_qm_transport.max_pkt_size - HEADER_SIZE;
    uint16_t pkt_sent = 0;

    bytes_left = tx_bytes_left();
    if ( g_qm_transport.tx.encrypted != 0) {
        payload_max_len &= ~(AES_BLK_SIZE - 1);
        payload_max_len -= 1;
        QM_BLE_LOGI(QM_BLE_LOG_STATS, "payload max len: %d", payload_max_len);
    }

    do {
        len = QM_BLE_MIN(bytes_left, payload_max_len);
        QM_BLE_LOGI(QM_BLE_LOG_STATS, "payload len: %d", len);
        build_packet( g_qm_transport.tx.data +  g_qm_transport.tx.bytes_sent, len);
        pkt_len = len +  g_qm_transport.tx.pad_len + HEADER_SIZE;
        if ( g_qm_transport.tx.active_func == qm_ble_service_send_indication)
            qm_ble_mutex_lock(&( g_qm_transport.tx.mutex_indicate_done), 1000);
        ret =  g_qm_transport.tx.active_func( g_qm_transport.tx.buff, pkt_len);
        if (ret == QB_SUCCESS) {
             g_qm_transport.tx.pkt_req++;
             g_qm_transport.tx.frame_seq++;
             g_qm_transport.tx.bytes_sent += len;
            bytes_left = tx_bytes_left();
            pkt_sent++;
        }
        if ( g_qm_transport.tx.active_func == qm_ble_service_send_indication)
            qm_ble_mutex_unlock(&( g_qm_transport.tx.mutex_indicate_done));
        if (ret != QB_SUCCESS ||
             g_qm_transport.tx.active_func == qm_ble_service_send_indication) {
            break;
        }
    }  while (bytes_left > 0);

    if (g_qm_transport.timeout != 0) {
        qm_ble_timer_start(& g_qm_transport.tx.timer);
    }
    if ( g_qm_transport.tx.active_func == qm_ble_service_send_notification) {
        qm_ble_transport_txdone(pkt_sent);
    }
    return ret;
}


static void trans_rx_dispatcher(void)
{
    if (!is_valid_rx_command( g_qm_transport.rx.cmd)) {
        return;
    }
    
    QM_BLE_LOGD(QM_BLE_LOG_DOWNLINK, "ble rx len: %d, cmd: 0x%02X",  g_qm_transport.rx.bytes_received,  g_qm_transport.rx.cmd);
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "ble rx", g_qm_transport.rx.buff,
                   g_qm_transport.rx.bytes_received);

    if( g_qm_transport.rx.cmd == QM_BLE_CMD_AUTH ||  g_qm_transport.rx.cmd == QM_BLE_CMD_SET_RAND){
#if QB_ENABLE_AUTH
        qm_ble_auth_rx_command( g_qm_transport.rx.cmd,  g_qm_transport.rx.buff,  g_qm_transport.rx.bytes_received);
#endif
    }else {
         qm_rx_cmd_post.msg_id =  g_qm_transport.rx.msg_id;
         qm_rx_cmd_post.cmd =  g_qm_transport.rx.cmd;
         qm_rx_cmd_post.frame_seq =  g_qm_transport.rx.frame_seq + 1;
         qm_rx_cmd_post.p_rx_buf =   g_qm_transport.rx.buff;
         qm_rx_cmd_post.buf_sz =  g_qm_transport.rx.bytes_received;        
        qm_ble_core_event_notify(QB_EVENT_RX_INFO, (uint8_t*)& qm_rx_cmd_post, sizeof( qm_rx_cmd_post));
    }
}

qm_ble_ret_code_t qm_ble_transport_deinit(void)
{
    if(g_qm_transport.tx.mutex_indicate_done.hdl) {
        qm_ble_mutex_free(&( g_qm_transport.tx.mutex_indicate_done));
    }

    qm_ble_timer_free(& g_qm_transport.tx.timer);
    qm_ble_timer_free(& g_qm_transport.rx.timer);

#if QB_ENABLE_AUTH
    qm_ble_aes128_destroy( g_qm_transport.p_aes_ctx);
     g_qm_transport.p_aes_ctx = NULL;
#endif

    return QB_SUCCESS;
}

qm_ble_ret_code_t qm_ble_transport_init(qm_ble_init_t const *p_init)
{
    /* Initialize context */
    memset(& g_qm_transport, 0, sizeof(qm_ble_transport_t));
     g_qm_transport.max_pkt_size = QB_GATT_MTU_SIZE_DEFAULT - 3;
     g_qm_transport.timeout = p_init->transport_timeout;

    if ( g_qm_transport.tx.mutex_indicate_done.hdl == NULL) {
        qm_ble_mutex_new(&( g_qm_transport.tx.mutex_indicate_done));
    }

    if ( g_qm_transport.timeout != 0) {
        qm_ble_timer_new(& g_qm_transport.tx.timer, on_tx_timeout, & g_qm_transport,  g_qm_transport.timeout, 0);
        qm_ble_timer_new(& g_qm_transport.rx.timer, on_rx_timeout, & g_qm_transport,  g_qm_transport.timeout, 0);
    }
    return QB_SUCCESS;
}

void qm_ble_transport_reset(void)
{
    reset_tx();
    reset_rx();
    
#if QB_ENABLE_AUTH
    qm_ble_aes128_destroy( g_qm_transport.p_aes_ctx);
     g_qm_transport.p_aes_ctx = NULL;
#endif
}

uint8_t qm_ble_tx_msg_id_get(void)
{
    ++ g_qm_transport.tx_msg_id;
    if( g_qm_transport.tx_msg_id == 16){
         g_qm_transport.tx_msg_id = 1;
    }
    return  g_qm_transport.tx_msg_id;
}

qm_ble_ret_code_t qm_ble_transport_tx(uint8_t tx_type, uint8_t *msg_id, uint8_t cmd,
                        uint8_t const *const p_data, uint16_t length)
{
    uint16_t pkt_payload_len;

    if (p_data == NULL && length != 0) {
        return QB_ENULL;
    }
    if(length > QB_MAX_PAYLOAD_SIZE){
        return QB_EDATASIZE;
    }

    if ( g_qm_transport.p_key != NULL && cmd != QM_BLE_CMD_AUTH) {
         g_qm_transport.tx.encrypted = 1;
#ifdef EN_LONG_MTU
        pkt_payload_len =  g_qm_transport.max_pkt_size - HEADER_SIZE;
#else
        pkt_payload_len = ( g_qm_transport.max_pkt_size - HEADER_SIZE) & ~(AES_BLK_SIZE - 1);
        pkt_payload_len -= 1;

#endif
    } else {
         g_qm_transport.tx.encrypted = 0;
        pkt_payload_len =  g_qm_transport.max_pkt_size - HEADER_SIZE;
    }
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "tx encrypted %d",  g_qm_transport.tx.encrypted);

    if (tx_bytes_left() != 0 ||
         g_qm_transport.tx.pkt_req !=  g_qm_transport.tx.pkt_cfm) {
        return QB_EBUSY;
    }

     g_qm_transport.tx.data = (uint8_t *)p_data;
     g_qm_transport.tx.len = length;
     g_qm_transport.tx.bytes_sent = 0;
     g_qm_transport.tx.cmd = cmd;
     g_qm_transport.tx.frame_seq = 0;
     g_qm_transport.tx.pkt_req = 0;
     g_qm_transport.tx.pkt_cfm = 0;

    if (cmd == QM_BLE_CMD_AUTH || cmd == QM_BLE_CMD_INIT || cmd == QM_BLE_CMD_REPORT) {
       
        if(*msg_id == 0){
             g_qm_transport.tx.msg_id = qm_ble_tx_msg_id_get();
            *msg_id =  g_qm_transport.tx.msg_id;
        }else{
             g_qm_transport.tx.msg_id = *msg_id;
        }
    }else{
         g_qm_transport.tx.msg_id = *msg_id;
    }
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "tx msg id %d",  g_qm_transport.tx.msg_id);

    if(p_data != NULL && length != 0){
         g_qm_transport.tx.total_frame = length / pkt_payload_len;
        if ( g_qm_transport.tx.total_frame * pkt_payload_len == length && length != 0) {
             g_qm_transport.tx.total_frame--;
        }
    }
    QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "tx total frame %d",  g_qm_transport.tx.total_frame + 1);

    if (tx_type == QB_TX_NOTIFICATION) {
         g_qm_transport.tx.active_func = qm_ble_service_send_notification;
    } else {
         g_qm_transport.tx.active_func = qm_ble_service_send_indication;
    }

    send_fragment();
    return QB_SUCCESS;
}

void qm_ble_transport_rx(uint8_t *p_data, uint16_t length)
{
    uint16_t len, buff_left;
    if (length < HEADER_SIZE) {
        QM_BLE_LOGW(QM_BLE_LOG_LIFECYCLE,
                    "transport rx short frame len=%u",
                    (unsigned)length);
        reset_rx();
        return;
    } else if ((length - HEADER_SIZE +  g_qm_transport.rx.bytes_received) > RX_BUFF_LEN) {
        qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_RX_BUFF_SIZE, QB_EDATASIZE);
        reset_rx();
        return;
    }

    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "transport rx cmd=0x%02x msg=%u total=%u seq=%u len=%u",
                CMD_TYPE(p_data), MSG_ID(p_data), TOTAL_FRAME(p_data),
                FRAME_SEQ(p_data), (unsigned)length);

    if (!rx_frames_left()) {
        if (FRAME_SEQ(p_data) != 0) {
            qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_1ST_FRAME, QB_EINVALIDDATA);
            reset_rx();
            return;
        }

         g_qm_transport.rx.msg_id = MSG_ID(p_data);
         g_qm_transport.rx.cmd = CMD_TYPE(p_data);
         g_qm_transport.rx.total_frame = TOTAL_FRAME(p_data);
         g_qm_transport.rx.frame_seq = 0;
         g_qm_transport.rx.bytes_received = 0;
    } else {
        if (( g_qm_transport.rx.msg_id != MSG_ID(p_data)) ||
            ( g_qm_transport.rx.cmd != CMD_TYPE(p_data)) ||
            ( g_qm_transport.rx.total_frame != TOTAL_FRAME(p_data)) ||
            ((( g_qm_transport.rx.frame_seq + 1) & 0xF) != FRAME_SEQ(p_data))) {
            qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_OTHER_FRAMES, QB_EINVALIDDATA);
            reset_rx();
            return;
        }else {
             g_qm_transport.rx.frame_seq = FRAME_SEQ(p_data);
        }
    }

    if (IS_ENC(p_data) != 0) {
        if ((length - HEADER_SIZE) % 16 != 0) {
            qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_ENCRYPTED, QB_EINVALIDDATA);
            reset_rx();
            return;
        }
        if ( g_qm_transport.p_key == NULL) {
            qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_ENCRYPTED, QB_EFORBIDDEN);
            reset_rx();
            return;
        }
    }

    if ((length != HEADER_SIZE + FRAME_LEN(p_data) && IS_ENC(p_data) == 0)
        || (length < HEADER_SIZE + FRAME_LEN(p_data) && IS_ENC(p_data) != 0)) {
        qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_OTHER_FRAMES, QB_EDATASIZE);
        reset_rx();
        return;
    }

    buff_left = RX_BUFF_LEN -  g_qm_transport.rx.bytes_received;
    if ((len = QM_BLE_MIN(buff_left, FRAME_LEN(p_data))) > 0) {
        if (IS_ENC(p_data) != 0) {
            do_decrypt(p_data + HEADER_SIZE, &len);
        }
        memcpy( g_qm_transport.rx.buff +  g_qm_transport.rx.bytes_received, p_data + HEADER_SIZE, len);
         g_qm_transport.rx.bytes_received += len;
    }
    if (!rx_frames_left()) {
        trans_rx_dispatcher();
        reset_rx();
    } else {
        if ( g_qm_transport.timeout != 0) {
            qm_ble_timer_start(& g_qm_transport.rx.timer);
        }
    }
}

void qm_ble_transport_txdone(uint16_t pkt_sent)
{
    uint16_t bytes_left;

    g_qm_transport.tx.pkt_cfm += pkt_sent;
    bytes_left = tx_bytes_left();
    if (bytes_left != 0) {
        send_fragment();
    } else if ( g_qm_transport.tx.pkt_req ==  g_qm_transport.tx.pkt_cfm &&
                g_qm_transport.tx.pkt_req != 0) {
        if (!is_valid_tx_command( g_qm_transport.tx.cmd)) {
            return;
        } 
        reset_tx();
#if QB_ENABLE_AUTH
        qm_ble_auth_tx_done();
#endif
        qm_ble_core_event_notify(QB_EVENT_TX_DONE, NULL, 0);

    } else if ( g_qm_transport.tx.pkt_req <  g_qm_transport.tx.pkt_cfm) {
        QM_BLE_LOGD(QM_BLE_LOG_UPLINK, "pkt req %d, pkt cfm %d",  g_qm_transport.tx.pkt_req,  g_qm_transport.tx.pkt_cfm);
        reset_tx();
        qm_ble_core_handle_err(QM_ERROR_SRC_TRANSPORT_PKT_CFM_SENT, QB_EINTERNAL);
    }
}

uint32_t qm_ble_transport_update_key(uint8_t *key)
{
    char *iv = "0123456789ABCDEF";

     g_qm_transport.p_key = key;
    if ( g_qm_transport.p_aes_ctx) {
        qm_ble_aes128_destroy( g_qm_transport.p_aes_ctx);
         g_qm_transport.p_aes_ctx = NULL;
    }

     g_qm_transport.p_aes_ctx = qm_ble_aes128_init( g_qm_transport.p_key, (const uint8_t *)iv);
    QM_BLE_LOGD(QM_BLE_LOG_AUTH, "aes key updated");
    return QB_SUCCESS;
}

#ifdef EN_QM_BLE_LONG_MTU

uint32_t qm_ble_trans_update_mtu(void)
{
    uint16_t rounding_mtu;
    uint16_t max_payload_len = 0;
    qm_ble_get_att_mtu(&rounding_mtu);
    max_payload_len = rounding_mtu - QB_ATT_HDR_SIZE - QB_FRAME_HDR_SIZE;
     g_qm_transport.max_pkt_size = (uint16_t)(max_payload_len / QB_ENCRY_BLOCK_LENGTH) * QB_ENCRY_BLOCK_LENGTH + QB_FRAME_HDR_SIZE; 
    QM_BLE_LOGD(QM_BLE_LOG_CONTROL, "qm ble mtu:%d, mpu:%d", rounding_mtu,  g_qm_transport.max_pkt_size);
    return 0;
}

#endif
