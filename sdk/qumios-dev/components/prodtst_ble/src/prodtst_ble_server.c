#include "prodtst_ble_comm.h"

#if CONFIG_PRODTST_BLE_SERVER

#include "prodtst_ble_server.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"

#define PRODTST_BLE_SERVER_RESEND_COUNT (2)
#define PRODTST_BLE_SERVER_CONN_MIN_ID  (1)
#define PRODTST_BLE_TMP_BUFF_LEN        (32)

#define LOG_TAG "prodtst_ble_server"

typedef struct ble_server_block
{
    uint8_t conn_id;
    uint8_t conn_addr[6];
    prodtst_ble_handle_t *handle;
    struct ble_server_block *next;
}ble_server_block_t;

typedef struct
{
    struct ble_server_block *conn_block;
}ble_server_ctx_t;

typedef struct 
{
    uint8_t local_addr[6];
    qm_timer_t report_id_timer;
    prodtst_ble_type_t channel_type; //产测类型 0：广播类产测 1：连接类产测
    prodtst_ble_server_event_t event_cb;
    ble_server_ctx_t conn_ctx;
}prodtst_ble_server_ctx_t;

static prodtst_ble_server_ctx_t *g_prodtst_ctx = NULL;

int32_t prodtst_ble_server_close(uint8_t conn_id)
{
    ble_server_block_t *m_conn_block = NULL;
    ble_server_block_t *conn_block_pre = NULL;

    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx->conn_ctx.conn_block, -QM_EINVAL, LOG_TAG, "conn_block null");
    
    m_conn_block = g_prodtst_ctx->conn_ctx.conn_block;

    if(conn_id == m_conn_block->conn_id){
        g_prodtst_ctx->conn_ctx.conn_block = m_conn_block->next;
        prodtst_ble_handle_delete(m_conn_block->handle);
        qm_free(m_conn_block);
        return QM_EOK;
    }

    while(m_conn_block){
        if(conn_id == m_conn_block->conn_id){
            m_conn_block->handle->is_conn = QM_FALSE;
            conn_block_pre->next = m_conn_block->next;
            prodtst_ble_handle_delete(m_conn_block->handle);
            qm_free(m_conn_block);
            break;
        }
        conn_block_pre = m_conn_block;
        m_conn_block = m_conn_block->next;
    }

    return QM_EOK;
}

int32_t prodtst_ble_server_open(void *handle, uint8_t *conn_id)
{
    uint8_t id = 1;
    ble_server_block_t *conn_block = NULL;
    ble_server_block_t *m_conn_block = NULL;
    ble_server_block_t *conn_block_pre = NULL;
    prodtst_ble_handle_t *recv_handle = (prodtst_ble_handle_t *)handle;

    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle null");
    QM_RETURN_ON_FALSE(conn_id, -QM_EINVAL, LOG_TAG, "conn_id null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");

    m_conn_block = g_prodtst_ctx->conn_ctx.conn_block;
    while(m_conn_block){
        if(id != m_conn_block->conn_id){
            break;
        }
        id++;
        m_conn_block = m_conn_block->next;
    }
    
    conn_block = (ble_server_block_t *)qm_malloc(sizeof(ble_server_block_t));
    if(conn_block == NULL){
        return -QM_ENOMEM;
    }
    memset(conn_block, 0, sizeof(ble_server_block_t));

    *conn_id = id;
    conn_block->conn_id = id;
    conn_block->handle = recv_handle;
    conn_block->handle->is_conn = QM_TRUE;
    memcpy(&conn_block->conn_addr[0], &recv_handle->src_addr[0], 6);

    if(g_prodtst_ctx->conn_ctx.conn_block == NULL){
        g_prodtst_ctx->conn_ctx.conn_block = conn_block; 
        conn_block->next = NULL;
        return QM_EOK;
    }

    m_conn_block = g_prodtst_ctx->conn_ctx.conn_block;
    while(m_conn_block){
        conn_block_pre = m_conn_block;
        m_conn_block = m_conn_block->next;  
    }

    conn_block_pre->next = conn_block;
    conn_block->next = NULL;

    return QM_EOK;
}

