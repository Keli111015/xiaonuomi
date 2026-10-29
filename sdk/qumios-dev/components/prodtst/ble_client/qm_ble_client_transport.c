#include "qm_ble_client_transport.h"
#include "qm_ble_client_common.h"
#include "qm_ble_client_service.h"
#include "qm_ble_client_core.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#define LOG_TAG "client trans"

#define HEADER_SIZE 4
#define AES_BLK_SIZE 16

#define IS_ENC(data) ((data[0] & 0x10) != 0)
#define MSG_ID(data) (data[0] & 0xf)
#define CMD_TYPE(data) (data[1])
#define TOTAL_FRAME(data) ((data[2] >> 4) & 0x0f)
#define FRAME_SEQ(data) (data[2] & 0x0f)
#define FRAME_LEN(data) (data[3])

static qm_ble_client_transport_t *qm_ble_client_transport_get(uint8_t conn_id);
static qm_ble_client_rx_cmd_post_t *qm_ble_client_rx_cmd_post_get(uint8_t conn_id);
static int send_fragment(uint8_t conn_id);
static uint8_t qm_ble_tx_msg_id_get(uint8_t conn_id);

static qm_ble_client_ctx_t g_qm_ble_client_ctx[CONFIG_QM_BLE_CONN_MAX_NUM] = {0};

static void reset_tx(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return;
    }

    ble_transport->tx.len = 0;
    ble_transport->tx.bytes_sent = 0;
    ble_transport->tx.msg_id = 0;
    ble_transport->tx.cmd = 0;
    ble_transport->tx.total_frame = 0;
    ble_transport->tx.frame_seq = 0;
    ble_transport->tx.pkt_req = 0;
    ble_transport->tx.pkt_cfm = 0;
    if ( ble_transport->timeout != 0) {
        qm_timer_stop(& ble_transport->tx.timer);
    }
}

static void reset_rx(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return;
    }
    ble_transport->rx.cmd = 0;
    ble_transport->rx.total_frame = 0;
    ble_transport->rx.frame_seq = 0;
    ble_transport->rx.bytes_received = 0;
    if ( ble_transport->timeout != 0) {
        qm_timer_stop(& ble_transport->rx.timer);
    }
}

static void on_tx_timeout(qm_timer_t *timer, void *arg)
{
    uint8_t conn_id = (uint8_t)(uint32_t)arg;
    QM_LOGE(LOG_TAG, "conn_id : %d, tx timeout", conn_id);
    reset_tx(conn_id);
    qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_TX_TIMER, QM_BLE_CLIENT_ETIMEOUT);
}

static void on_rx_timeout(qm_timer_t *timer, void *arg)
{
    uint8_t conn_id = (uint8_t)(uint32_t)arg;
    QM_LOGE(LOG_TAG, "conn_id : %d, rx timeout", conn_id);
    reset_rx(conn_id);
}

static bool_t is_valid_rx_command(uint8_t cmd) {
    if (cmd == QM_BLE_CLIENT_CMD_PRODTST) {
        return 1;
    }
    return 0;
}

static bool_t is_valid_tx_command(uint8_t cmd) {
    if (cmd == QM_BLE_CLIENT_CMD_PRODTST) {
        return 1;
    }
    return 0;
}

static int build_packet(uint8_t conn_id, uint8_t *data, uint16_t len)
{
    int ret = QM_EOK;

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return -QM_EINVAL;
    }

    ble_transport->tx.pad_len = 0;
    ble_transport->tx.buff[0] = ((QM_BLE_CLIENT_TRANSPORT_VER & 0x7) << 5) |
                            (( ble_transport->tx.encrypted & 0x1) << 4) |
                            ( ble_transport->tx.msg_id & 0xF);
    ble_transport->tx.buff[1] =  ble_transport->tx.cmd;
    ble_transport->tx.buff[2] = (( ble_transport->tx.total_frame & 0x0F) << 4) |
                            ( ble_transport->tx.frame_seq & 0x0F);
    ble_transport->tx.buff[3] = len;

    QM_LOGD(LOG_TAG, "frame len (%d)",  ble_transport->tx.buff[3]);

    /* Payload */
    if (len != 0) {
        memcpy( ble_transport->tx.buff + HEADER_SIZE, data, len);
    }
    
    return ret;
}

static uint16_t tx_bytes_left(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return 0;
    }
    return ( ble_transport->tx.len -  ble_transport->tx.bytes_sent);
}

static bool_t rx_frames_left(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return 0;
    }
    return ( ble_transport->rx.total_frame !=  ble_transport->rx.frame_seq);
}

