
#include "qm.h"
#include "qm_log.h"
#include "qm_work.h"
#include "qm_utils_list.h"
#include "harmony_ble_hal.h"
#include "harmony_ble_comm.h"
#include "harmony_ble_trans.h"

/** Harmony BLE event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_HARMONY_BLE);

#define LOG_TAG "harmony ble gatt"

#define TEMP_BUFF_LEN   (256)

#define HARMONY_BLE_MIN(a, b) (a)<(b)? (a) : (b)

typedef struct {
    struct {
        uint8_t buff[HARMONY_BLE_DEFAULT_MTU_SIZE];
        uint8_t *data;
        uint16_t len;
        uint16_t bytes_sent;
        uint8_t encrypted;
        uint8_t msg_id;
        uint8_t cmd;
        uint8_t total_frame;
        uint8_t frame_seq;
        qm_list_t tx_list;
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_t tx_mutex; 
    #endif
    }tx;

    struct {
        uint8_t buff[HARMONY_BLE_TRANS_MAX_SIZE];
        uint16_t bytes_received;
        uint8_t msg_id;
        uint8_t cmd;
        uint8_t total_frame;
        uint8_t frame_seq;
        uint8_t encrypted;
    } rx;
    uint8_t tx_busy;
    uint8_t tx_msg_id;
    uint16_t max_pkt_size;
    uint8_t sta_buff[HARMONY_BLE_TRANS_MAX_SIZE];
    harmony_ble_trans_rx_cb rx_info;
} harmony_ble_trans_ctx_t;

typedef struct 
{
    int len;
    uint8_t payload[1];
}harmony_ble_trans_node_t;

static harmony_ble_trans_ctx_t g_trans_ctx = {0};

static int send_fragment(void);

static bool rx_frames_left(void)
{
    return (g_trans_ctx.rx.total_frame !=  g_trans_ctx.rx.frame_seq);
}

static void reset_tx(void)
{
    g_trans_ctx.tx.len = 0;
    g_trans_ctx.tx.bytes_sent = 0;
    g_trans_ctx.tx.msg_id = 0;
    g_trans_ctx.tx.cmd = 0;
    g_trans_ctx.tx.total_frame = 0;
    g_trans_ctx.tx.frame_seq = 0;
}

static void reset_rx(void)
{
    g_trans_ctx.rx.cmd = 0;
    g_trans_ctx.rx.total_frame = 0;
    g_trans_ctx.rx.frame_seq = 0;
    g_trans_ctx.rx.bytes_received = 0;
    memset(g_trans_ctx.rx.buff, 0, HARMONY_BLE_TRANS_MAX_SIZE);
}

static void trans_rx_dispatcher(void)
{
    int len = 0;
    harmony_ble_trans_info_t rx_info = {0};

    rx_info.cmd = g_trans_ctx.rx.cmd;
    rx_info.msg_id = g_trans_ctx.rx.msg_id;
    rx_info.encrypted = g_trans_ctx.rx.encrypted;
    rx_info.opt = HARMONY_BLE_PAYLOAD_DATA_OPT_GET(g_trans_ctx.rx.buff);
    rx_info.data_type =  HARMONY_BLE_PAYLOAD_DATA_FORMAT_GET(g_trans_ctx.rx.buff);
    rx_info.data.json_data.service_length = HARMONY_BLE_PAYLOAD_DATA_SERVICE_LEN_GET(g_trans_ctx.rx.buff);
    rx_info.data.json_data.service_name   = (char *)&g_trans_ctx.rx.buff[2];
    rx_info.data.json_data.body_length = g_trans_ctx.rx.buff[2 + rx_info.data.json_data.service_length] | g_trans_ctx.rx.buff[3 + rx_info.data.json_data.service_length] << 8;
    rx_info.data.json_data.body_data = (char *)&g_trans_ctx.rx.buff[4 + rx_info.data.json_data.service_length];


    if(rx_info.encrypted){
        len = 4 + rx_info.data.json_data.service_length + rx_info.data.json_data.body_length;
        memcpy(rx_info.data.json_data.encrpyt_hmac, &g_trans_ctx.rx.buff[len], HARMONY_BLE_CIPHER_HMACKEY_LEN);
    }

    if(g_trans_ctx.rx_info){
        g_trans_ctx.rx_info(&rx_info);
    }
}

static uint8_t tx_msg_id_get(void)
{
    ++ g_trans_ctx.tx_msg_id;
    if( g_trans_ctx.tx_msg_id == 16){
         g_trans_ctx.tx_msg_id = 1;
    }
    return  g_trans_ctx.tx_msg_id;
}

static void tx_raw_data_packet(uint8_t *pdata, harmony_ble_trans_info_t *tx_info)
{
    int len = 0;
    pdata[0] = 0x11;
    len = pdata[1] = tx_info->data.json_data.service_length;
    memcpy(pdata + 2,  tx_info->data.json_data.service_name, len);
    pdata[2 + len] =  tx_info->data.json_data.body_length & 0XFF;
    pdata[3 + len] =  tx_info->data.json_data.body_length >> 8;

    memcpy(pdata + 4 + len,  tx_info->data.json_data.body_data, tx_info->data.json_data.body_length);
}

static uint16_t tx_bytes_left(void)
{
    return ( g_trans_ctx.tx.len -  g_trans_ctx.tx.bytes_sent);
}

static void transport_txdone(uint8_t pkt_sent)
{
    int list_len = 0;
    qm_list_t *tx_list = &g_trans_ctx.tx.tx_list;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_trans_ctx.tx.tx_mutex, QM_WAIT_FOREVER);
#endif
    list_len = qm_list_len_get(tx_list);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_trans_ctx.tx.tx_mutex);
#endif
    if(!list_len){
        g_trans_ctx.tx_busy = QM_FALSE;
        return;
    }

    qm_event_post(QM_EVENT_HARMONY_BLE, HARMONY_BLE_TX_EVENT, NULL, 0, QM_NO_WAIT);
}

static void transport_tx(void)
{
    harmony_ble_trans_node_t *tx_buff = NULL;
    qm_list_node_t *node = NULL;
    qm_list_t *tx_list = &g_trans_ctx.tx.tx_list;
    
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_trans_ctx.tx.tx_mutex, QM_WAIT_FOREVER);
#endif
    if(!qm_list_len_get(tx_list)){
        g_trans_ctx.tx_busy = QM_FALSE;
        QM_LOGE(LOG_TAG, "tx_list null");
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(&g_trans_ctx.tx.tx_mutex);
    #endif
        return;
    }

    node = qm_list_lpop(tx_list);
    if(node == NULL){
        g_trans_ctx.tx_busy = QM_FALSE;
        QM_LOGE(LOG_TAG, "pop node null");

    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(&g_trans_ctx.tx.tx_mutex);
    #endif
        return;
    }
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_trans_ctx.tx.tx_mutex);
#endif
    tx_buff = qm_list_node_val_get(node);

    QM_LOGD(LOG_TAG, "pkt_len: %d", tx_buff->len);
    QM_HEX_LOGD(LOG_TAG, "PDATA: ",tx_buff->payload, tx_buff->len);
    harmony_ble_hal_send_indication(tx_buff->payload, tx_buff->len, transport_txdone);
    qm_list_node_destroy(node);
}

static uint32_t build_packet(uint8_t *data, uint16_t len)
{
    uint32_t ret = QM_EOK;

    g_trans_ctx.tx.buff[0] = g_trans_ctx.tx.cmd;
    g_trans_ctx.tx.buff[1] =  g_trans_ctx.tx.msg_id;
    g_trans_ctx.tx.buff[2] = g_trans_ctx.tx.total_frame + 1;
    g_trans_ctx.tx.buff[3] = g_trans_ctx.tx.frame_seq;
    g_trans_ctx.tx.buff[4] = 0x00;
    g_trans_ctx.tx.buff[5] = g_trans_ctx.tx.encrypted;
    g_trans_ctx.tx.buff[6] = 0;

    /* Payload */
    if (len != 0) {
        memcpy( g_trans_ctx.tx.buff + HARMONY_BLE_HEAD_LEN, data, len);
    }
    
    return ret;
}