int32_t prodtst_ble_server_conn_id_get(uint8_t mac[6], uint8_t *conn_id)
{
    int ret = -QM_ERROR;
    ble_server_block_t *conn_block = NULL;

    QM_RETURN_ON_FALSE(conn_id, -QM_EINVAL, LOG_TAG, "conn_id null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx->conn_ctx.conn_block, -QM_EINVAL, LOG_TAG, "conn_block null");

    conn_block = g_prodtst_ctx->conn_ctx.conn_block;
    while(conn_block){
        if(!memcmp(&conn_block->conn_addr[0], &mac[0], 6)){
            ret = QM_EOK;
            *conn_id = conn_block->conn_id;
            break;
        }
        conn_block = conn_block->next;  
    }
    return ret;
}

int32_t prodtst_ble_server_conn_mac_get(uint8_t conn_id, uint8_t mac[6])
{
    int ret = -QM_ERROR;
    ble_server_block_t *conn_block = NULL;

    QM_RETURN_ON_FALSE(conn_id, -QM_EINVAL, LOG_TAG, "conn_id null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx->conn_ctx.conn_block, -QM_EINVAL, LOG_TAG, "conn_block null");

    conn_block = g_prodtst_ctx->conn_ctx.conn_block;
    while(conn_block){
        if(conn_id == conn_block->conn_id){
            ret = QM_EOK;
            memcpy(&mac[0], &conn_block->conn_addr[0], 6);
            break;
        }
        conn_block = conn_block->next;  
    }
    return ret;
}

static void qm_timer_callback(qm_timer_t *timer, void *arg)
{
    qm_timer_free(&g_prodtst_ctx->report_id_timer);
    for(int id = 1; id < CONFIG_PRODTST_BLE_MAX_CONN_NUM; id++){
        prodtst_ble_server_close(id);
    }
}

static void prodtst_server_connect_ack(uint8_t mac[6], uint8_t conn_id, uint8_t send_count)
{
    for(int index = 0; index < send_count; index++){
        prodtst_ble_comm_send(PRODTST_BLE_CMD_CONNECT, &mac[0], QM_FALSE, &conn_id, sizeof(uint8_t));
    }
}

static int prodtst_ble_event_notify(prodtst_ble_prodtst_event_t event, uint8_t *data, int length)
{
    int ret = QM_EOK;
    uint8_t conn_id = 0;
    prodtst_ble_frame_cmd_t cmd;
    prodtst_ble_server_event_info_t event_info = {0};
    prodtst_ble_handle_t *handle = (prodtst_ble_handle_t *)data;

    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data null");
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx null");

    if(handle->is_conn){
        ret = prodtst_ble_server_conn_id_get(&handle->src_addr[0], &conn_id);
        if(ret != QM_EOK){
            return ret;
        }

        if(g_prodtst_ctx->local_addr[4] != handle->frame.mac[0] ||
            g_prodtst_ctx->local_addr[5] != handle->frame.mac[1] ){
            return -QM_EINVAL;
        }
    }

    if(event == PRODTST_BLE_PRODTST_EVENT_ACTION_SEQ){
        return QM_EOK;
    }

    cmd = (prodtst_ble_frame_cmd_t)handle->frame.frame_cmd.cmd;

    switch (cmd)
    {
        case PRODTST_BLE_CMD_TEST:

            event_info.test_info.gen_info.test_id = handle->pdata[0];
            if(handle->test_id == event_info.test_info.gen_info.test_id){
                break;
            }
            handle->test_id = event_info.test_info.gen_info.test_id;
            if(handle->bytes_received > PRODTST_BLE_TEST_ID_LEN){
                event_info.test_info.gen_info.pdata = handle->pdata + PRODTST_BLE_TEST_ID_LEN;
                event_info.test_info.gen_info.length = handle->bytes_received - PRODTST_BLE_TEST_ID_LEN;
            }
            memcpy(&event_info.test_info.conn_info.conn_addr[0], &handle->src_addr[0], 6);
            g_prodtst_ctx->event_cb(PRODTST_BLE_SERVER_TEST, &event_info);
        break;

        case PRODTST_BLE_CMD_STATE_SET:

            memcpy(&event_info.code_info, handle->pdata, handle->bytes_received);
            g_prodtst_ctx->event_cb(PRODTST_BLE_SERVER_STATE_SET, &event_info);
        break;

        case PRODTST_BLE_CMD_CONNECT:

            event_info.conn_info.handle = (void *)handle;
            memcpy(&event_info.conn_info.conn_addr[0], &handle->src_addr[0], 6);
            if( g_prodtst_ctx->local_addr[4] != handle->frame.mac[0] ||
                g_prodtst_ctx->local_addr[5] != handle->frame.mac[1] ){
                ret = -QM_EINVAL;
                break;
            }

            if(handle->is_conn){
               break;
            }

            ret = g_prodtst_ctx->event_cb(PRODTST_BLE_SERVER_CONNECT, &event_info);
            if (ret != QM_EOK){
                break;
            }
                       
            prodtst_server_connect_ack(&handle->src_addr[0], conn_id, PRODTST_BLE_SERVER_RESEND_COUNT);
        break;

        case PRODTST_BLE_CMD_REPORT:

            event_info.test_info.gen_info.test_id = *handle->pdata;
            memcpy(&event_info.test_info.conn_info.conn_addr[0], &handle->src_addr[0], 6);

            ret = prodtst_ble_server_conn_id_get(handle->src_addr, &conn_id);
            if(ret == QM_EOK){
                prodtst_ble_comm_send(PRODTST_BLE_CMD_REPORT, &handle->src_addr[0], QM_FALSE,
                                                        &event_info.test_info.gen_info.test_id, sizeof(uint8_t));
                break;
            }

            ret = prodtst_ble_server_open(handle, &conn_id);
            if(ret != QM_EOK){
                break;
            }

            if(handle->bytes_received > PRODTST_BLE_TEST_ID_LEN){
                event_info.test_info.gen_info.pdata = handle->pdata + PRODTST_BLE_TEST_ID_LEN;
                event_info.test_info.gen_info.length = handle->bytes_received - PRODTST_BLE_TEST_ID_LEN;
            }
            
            if(!qm_timer_is_valid(&g_prodtst_ctx->report_id_timer)){
                qm_timer_new(&g_prodtst_ctx->report_id_timer, qm_timer_callback, NULL, CONFIG_PRODTST_BLE_CMD_TIMEOUT, 0);
            }else{
                qm_timer_stop(&g_prodtst_ctx->report_id_timer);
            }

            g_prodtst_ctx->event_cb(PRODTST_BLE_SERVER_REPORT, &event_info);
            prodtst_ble_comm_send(PRODTST_BLE_CMD_REPORT, &handle->src_addr[0], QM_FALSE,
                                                    &event_info.test_info.gen_info.test_id, sizeof(uint8_t));
            qm_timer_start(&g_prodtst_ctx->report_id_timer);
        break;        
        default:
        break;
    }

    return ret;
}

int32_t prodtst_ble_server_pid_change(uint16_t pid)
{
    return prodtst_ble_comm_pid_change(pid);
}


int32_t prodtst_ble_server_init(prodtst_ble_server_param_t *param)
{
    int ret = QM_EOK;
    QM_RETURN_ON_FALSE(param, -QM_EINVAL, LOG_TAG, "param null");

    g_prodtst_ctx = (prodtst_ble_server_ctx_t*)qm_malloc(sizeof(prodtst_ble_server_ctx_t));
    if(NULL == g_prodtst_ctx){
       return -QM_ENOMEM;
    }
    memset(g_prodtst_ctx, 0, sizeof(prodtst_ble_server_ctx_t));

    g_prodtst_ctx->channel_type = param->channel_type;
    ret = prodtst_ble_comm_init(PRODTST_BLE_CHANNEL_SERVER, &param->ble_param);
    if(ret != QM_EOK){
        goto _error;
    }

    qm_ble_gap_get_local_addr(&g_prodtst_ctx->local_addr[0]);
    prodtst_ble_comm_event_register(prodtst_ble_event_notify);
    return QM_EOK;

_error:
    prodtst_ble_server_deinit();
    return ret;
}

int32_t prodtst_ble_server_deinit(void)
{
    if(g_prodtst_ctx == NULL){
        return -QM_EINVAL;
    }

    prodtst_ble_comm_deinit();
    qm_free(g_prodtst_ctx);
    g_prodtst_ctx = NULL;
    return QM_EOK;
}

#if CONFIG_PRODTST_BLE_USE_EXT_SCAN
int32_t prodtst_ble_server_data_push(uint8_t mac[6], uint8_t *buff, int length)
{
    return prodtst_ble_comm_data_push(mac, buff, length);
}
#endif


int32_t prodtst_ble_server_event_register(prodtst_ble_server_event_t event_cb)
{
    if(g_prodtst_ctx == NULL){
        return -QM_EINVAL;
    }
    g_prodtst_ctx->event_cb = event_cb;
    return QM_EOK;
}

int32_t prodtst_ble_server_prodtst_status_set(uint8_t conn_id, prodtst_ble_prodtst_t status, uint8_t send_count)
{
    int ret = QM_EOK;
    uint8_t mac[6] = {0};
    uint8_t need_ack = 0;

    if(g_prodtst_ctx->channel_type == PRODTST_BLE_TYPE_CONN){
        need_ack = QM_TRUE;
        prodtst_ble_server_conn_mac_get(conn_id, mac);
    }

    for(int index = 0; index < send_count; index++){
        ret = prodtst_ble_comm_send(PRODTST_BLE_CMD_STATE_SET, &mac[0], need_ack, 
                                                (uint8_t *)&status, 
                                                sizeof(prodtst_ble_prodtst_t));
    }

    return ret;
}


int32_t prodtst_ble_server_test(uint8_t conn_id, uint8_t test_id, uint8_t *data, int len, uint8_t send_count)
{
    uint8_t mac[6] = {0};
    uint8_t *hex_data = NULL;
    uint8_t *p_hex_data = NULL;
    uint8_t need_ack = QM_FALSE;
    uint8_t hex_buff[PRODTST_BLE_TMP_BUFF_LEN] = {0};

    if(g_prodtst_ctx->channel_type == PRODTST_BLE_TYPE_CONN){
        need_ack = QM_TRUE;
    }

    if (len + 1 > PRODTST_BLE_TMP_BUFF_LEN){
        p_hex_data = (uint8_t *)qm_malloc(len + 1);
        if(p_hex_data == NULL){
            return -QM_ENOMEM;
        }
        memset(p_hex_data, 0, len + 1);
        hex_data = p_hex_data;
    }else{
        hex_data = hex_buff;
    }
    
    *hex_data = test_id;
    memcpy(hex_data + 1, data, len);

    if(g_prodtst_ctx->channel_type == PRODTST_BLE_TYPE_CONN){
        prodtst_ble_server_conn_mac_get(conn_id, mac);
    }
    
    for(int index = 0; index < send_count; index++){
        prodtst_ble_comm_send(PRODTST_BLE_CMD_TEST, &mac[0], need_ack, hex_data, len + 1);
    }

    if(p_hex_data){
        qm_free(p_hex_data);
        p_hex_data = NULL;
    }

    return QM_EOK;
}

#endif