static void qm_ble_client_transport_txdone(uint8_t conn_id, uint16_t pkt_sent)
{
    uint16_t bytes_left;

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return;
    }
    ble_transport->tx.pkt_cfm += pkt_sent;
    bytes_left = tx_bytes_left(conn_id);
    if (bytes_left != 0) {
        send_fragment(conn_id);
    } else if ( ble_transport->tx.pkt_req == ble_transport->tx.pkt_cfm &&
                ble_transport->tx.pkt_req != 0) {
        if (!is_valid_tx_command( ble_transport->tx.cmd)) {
            return;
        } 
        reset_tx(conn_id);
        qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_TX_DONE, NULL, 0);

    } else if ( ble_transport->tx.pkt_req < ble_transport->tx.pkt_cfm) {
        QM_LOGD(LOG_TAG, "pkt_req %d, pkt_cfm %d", ble_transport->tx.pkt_req, ble_transport->tx.pkt_cfm);
        reset_tx(conn_id);
        qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_PKT_CFM_SENT, QM_BLE_CLIENT_EINTERNAL);
    }
}

static int send_fragment(uint8_t conn_id)
{
    qm_err_t ret = QM_EOK;
    uint16_t len, pkt_len, bytes_left;
    uint16_t payload_max_len = 0;
    uint16_t pkt_sent = 0;

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return -QM_EINVAL;
    }

    payload_max_len = ble_transport->max_pkt_size - HEADER_SIZE;

    bytes_left = tx_bytes_left(conn_id);
    if ( ble_transport->tx.encrypted != 0) {
        payload_max_len &= ~(AES_BLK_SIZE - 1);
        payload_max_len -= 1;
        QM_LOGD(LOG_TAG, "payload_max_len: %d", payload_max_len);
    }

    do {
        len = QM_BLE_MIN(bytes_left, payload_max_len);
        QM_LOGD(LOG_TAG, "len: %d", len);
        build_packet(conn_id, ble_transport->tx.data + ble_transport->tx.bytes_sent, len);
        pkt_len = len + ble_transport->tx.pad_len + HEADER_SIZE;
        ret =  ble_transport->tx.active_func(conn_id, ble_transport->tx.buff, pkt_len);
        if (ret == QM_EOK) {
            ble_transport->tx.pkt_req++;
            ble_transport->tx.frame_seq++;
            ble_transport->tx.bytes_sent += len;
            bytes_left = tx_bytes_left(conn_id);
            pkt_sent++;
        }
        if (ret != QM_EOK ||
            ble_transport->tx.active_func == qm_ble_client_service_send_indication) {
            break;
        }
    }  while (bytes_left > 0);

    if (ble_transport->timeout != 0) {
        qm_timer_start(& ble_transport->tx.timer);
    }
    if ( ble_transport->tx.active_func == qm_ble_client_service_send_notification) {
        qm_ble_client_transport_txdone(conn_id, pkt_sent);
    }
    return ret;
}


static void trans_rx_dispatcher(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return;
    }

    qm_ble_client_rx_cmd_post_t *rx_cmd_post = qm_ble_client_rx_cmd_post_get(conn_id);
    if(rx_cmd_post == NULL){
        return;
    }

    if (!is_valid_rx_command( ble_transport->rx.cmd)) {
        return;
    }
    
    QM_LOGD(LOG_TAG, "ble rx len: %d, cmd: 0x%02X",  ble_transport->rx.bytes_received,  ble_transport->rx.cmd);
    QM_HEX_LOGD(LOG_TAG, "ble rx: ", ble_transport->rx.buff,  ble_transport->rx.bytes_received);

    rx_cmd_post->msg_id =  ble_transport->rx.msg_id;
    rx_cmd_post->cmd =  ble_transport->rx.cmd;
    rx_cmd_post->frame_seq = ble_transport->rx.frame_seq + 1;
    rx_cmd_post->p_rx_buf = ble_transport->rx.buff;
    rx_cmd_post->buf_sz = ble_transport->rx.bytes_received;        
    qm_ble_client_core_event_notify(conn_id, QM_BLE_CLIENT_EVENT_RX_INFO, (uint8_t*)rx_cmd_post, sizeof(qm_ble_client_rx_cmd_post_t));
}

int qm_ble_client_transport_init(uint8_t conn_id, uint16_t max_mtu, uint32_t transport_timeout)
{

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return -QM_EINVAL;
    }

    ble_transport->max_pkt_size = max_mtu;
    ble_transport->timeout = transport_timeout;

    if (ble_transport->tx.timer.hdl == NULL && ble_transport->timeout != 0) {
        qm_timer_new(&ble_transport->tx.timer, on_tx_timeout, (void*)(uint32_t)conn_id, ble_transport->timeout, 0);
    }

    if (ble_transport->rx.timer.hdl == NULL && ble_transport->timeout != 0) {
        qm_timer_new(&ble_transport->rx.timer, on_rx_timeout, (void*)(uint32_t)conn_id, ble_transport->timeout, 0);
    }
    return QM_EOK;
}