static int send_fragment(void)
{
    int ret = QM_EOK;
    harmony_ble_trans_node_t *tx_buff = NULL;
    qm_list_node_t *node = NULL;
    qm_list_t *tx_list = &g_trans_ctx.tx.tx_list;
    uint16_t len, pkt_len, bytes_left;
    uint16_t payload_max_len =  HARMONY_BLE_DEFAULT_MTU_SIZE - HARMONY_BLE_HEAD_LEN;

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_trans_ctx.tx.tx_mutex, QM_WAIT_FOREVER);
#endif
    bytes_left = tx_bytes_left();
    while (bytes_left > 0)
    {
        len = HARMONY_BLE_MIN(bytes_left, payload_max_len);
        build_packet( g_trans_ctx.tx.data +  g_trans_ctx.tx.bytes_sent, len);
        pkt_len = len + HARMONY_BLE_HEAD_LEN;
        node = qm_list_node_extra_new(sizeof(harmony_ble_trans_node_t) + pkt_len - 1);
        if(node == NULL){
            QM_LOGE(LOG_TAG, "node NULL", node);
            break;
        }
        tx_buff = (harmony_ble_trans_node_t *)qm_list_node_val_get(node);

        tx_buff->len = pkt_len;
        memcpy(&tx_buff->payload[0], g_trans_ctx.tx.buff, pkt_len);

        qm_list_rpush(tx_list, node);
        
        g_trans_ctx.tx.frame_seq++;
        g_trans_ctx.tx.bytes_sent += len;
        bytes_left = tx_bytes_left();
    }
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_trans_ctx.tx.tx_mutex);
#endif

    if(!g_trans_ctx.tx_busy){
        g_trans_ctx.tx_busy = QM_TRUE;
        qm_event_post(QM_EVENT_HARMONY_BLE, HARMONY_BLE_TX_EVENT, NULL, 0, QM_NO_WAIT);
    }

    reset_tx();

    return ret;
}

