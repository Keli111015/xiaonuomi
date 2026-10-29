
#include "prodtst_ble_comm.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"
#include "qm_utils_list.h"
#include "qm_work.h"

/** Production-test BLE event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_PRODTST_BLE);

#define LOG_TAG "prodtst ble"

#define PRODTST_BLE_EVENT_RECV                  (0x0001)

#define PRODTST_BLE_PARSE_TOTAL_LEGTH_INDEX     (0)
#define PRODTST_BLE_PARSE_TYPE_INDEX            (1)
#define PRODTST_BLE_PARSE_CID1_INDEX            (2)
#define PRODTST_BLE_PARSE_CID2_INDEX            (3)
#define PRODTST_BLE_PARSE_VER_SUBTYPE_INDEX     (4)
#define PRODTST_BLE_PARSE_FMSK_INDEX            (5)
#define PRODTST_BLE_PARSE_CMD_INDEX             (6)
#define PRODTST_BLE_PARSE_PID_INDEX             (7)
#define PRODTST_BLE_PARSE_PAYLOAD_INDEX         (13)

typedef struct 
{
    uint8_t msg_id;
    prodtst_ble_channel_t channel;

    qm_list_t *recv_list;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t recv_mutex;
#endif
#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
    qm_list_t *send_list;

    uint8_t start_adv;
    qm_work_t tx_timer;
#endif
    prodtst_ble_param_t param;
    prodtst_ble_event_t event_cb;
}prodtst_ble_ctx_t;

#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
static uint8_t tx_msg_id_get(void);
static void on_tx_timeout(void *arg);
#endif
static int recv_list_match(void *a, void *b);
static int reset_rx(prodtst_ble_handle_t *ble_transport);
static bool_t rx_frames_left(prodtst_ble_handle_t *recv_handle);
static int32_t ble_prodtst_scan_read(uint8_t mac[6], uint8_t *recv_buff, int recv_length);
static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param);
static void frame_head_init(uint8_t *raw_buffer, uint8_t total_frame_seq , uint8_t cmd, 
                            uint8_t need_ack, uint8_t mac[6], uint8_t *buffer, uint16_t len);
static int prodtst_ble_event_notify(prodtst_ble_prodtst_event_t event, uint8_t *data, int length);

#if !CONFIG_PRODTST_BLE_USE_ALLOC
static prodtst_ble_ctx_t _prodtst_ctx = {0};
#endif
static prodtst_ble_ctx_t *g_prodtst_ctx = NULL;

int32_t prodtst_ble_comm_pid_change(uint16_t pid)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");

    g_prodtst_ctx->param.pid = pid;

    return QM_EOK;
}

#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
static void frame_head_init(uint8_t *raw_buffer, uint8_t total_frame_seq , uint8_t cmd, 
                            uint8_t need_ack, uint8_t mac[6], uint8_t *buffer, uint16_t len)
{
    int index = 0;
    prodtst_ble_frame_info_t *frame_info = (prodtst_ble_frame_info_t *)raw_buffer;
    prodtst_ble_head_t *head = &frame_info->header;
    prodtst_ble_frame_t *frame = &frame_info->frame;

    if(raw_buffer == NULL){
        return ;
    }

    head->len  =  PRODTST_BLE_FRAME_MIN_LEN + len - 1;
    head->type =  PRODTST_BLE_TYPE;
    head->cid  =  (PRODTST_BLE_CID_2 << 8) | PRODTST_BLE_CID_1;
    head->ver  =  (PRODTST_BLE_VER << 4) | PRODTST_BLE_SUBTYPE;

    frame->fmsk.channel = (uint8_t)g_prodtst_ctx->channel;
    frame->fmsk.encrypted = g_prodtst_ctx->param.encrypted;
    frame->fmsk.type = (uint8_t)g_prodtst_ctx->param.data_type;

    frame->frame_cmd.cmd = cmd;
    frame->fmsk.need_ack = need_ack;

    memcpy(frame->mac, mac + 4, 2);
    frame->pid = be16_to_cpu(g_prodtst_ctx->param.pid);

    frame->sno.encrypted = g_prodtst_ctx->param.encrypted;
    frame->sno.msg_id = tx_msg_id_get();
    frame->total_frame_seq.total_frame = ((total_frame_seq & 0xF0) >> 4);
    frame->total_frame_seq.frame_seq = total_frame_seq & 0x0F;

    frame->length = len;

    if(len != 0 && buffer){
        memcpy(&frame_info->pdata[0] + index, buffer, len);
    }
}   

int32_t prodtst_ble_comm_send(uint8_t cmd, uint8_t mac[6], uint8_t need_ack, uint8_t *data, int len)
{
    int ret = QM_EOK;
    uint8_t frame_seq = 0;
    uint8_t total_frame_seq = 0;
    qm_list_node_t *node = NULL;
    uint8_t *send_data = NULL;
    uint8_t *send_info = NULL;

    int send_len = 0;
    int total_len = 0;

    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_ENOMEM, LOG_TAG, "g_prodtst_ctx null");

    if(len > PRODTST_BLE_PLAYLOAD_MAX_LEN){
        total_frame_seq = (len % PRODTST_BLE_PLAYLOAD_MAX_LEN)  ? 
                        (len / PRODTST_BLE_PLAYLOAD_MAX_LEN) :
                        ((len / PRODTST_BLE_PLAYLOAD_MAX_LEN) - 1);
    }
    
    total_frame_seq = (total_frame_seq << 4) & 0xF0;

    do{
        send_len = ((len - total_len) <= PRODTST_BLE_PLAYLOAD_MAX_LEN) ? 
                    (len - total_len) : 
                    PRODTST_BLE_PLAYLOAD_MAX_LEN;

        if (send_len){
            node = qm_list_node_extra_new(sizeof(prodtst_ble_frame_info_t) + send_len - 1);
        }else{
            node = qm_list_node_extra_new(sizeof(prodtst_ble_frame_generic_t) + sizeof(int));
        }
        
        if(node == NULL){
            return -QM_ENOMEM;
        }

        send_info = (uint8_t *)node->val;
        
        if(send_len && data){
            send_data = data + total_len;
        }
        
        frame_head_init(send_info, total_frame_seq, cmd, need_ack, mac, send_data, send_len);

        qm_list_rpush(g_prodtst_ctx->send_list, node);

        frame_seq++;
        total_len += send_len;
        total_frame_seq &= 0xF0;
        total_frame_seq |= frame_seq;
    }while (total_len < len);

    if(!g_prodtst_ctx->start_adv){
        qm_cancel_delayed_action(&g_prodtst_ctx->tx_timer);
        qm_post_delayed_action(&g_prodtst_ctx->tx_timer, on_tx_timeout, NULL, 0);
    }

    return ret;
}
#endif

static int prodtst_ble_event_notify(prodtst_ble_prodtst_event_t event, uint8_t *data, int length)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx->event_cb, -QM_EINVAL, LOG_TAG, "event_cb null");

    return g_prodtst_ctx->event_cb(event,data, length);
}

static void rx_dispatch(void *data, int len)
{
    uint8_t parse_length = 0;
    uint8_t *parse_buff = NULL;
    prodtst_ble_frame_t *frame_mask = NULL;
    prodtst_ble_frame_t *handle_frame = NULL;
    prodtst_ble_handle_t *recv_handle = NULL;

    if(data == NULL){
        return ;
    }

    recv_handle = (prodtst_ble_handle_t *)data;

    handle_frame = &recv_handle->frame;
    frame_mask = (prodtst_ble_frame_t *)(recv_handle->raw_data + sizeof(prodtst_ble_head_t));

    if(!rx_frames_left(recv_handle)){
        if( frame_mask->total_frame_seq.frame_seq ||
            handle_frame->sno.msg_id == frame_mask->sno.msg_id){
            return ;
        }
        recv_handle->bytes_received = 0;
        memcpy(&recv_handle->frame, frame_mask, sizeof(prodtst_ble_frame_t));
    }else{
        if ((handle_frame->sno.msg_id == frame_mask->sno.msg_id) || 
            ( handle_frame->fmsk.channel != frame_mask->fmsk.channel) ||
            ( handle_frame->fmsk.need_ack != frame_mask->fmsk.need_ack) ||
            ( handle_frame->total_frame_seq.total_frame != frame_mask->total_frame_seq.total_frame) ||
            ( handle_frame->frame_cmd.cmd != frame_mask->frame_cmd.cmd) ||
            (((handle_frame->total_frame_seq.frame_seq + 1) & 0xF) != frame_mask->total_frame_seq.frame_seq)){
            return ;
        }else {
            handle_frame->total_frame_seq.frame_seq = frame_mask->total_frame_seq.frame_seq;
        }    
    }

    handle_frame->sno.msg_id = frame_mask->sno.msg_id;
    parse_length = recv_handle->raw_data[PRODTST_BLE_PARSE_PAYLOAD_INDEX];
    parse_buff =  recv_handle->raw_data + sizeof(prodtst_ble_frame_generic_t);

    frame_mask = (prodtst_ble_frame_t *)(recv_handle->raw_data + sizeof(prodtst_ble_head_t));

    if(parse_buff && parse_length != 0){
        if(recv_handle->bytes_received < CONFIG_PRODTST_BLE_BUFF_LEN){
            memcpy(recv_handle->pdata + recv_handle->bytes_received, parse_buff, parse_length);
            recv_handle->bytes_received += parse_length;    
        }
    }

    if(!rx_frames_left(recv_handle)){
        prodtst_ble_event_notify(PRODTST_BLE_PRODTST_EVENT_ACTION, (uint8_t*)recv_handle, sizeof(prodtst_ble_handle_t));
        reset_rx(recv_handle);
    }
}

static void qm_event_callback(void *event_handler_arg, qm_event_base_t event_base,
                              int32_t event_id, void *event_data, size_t event_data_size)
{
    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);

    switch (event_id)
    {
        case PRODTST_BLE_EVENT_RECV:
            rx_dispatch(event_data, event_data_size);
        break;
        default:
        break;
    }
}

int32_t prodtst_ble_comm_init(prodtst_ble_channel_t channel, prodtst_ble_param_t *param)
{
    qm_err_t ret = QM_EOK;

#if !CONFIG_PRODTST_BLE_USE_EXT_SCAN
    qm_ble_scan_params_t scan_param;
#endif

#if CONFIG_PRODTST_BLE_USE_ALLOC
    g_prodtst_ctx = (prodtst_ble_ctx_t*)qm_malloc(sizeof(prodtst_ble_ctx_t));
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_ENOMEM, LOG_TAG, "g_prodtst_ctx null");
#else
    g_prodtst_ctx = &_prodtst_ctx;
#endif
    memset(g_prodtst_ctx, 0, sizeof(prodtst_ble_ctx_t));
    
    g_prodtst_ctx->channel = channel;
    g_prodtst_ctx->recv_list = qm_list_new();
#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
    QM_GOTO_ON_FALSE(g_prodtst_ctx->recv_list, -QM_ENOMEM, _error, LOG_TAG, "recv_list null");
    g_prodtst_ctx->send_list = qm_list_new();
    QM_GOTO_ON_FALSE(g_prodtst_ctx->send_list, -QM_ENOMEM, _error, LOG_TAG, "send_list null");
#endif
    g_prodtst_ctx->recv_list->match = recv_list_match;
#if CONFIG_QM_OS_SUPPORT
    ret = qm_mutex_new(&g_prodtst_ctx->recv_mutex);
    QM_GOTO_ON_ERROR(ret, _error, LOG_TAG, "recv_mutex error");
#endif
    
    qm_event_handler_register(QM_EVENT_PRODTST_BLE, QM_EVENT_ANY_ID, qm_event_callback, NULL);
    
    qm_ble_gap_register_callback(qm_gap_ble_callback);

    memcpy(&g_prodtst_ctx->param, param, sizeof(prodtst_ble_param_t));

#if !CONFIG_PRODTST_BLE_USE_EXT_SCAN
    scan_param.scan_type = QM_BLE_SCAN_TYPE_PASSIVE;
    scan_param.scan_interval = CONFIG_PRODTST_BLE_SCAN_INTERVAL; 
    scan_param.scan_window = CONFIG_PRODTST_BLE_SCAN_WINDOWS; 
    qm_ble_gap_set_scan_params(&scan_param);
#endif

    return QM_EOK;

_error:
    prodtst_ble_comm_deinit();
    return ret;
}

int32_t prodtst_ble_comm_deinit(void)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");

    qm_event_handler_unregister(QM_EVENT_PRODTST_BLE, QM_EVENT_ANY_ID, qm_event_callback, NULL);

    qm_ble_gap_unregister_callback(qm_gap_ble_callback);
    
#if !CONFIG_PRODTST_BLE_USE_EXT_SCAN
    qm_ble_gap_stop_scanning();
#endif
    
#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
    qm_ble_gap_stop_advertising();
#endif
    
#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
    qm_cancel_delayed_action(&g_prodtst_ctx->tx_timer);
    qm_list_destroy(g_prodtst_ctx->send_list);
    g_prodtst_ctx->send_list = NULL;
#endif
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_prodtst_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif

    qm_list_destroy(g_prodtst_ctx->recv_list);

    g_prodtst_ctx->recv_list = NULL;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_prodtst_ctx->recv_mutex);
    qm_mutex_free(&g_prodtst_ctx->recv_mutex);
#endif
    
#if CONFIG_PRODTST_BLE_USE_ALLOC
    qm_free(g_prodtst_ctx);
#endif
    g_prodtst_ctx = NULL;

    return QM_EOK;
}

#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
static uint8_t tx_msg_id_get(void)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, 1, LOG_TAG, "g_prodtst_ctx null");
    
    ++g_prodtst_ctx->msg_id;
    if(g_prodtst_ctx->msg_id == 16){
         g_prodtst_ctx->msg_id = 1;
    }
    return g_prodtst_ctx->msg_id;
}

static void on_tx_timeout(void *arg)
{
    qm_list_node_t *node = NULL;
    prodtst_ble_frame_info_t *info = NULL;

    if(g_prodtst_ctx == NULL){
        return;
    }

    if(g_prodtst_ctx->start_adv){
        qm_ble_gap_stop_advertising();
    }

    node = qm_list_lpop(g_prodtst_ctx->send_list);

    if(node == NULL){
        g_prodtst_ctx->start_adv = QM_FALSE;
        return;
    }

    info = (prodtst_ble_frame_info_t *)node->val;
    qm_ble_gap_set_adv_data_raw((uint8_t *)info, sizeof(prodtst_ble_frame_info_t) + info->frame.length - 1);

    qm_list_node_destroy(node);

    g_prodtst_ctx->start_adv = QM_TRUE;

    qm_post_delayed_action(&g_prodtst_ctx->tx_timer, on_tx_timeout, NULL, CONFIG_PRODTST_BLE_TX_TIMEOUT);
}
#endif

static int recv_list_match(void *a, void *b)
{
    uint8_t *a_handle = (uint8_t *)a;
    prodtst_ble_handle_t *b_handle = (prodtst_ble_handle_t*)b;

    if(!memcmp(a_handle, b_handle->src_addr, 6)){
        return QM_TRUE;
    }
    return QM_FALSE;
}

static void qm_gap_ble_callback(qm_gap_ble_cb_event_t event, qm_ble_gap_cb_param_t *param)
{
    qm_ble_adv_params_t adv_param = {
        .adv_int_max = CONFIG_PRODTST_BLE_ADV_INTERVAL,
        .adv_int_min = CONFIG_PRODTST_BLE_ADV_INTERVAL,
    };

    if(g_prodtst_ctx == NULL){
        return;
    }

    switch (event)
    {
#if !CONFIG_PRODTST_BLE_USE_EXT_SCAN
        case QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            qm_ble_gap_start_scanning(0);
        break; 
        case QM_GAP_BLE_SCAN_RESULT_EVT:
        {
            if(param->scan_rst.rssi < g_prodtst_ctx->param.min_rssi){
                return;
            }
            ble_prodtst_scan_read(param->scan_rst.bda, 
                                    param->scan_rst.ble_adv,
                                    param->scan_rst.adv_data_len);
        }
        break;
#endif
#if CONFIG_PRODTST_BLE_SERVER || CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT ||  CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
        case QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        case QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            qm_ble_gap_start_advertising(&adv_param);
        break;
#endif
        default:
        
        break;
    }
}

prodtst_ble_handle_t * prodtst_ble_handle_create(uint8_t mac[6])
{
    qm_list_node_t *node = NULL;

    if(g_prodtst_ctx == NULL){
        return NULL;
    }

    node = qm_list_node_extra_new(sizeof(prodtst_ble_handle_t));
    if(node == NULL){
        return NULL;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_prodtst_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif    
    qm_list_rpush(g_prodtst_ctx->recv_list, node);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_prodtst_ctx->recv_mutex); 
#endif
    return (prodtst_ble_handle_t *)node->val;
}

prodtst_ble_handle_t *prodtst_ble_handle_get(uint8_t mac[6])
{
    qm_list_node_t *node = NULL;

    if(g_prodtst_ctx == NULL){
        return NULL;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_prodtst_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif
    node = qm_list_find(g_prodtst_ctx->recv_list, mac);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_prodtst_ctx->recv_mutex);    
#endif
    if(node == NULL){
        return NULL;
    }

    return (prodtst_ble_handle_t *)node->val;
}

int32_t prodtst_ble_handle_delete(prodtst_ble_handle_t *handle)
{
    int ret = QM_EOK;
    uint8_t match_mac[6] = {0};
    qm_list_node_t *node = NULL;

    if(g_prodtst_ctx == NULL){
        return -QM_EINVAL;
    }

    memcpy(&match_mac[0], &handle->src_addr[0], 6);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_prodtst_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif    
    node = qm_list_find(g_prodtst_ctx->recv_list, match_mac);
    if(node == NULL){
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(&g_prodtst_ctx->recv_mutex);    
    #endif
        return -QM_EEMPTY;
    }
    qm_list_remove(g_prodtst_ctx->recv_list, node);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_prodtst_ctx->recv_mutex);   
#endif

    qm_list_node_destroy(node);
    return ret;
}

static bool_t rx_frames_left(prodtst_ble_handle_t *recv_handle)
{
    QM_RETURN_ON_FALSE(recv_handle, 0, LOG_TAG, "recv_handle null");
    return ( recv_handle->frame.total_frame_seq.total_frame != recv_handle->frame.total_frame_seq.frame_seq);
}

static int reset_rx(prodtst_ble_handle_t *ble_transport)
{
    prodtst_ble_frame_t *handle_frame = NULL;
    QM_RETURN_ON_FALSE(ble_transport, -QM_EINVAL, LOG_TAG, "ble_transport null");

    handle_frame = &ble_transport->frame;

    handle_frame->fmsk.channel = 0;
    handle_frame->total_frame_seq.frame_seq = 0;
    handle_frame->total_frame_seq.total_frame = 0;

    ble_transport->bytes_received = 0;
    memset(ble_transport->raw_data, 0, 32);
    memset(ble_transport->pdata, 0, CONFIG_PRODTST_BLE_BUFF_LEN);

    if(!ble_transport->is_conn){
        prodtst_ble_handle_delete(ble_transport);
    }
    return QM_EOK;
}

static int32_t ble_prodtst_scan_read(uint8_t mac[6], uint8_t *recv_buff, int recv_length)
{
    int ret = QM_EOK;
    uint16_t pid    = 0;
    uint8_t ver_subtype = 0;
    prodtst_ble_frame_t *frame_mask = NULL;
    prodtst_ble_handle_t *recv_handle = NULL;
    prodtst_ble_frame_t *handle_frame = NULL;

    QM_RETURN_ON_FALSE(recv_buff, -QM_EINVAL, LOG_TAG, "recv_buffs NULL");
    QM_RETURN_ON_FALSE(recv_length, -QM_EINVAL, LOG_TAG, "recv_length error");

    if(recv_length < PRODTST_BLE_MIN_LEN){
        return -QM_EINVAL;
    }

    ver_subtype = recv_buff[PRODTST_BLE_PARSE_VER_SUBTYPE_INDEX];
    pid = recv_buff[PRODTST_BLE_PARSE_PID_INDEX] << 8 | recv_buff[PRODTST_BLE_PARSE_PID_INDEX + 1];
    frame_mask = (prodtst_ble_frame_t *)(recv_buff + sizeof(prodtst_ble_head_t));

    if( pid != g_prodtst_ctx->param.pid ||
        frame_mask->fmsk.channel != !g_prodtst_ctx->channel ||
        recv_buff[PRODTST_BLE_PARSE_TYPE_INDEX] != PRODTST_BLE_TYPE ||
        recv_buff[PRODTST_BLE_PARSE_CID1_INDEX] != PRODTST_BLE_CID_1 || 
        recv_buff[PRODTST_BLE_PARSE_CID2_INDEX] != PRODTST_BLE_CID_2 ||
        PRODTST_BLE_VER_GET(ver_subtype) != PRODTST_BLE_VER ||
        PRODTST_BLE_SUBTYPE_GET(ver_subtype) !=  PRODTST_BLE_SUBTYPE){
        return -QM_EINVAL;
    }

    recv_handle = prodtst_ble_handle_get(mac);
    if(recv_handle == NULL){
        recv_handle = prodtst_ble_handle_create(mac);
        if(recv_handle == NULL){
            return -QM_ENOMEM;
        }
        memcpy(&recv_handle->src_addr[0], &mac[0], 6);
    }

    handle_frame = &recv_handle->frame;
    memcpy(&handle_frame->mac[0], &frame_mask->mac[0], 2);

    ret = prodtst_ble_event_notify(PRODTST_BLE_PRODTST_EVENT_ACTION_SEQ, (uint8_t*)recv_handle, sizeof(prodtst_ble_handle_t));
    if(ret != QM_EOK){
        if(!recv_handle->is_conn){
            prodtst_ble_handle_delete(recv_handle);
        }
        return -QM_EINVAL;
    }

    if(!memcmp(recv_handle->raw_data, recv_buff, recv_length)){
        return -QM_EINVAL;
    }

    recv_handle->raw_len = recv_length;
    memcpy(recv_handle->raw_data, recv_buff, recv_length);

  
    qm_event_post(QM_EVENT_PRODTST_BLE, PRODTST_BLE_EVENT_RECV, recv_handle, 0, QM_NO_WAIT);

    return QM_EOK;
}

int32_t prodtst_ble_comm_event_register(prodtst_ble_event_t event_cb)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx NULL");
    g_prodtst_ctx->event_cb = event_cb;
    return QM_EOK;
}

#if CONFIG_PRODTST_BLE_USE_EXT_SCAN
int32_t prodtst_ble_comm_data_push(uint8_t mac[6], uint8_t *buff, int length)
{
    QM_RETURN_ON_FALSE(buff, -QM_EINVAL, LOG_TAG, "buff NULL");
    QM_RETURN_ON_FALSE(length, -QM_EINVAL, LOG_TAG, "length 0");

    return ble_prodtst_scan_read(mac, buff, length);
}

#endif