void qm_ble_client_transport_reset(uint8_t conn_id)
{
    reset_tx(conn_id);
    reset_rx(conn_id);
}

static uint8_t qm_ble_tx_msg_id_get(uint8_t conn_id)
{
    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return 0;
    }

    ++ble_transport->tx_msg_id;
    if(ble_transport->tx_msg_id == 16){
        ble_transport->tx_msg_id = 1;
    }
    return ble_transport->tx_msg_id;
}

int qm_ble_client_transport_tx(uint8_t conn_id, uint8_t tx_type, uint8_t *msg_id, uint8_t cmd,
                        uint8_t const *const p_data, uint16_t length)
{
    uint16_t pkt_payload_len;

    if (p_data == NULL || length == 0) {
        return -QM_EINVAL;
    }
    if(length > QM_BLE_CLIENT_MAX_PAYLOAD_SIZE){
        return -QM_EINVAL;
    }

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return -QM_EINVAL;
    }

    ble_transport->tx.encrypted = 0;
    pkt_payload_len = ble_transport->max_pkt_size - HEADER_SIZE;


    if (tx_bytes_left(conn_id) != 0 ||
        ble_transport->tx.pkt_req != ble_transport->tx.pkt_cfm) {
        return -QM_EBUSY;
    }

     ble_transport->tx.data = (uint8_t*)p_data;
     ble_transport->tx.len = length;
     ble_transport->tx.bytes_sent = 0;
     ble_transport->tx.cmd = cmd;
     ble_transport->tx.frame_seq = 0;
     ble_transport->tx.pkt_req = 0;
     ble_transport->tx.pkt_cfm = 0;

    if (cmd == QM_BLE_CLIENT_CMD_PRODTST) {
       
        if(*msg_id == 0){
             ble_transport->tx.msg_id = qm_ble_tx_msg_id_get(conn_id);
            *msg_id = ble_transport->tx.msg_id;
        }else{
             ble_transport->tx.msg_id = *msg_id;
        }
    }else{ 
         ble_transport->tx.msg_id = *msg_id;
    }
    QM_LOGD(LOG_TAG, "tx.msg_id %d",  ble_transport->tx.msg_id);

    if(p_data != NULL && length != 0){
         ble_transport->tx.total_frame = length / pkt_payload_len;
        if ( ble_transport->tx.total_frame * pkt_payload_len == length && length != 0) {
             ble_transport->tx.total_frame--;
        }
    }
    QM_LOGD(LOG_TAG, "tx.total_frame %d",  ble_transport->tx.total_frame + 1);

    if (tx_type == QM_BLE_CLIENT_TX_NOTIFICATION) {
         ble_transport->tx.active_func = qm_ble_client_service_send_notification;
    } else {
         ble_transport->tx.active_func = qm_ble_client_service_send_indication;
    }

    send_fragment(conn_id);
    return QM_EOK;
}