int harmony_ble_transport_tx(harmony_ble_trans_info_t *tx_info)
{
    uint16_t pkt_payload_len;
    QM_RETURN_ON_FALSE(tx_info, -QM_EINVAL, LOG_TAG, "tx_info inval");
    
    g_trans_ctx.tx.encrypted = 0;
    pkt_payload_len =  HARMONY_BLE_DEFAULT_MTU_SIZE - HARMONY_BLE_HEAD_LEN;

    g_trans_ctx.tx.len = 4;
    g_trans_ctx.tx.len += tx_info->data.json_data.body_length;
    g_trans_ctx.tx.len += tx_info->data.json_data.service_length;
    if(g_trans_ctx.tx.len > HARMONY_BLE_TRANS_MAX_SIZE){
        return -QM_ENOMEM;
    }


    memset(g_trans_ctx.sta_buff, 0 , HARMONY_BLE_TRANS_MAX_SIZE);

    tx_raw_data_packet(g_trans_ctx.sta_buff, tx_info);
    
    g_trans_ctx.tx.data = (uint8_t *)g_trans_ctx.sta_buff;
    g_trans_ctx.tx.bytes_sent = 0;
    g_trans_ctx.tx.cmd = tx_info->cmd;
    g_trans_ctx.tx.frame_seq = 0;

    if(tx_info->msg_id){
        g_trans_ctx.tx.msg_id = tx_info->msg_id;
    }else{
        g_trans_ctx.tx.msg_id = tx_msg_id_get();
    }

    g_trans_ctx.tx.total_frame = g_trans_ctx.tx.len / pkt_payload_len;
    if(g_trans_ctx.tx.total_frame * pkt_payload_len == g_trans_ctx.tx.len){
        g_trans_ctx.tx.total_frame--;
    }

    QM_LOGD(LOG_TAG, "tx.total_frame %d",  g_trans_ctx.tx.total_frame + 1);

    send_fragment();
    

    return QM_EOK;
}

int harmony_ble_transport_encrypt_tx(harmony_ble_trans_info_t *tx_info, 
									char *prod_id, int id_len,
                                    uint8_t iv[HARMONY_BLE_CIPHER_IV_LEN],
                                    uint8_t sesskey[HARMONY_BLE_CIPHER_SESSID_LEN],
                                    uint8_t sessid[HARMONY_BLE_CIPHER_SESSID_LEN], 
                                    uint8_t hmac_key[HARMONY_BLE_CIPHER_HMACKEY_LEN])
{
    int len = 0;
    int encrypt_len = 0;
    uint8_t *p_str_data = NULL;
    harmony_ble_cipher_info_t cipher_info = {0};
    uint8_t tag_data[HARMONY_BLE_CIPHER_TAG_LEN] = {0};
    uint8_t hmac_data[HARMONY_BLE_CIPHER_HMACKEY_LEN] = {0};


    uint16_t pkt_payload_len = HARMONY_BLE_DEFAULT_MTU_SIZE - HARMONY_BLE_HEAD_LEN;
    QM_RETURN_ON_FALSE(tx_info, -QM_EINVAL, LOG_TAG, "tx_info inval");

    encrypt_len = tx_info->data.json_data.body_length;
    tx_info->data.json_data.body_length += HARMONY_BLE_CIPHER_IV_LEN;
    tx_info->data.json_data.body_length += HARMONY_BLE_CIPHER_TAG_LEN;
    tx_info->data.json_data.body_length += HARMONY_BLE_CIPHER_SESSID_LEN;

    g_trans_ctx.tx.encrypted = tx_info->encrypted;
    g_trans_ctx.tx.len = 4;
    g_trans_ctx.tx.len += tx_info->data.json_data.body_length;
    g_trans_ctx.tx.len += tx_info->data.json_data.service_length;
    g_trans_ctx.tx.len += HARMONY_BLE_CIPHER_HMACKEY_LEN;

    if(g_trans_ctx.tx.len > HARMONY_BLE_TRANS_MAX_SIZE){
        return -QM_ENOMEM;
    }

    memset(g_trans_ctx.sta_buff, 0 , HARMONY_BLE_TRANS_MAX_SIZE);

    g_trans_ctx.sta_buff[0] = 0x11;
    len = g_trans_ctx.sta_buff[1] = tx_info->data.json_data.service_length;
    memcpy(g_trans_ctx.sta_buff + 2,  tx_info->data.json_data.service_name, len);
    g_trans_ctx.sta_buff[2 + len] =  tx_info->data.json_data.body_length & 0XFF;
    g_trans_ctx.sta_buff[3 + len] =  tx_info->data.json_data.body_length >> 8;

    p_str_data = (uint8_t *)qm_malloc(encrypt_len);
    memset(p_str_data, 0, encrypt_len);
 
    cipher_info.session_key = sesskey;
    cipher_info.data_len = encrypt_len;
    cipher_info.in_data = (uint8_t *)tx_info->data.json_data.body_data;
    cipher_info.iv = iv;
    cipher_info.prod_id = (uint8_t *)prod_id;
    cipher_info.prod_length = id_len;
    cipher_info.out_data = p_str_data;
    cipher_info.tag = tag_data;
    harmony_ble_cipher_encrypt_data(&cipher_info);

    encrypt_len = 4 + len;
    memcpy(g_trans_ctx.sta_buff + encrypt_len, cipher_info.iv, HARMONY_BLE_CIPHER_IV_LEN);
    encrypt_len += HARMONY_BLE_CIPHER_IV_LEN;

    memcpy(g_trans_ctx.sta_buff + encrypt_len, cipher_info.out_data, cipher_info.data_len);
    encrypt_len += cipher_info.data_len;

    memcpy(g_trans_ctx.sta_buff + encrypt_len, cipher_info.tag, HARMONY_BLE_CIPHER_TAG_LEN);
    encrypt_len += HARMONY_BLE_CIPHER_TAG_LEN;

    memcpy(g_trans_ctx.sta_buff + encrypt_len, sessid, HARMONY_BLE_CIPHER_SESSID_LEN);
    encrypt_len += HARMONY_BLE_CIPHER_SESSID_LEN;

    harmony_ble_cipher_encrpy_hmac_flush(g_trans_ctx.sta_buff, 
                                        g_trans_ctx.tx.len - HARMONY_BLE_CIPHER_HMACKEY_LEN, 
                                        hmac_key, 
                                        hmac_data);

    memcpy(g_trans_ctx.sta_buff + encrypt_len, hmac_data, HARMONY_BLE_CIPHER_HMACKEY_LEN);

    g_trans_ctx.tx.data = (uint8_t *)g_trans_ctx.sta_buff;
    g_trans_ctx.tx.bytes_sent = 0;
    g_trans_ctx.tx.cmd = tx_info->cmd;
    g_trans_ctx.tx.frame_seq = 0;

    if(tx_info->msg_id){
        g_trans_ctx.tx.msg_id = tx_info->msg_id;
    }else{
        g_trans_ctx.tx.msg_id = tx_msg_id_get();
    }

    g_trans_ctx.tx.total_frame = g_trans_ctx.tx.len / pkt_payload_len;
    if(g_trans_ctx.tx.total_frame * pkt_payload_len == g_trans_ctx.tx.len){
        g_trans_ctx.tx.total_frame--;
    }

    qm_free(p_str_data);
    p_str_data = NULL;

    QM_LOGD(LOG_TAG, "encrypt tx.total_frame %d",  g_trans_ctx.tx.total_frame + 1);

    send_fragment();

    return QM_EOK;
}