void qm_ble_client_transport_rx(uint8_t conn_id, uint8_t *p_data, uint16_t length)
{
    uint16_t len, buff_left;

    qm_ble_client_transport_t *ble_transport = qm_ble_client_transport_get(conn_id);
    if(ble_transport == NULL){
        return;
    }

    if (length == 0) {
        return;
    } else if ((length - HEADER_SIZE +  ble_transport->rx.bytes_received) > QM_BLE_CLIENT_RX_BUFF_LEN) {
        qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_RX_BUFF_SIZE, QM_BLE_CLIENT_EDATASIZE);
        reset_rx(conn_id);
        return;
    }

    if (!rx_frames_left(conn_id)) {
        if (FRAME_SEQ(p_data) != 0) {
            qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_1ST_FRAME, QM_BLE_CLIENT_EINVALIDDATA);
            reset_rx(conn_id);
            return;
        }

         ble_transport->rx.msg_id = MSG_ID(p_data);
         ble_transport->rx.cmd = CMD_TYPE(p_data);
         ble_transport->rx.total_frame = TOTAL_FRAME(p_data);
         ble_transport->rx.frame_seq = 0;
         ble_transport->rx.bytes_received = 0;
    } else {
        if (( ble_transport->rx.msg_id != MSG_ID(p_data)) ||
            ( ble_transport->rx.cmd != CMD_TYPE(p_data)) ||
            ( ble_transport->rx.total_frame != TOTAL_FRAME(p_data)) ||
            ((( ble_transport->rx.frame_seq + 1) & 0xF) != FRAME_SEQ(p_data))) {
            qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_OTHER_FRAMES, QM_BLE_CLIENT_EINVALIDDATA);
            reset_rx(conn_id);
            return;
        }else {
             ble_transport->rx.frame_seq = FRAME_SEQ(p_data);
        }
    }

    if (IS_ENC(p_data) != 0) {
        if ((length - HEADER_SIZE) % 16 != 0) {
            qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_ENCRYPTED, QM_BLE_CLIENT_EINVALIDDATA);
            reset_rx(conn_id);
            return;
        }
    }

    if ((length != HEADER_SIZE + FRAME_LEN(p_data) && IS_ENC(p_data) == 0)
        || (length < HEADER_SIZE + FRAME_LEN(p_data) && IS_ENC(p_data) != 0)) {
        qm_ble_client_core_handle_err(conn_id, QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_OTHER_FRAMES, QM_BLE_CLIENT_EDATASIZE);
        reset_rx(conn_id);
        return;
    }

    buff_left = QM_BLE_CLIENT_RX_BUFF_LEN -  ble_transport->rx.bytes_received;
    if ((len = QM_BLE_MIN(buff_left, FRAME_LEN(p_data))) > 0) {
        if (IS_ENC(p_data) != 0) {
        }
        memcpy( ble_transport->rx.buff + ble_transport->rx.bytes_received, p_data + HEADER_SIZE, len);
         ble_transport->rx.bytes_received += len;
    }
    if (!rx_frames_left(conn_id)) {
        trans_rx_dispatcher(conn_id);
        reset_rx(conn_id);
    } else {
        if ( ble_transport->timeout != 0) {
            qm_timer_start(& ble_transport->rx.timer);
        }
    }
}

static qm_ble_client_transport_t *qm_ble_client_transport_get(uint8_t conn_id)
{
    return &g_qm_ble_client_ctx[conn_id].ble_transport;
}

static qm_ble_client_rx_cmd_post_t *qm_ble_client_rx_cmd_post_get(uint8_t conn_id)
{
    return &g_qm_ble_client_ctx[conn_id].rx_cmd_post;
}

int qm_ble_client_connect_set(void *conn_handle, int is_connected)
{
    int i = 0;
    if(conn_handle == NULL){
        return -QM_EINVAL;
    }
    for(i = 0; i < CONFIG_QM_BLE_CONN_MAX_NUM; i++){
        if(g_qm_ble_client_ctx[i].conn_handle == conn_handle){  
            g_qm_ble_client_ctx[i].is_connected = is_connected;
            return QM_EOK;
        } 
    }
    return -QM_EINVAL;
}

int qm_ble_get_remain_conn_id(void *conn_handle, uint8_t *conn_id)
{
    int i = 0;
    if(conn_handle == NULL || conn_id == NULL){
        return -QM_EINVAL;
    }
    for(i = 0; i < CONFIG_QM_BLE_CONN_MAX_NUM; i++){
        if(g_qm_ble_client_ctx[i].conn_handle == NULL){  
            g_qm_ble_client_ctx[i].conn_handle = conn_handle;
            *conn_id = i;
            return QM_EOK;
        } 
    }
    return -QM_EINVAL;
}

int qm_ble_del_conn_id(void *conn_handle)
{
    int i = 0;
    if(conn_handle == NULL){
        return -QM_EINVAL;
    }
    for(i = 0; i < CONFIG_QM_BLE_CONN_MAX_NUM; i++){
        if(g_qm_ble_client_ctx[i].conn_handle == conn_handle){  
            g_qm_ble_client_ctx[i].conn_handle = NULL;
            return QM_EOK;
        } 
    }
    return -QM_EINVAL;
}


void *qm_ble_conn_handle_get(uint8_t conn_id)
{
    if(conn_id >= CONFIG_QM_BLE_CONN_MAX_NUM){
        return NULL; 
    }

    return g_qm_ble_client_ctx[conn_id].conn_handle;
}

int qm_ble_conn_id_get_by_conn_handle(void *conn_handle, uint8_t *conn_id)
{
    int i = 0;
    if(conn_handle == NULL || conn_id == NULL){
        return -QM_EINVAL;
    }
    for(i = 0; i < CONFIG_QM_BLE_CONN_MAX_NUM; i++){
        if(g_qm_ble_client_ctx[i].conn_handle == conn_handle){  
            *conn_id = i;
            return QM_EOK;
        } 
    }
    return -QM_EINVAL;
}

int qm_ble_is_connnected(uint8_t conn_id)
{
    if(conn_id >= CONFIG_QM_BLE_CONN_MAX_NUM){
        return 0;
    }
    return g_qm_ble_client_ctx[conn_id].is_connected;
}