int harmony_ble_transport_rx(uint8_t *p_data, uint16_t length)
{

    harmony_ble_frame_header_t *header = (harmony_ble_frame_header_t *)p_data;

    QM_RETURN_ON_FALSE(p_data, -QM_EINVAL, LOG_TAG, "pdata inval");
    QM_RETURN_ON_FALSE(length > 8, -QM_EINVAL, LOG_TAG, "length < 8");
    
    if ((length - HARMONY_BLE_HEAD_LEN +  g_trans_ctx.rx.bytes_received) > HARMONY_BLE_TRANS_MAX_SIZE) {
        reset_rx();
        return -QM_EINVAL;
    }

    if (!rx_frames_left()){
        if (header->frame_seq != 0) {
            reset_rx();
            return -QM_EINVAL;
        }
        g_trans_ctx.rx.msg_id = header->msg_id;
        g_trans_ctx.rx.cmd = header->cmd_type;
        g_trans_ctx.rx.total_frame = header->frame_total - 1;
        g_trans_ctx.rx.frame_seq = 0;
        g_trans_ctx.rx.bytes_received = 0;
        g_trans_ctx.rx.encrypted = header->encry;
    } else {
        if (( g_trans_ctx.rx.msg_id != header->msg_id) ||
            ( g_trans_ctx.rx.cmd != header->cmd_type) ||
            ((g_trans_ctx.rx.total_frame + 1) != header->frame_total) ||
            ((g_trans_ctx.rx.frame_seq + 1) != header->frame_seq) ){
            reset_rx();
            return -QM_EINVAL;
        }else {
             g_trans_ctx.rx.frame_seq = header->frame_seq;
        }
    }

    memcpy(g_trans_ctx.rx.buff + g_trans_ctx.rx.bytes_received, p_data + HARMONY_BLE_HEAD_LEN, length - HARMONY_BLE_HEAD_LEN);
    g_trans_ctx.rx.bytes_received += (length - HARMONY_BLE_HEAD_LEN);

    if(!rx_frames_left()) {
        qm_event_post(QM_EVENT_HARMONY_BLE, HARMONY_BLE_RX_EVENT, NULL, 0, QM_NO_WAIT);
    }
    return QM_EOK;
}

static void qm_event_callback(void *event_handler_arg, qm_event_base_t event_base,
                              int32_t event_id, void *event_data, size_t event_data_size)
{
    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data);
    QM_UNUSED_ARG(event_data_size);

    switch (event_id)
    {
        case HARMONY_BLE_RX_EVENT:
            trans_rx_dispatcher();
            reset_rx();
        break;
        case HARMONY_BLE_TX_EVENT:
            transport_tx();
        break;
        default:
        break;
    }
}

int harmony_ble_trans_init(harmony_ble_trans_rx_cb rx_info)
{
    g_trans_ctx.rx_info = rx_info;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_new(&g_trans_ctx.tx.tx_mutex);
#endif
    qm_event_handler_register(QM_EVENT_HARMONY_BLE, QM_EVENT_ANY_ID, qm_event_callback, NULL);
    return QM_EOK;
}

int harmony_ble_trans_flush(void)
{
    unsigned int len = g_trans_ctx.tx.tx_list.len;
    qm_list_node_t *next;
    qm_list_node_t *curr = g_trans_ctx.tx.tx_list.head;

    reset_tx();
    reset_rx();

    while (len--) {
        next = curr->next;
        qm_free(curr);
        curr = next;
    }

    g_trans_ctx.tx_busy = QM_FALSE;
    
    return QM_EOK;
}

