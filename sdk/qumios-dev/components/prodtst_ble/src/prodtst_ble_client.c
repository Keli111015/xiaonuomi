#include "prodtst_ble_comm.h"

#if CONFIG_PRODTST_BLE_CLIENT
#include "prodtst_ble_client.h"
#include "qm_ble_gap.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_ble_gap.h"

#define LOG_TAG "prodtst_ble_client"
#define PRODTST_BLE_TMP_BUFF_LEN 64

typedef struct 
{
    uint8_t conn_addr[6];
    uint8_t dst_addr[6];
    prodtst_ble_type_t channel_type; //产测类型 0：广播类产测 1：连接类产测
    prodtst_ble_client_event_t event_cb;
}prodtst_ble_client_ctx_t;

static prodtst_ble_client_ctx_t *g_prodtst_ctx = NULL;
#if !CONFIG_PRODTST_BLE_USE_ALLOC
static prodtst_ble_client_ctx_t _prodtst_ctx = {0};
#endif

static int prodtst_ble_event_notify(prodtst_ble_prodtst_event_t event, uint8_t *data, int length)
{
    int ret = QM_EOK;
    prodtst_ble_frame_cmd_t cmd;
    prodtst_ble_client_event_info_t event_info = {0};
    prodtst_ble_handle_t *handle = (prodtst_ble_handle_t *)data;

    QM_RETURN_ON_FALSE(data, -QM_EINVAL, __exit, LOG_TAG, "data NULL");
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, __exit, LOG_TAG, "g_prodtst_ctx NULL");
    QM_RETURN_ON_FALSE(g_prodtst_ctx->event_cb, -QM_EINVAL, __exit, LOG_TAG, "event_cb NULL");

    if(handle->is_conn){
        ret = memcmp(&g_prodtst_ctx->conn_addr[0], &handle->src_addr[0], 6);
        if(ret != QM_EOK){
            return ret;
        }
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
        if(g_prodtst_ctx->channel_type == PRODTST_BLE_TYPE_CONN){
            ret = memcmp(&g_prodtst_ctx->dst_addr[4], &handle->frame.mac, 2);
            if(ret != QM_EOK){
                return ret;
            }
        }
    #endif
    }

    if(event == PRODTST_BLE_PRODTST_EVENT_ACTION_SEQ){
        return ret;
    }

    cmd = (prodtst_ble_frame_cmd_t)handle->frame.frame_cmd.cmd;

    switch (cmd)
    {
        case PRODTST_BLE_CMD_TEST:
            event_info.gen_info.test_id = handle->pdata[0];
            
            if(handle->bytes_received > PRODTST_BLE_TEST_ID_LEN){
                event_info.gen_info.pdata = handle->pdata + PRODTST_BLE_TEST_ID_LEN;
                event_info.gen_info.length = handle->bytes_received - PRODTST_BLE_TEST_ID_LEN;
            }
        #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
            if(handle->frame.fmsk.need_ack){
                g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_TEST_REQ, &event_info);
            }else{
                g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_TEST, &event_info);
            }
        #else
            g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_TEST, &event_info);
        #endif

        break;
        case PRODTST_BLE_CMD_STATE_SET:
            memcpy(&event_info.prodtst_info, handle->pdata, handle->bytes_received);
            g_prodtst_ctx->channel_type = event_info.prodtst_info.prodtst_type;

            memcpy(&g_prodtst_ctx->conn_addr[0], &handle->src_addr[0], 6);

            if(event_info.prodtst_info.prodtst_status == 1){
                handle->is_conn = QM_TRUE;
                qm_ble_gap_get_local_addr(g_prodtst_ctx->dst_addr); // 再次尝试获取本地地址
            }else{
                handle->is_conn = QM_FALSE;
            }

            ret = g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_STATE_SET, &event_info);
            if (ret != QM_EOK){
                break;
            }  
        break;
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
        case PRODTST_BLE_CMD_CONNECT:
            event_info.conn_info.conn_id = handle->pdata[0];
            memcpy(&g_prodtst_ctx->conn_addr[0], &handle->src_addr[0], 6);
            g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_CONNECT, &event_info);
        break;
    #endif
    #if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT
        case PRODTST_BLE_CMD_REPORT:
            event_info.code_info.error_code = *handle->pdata;
            g_prodtst_ctx->event_cb(PRODTST_BLE_CLIENT_REPORT, &event_info);
        break;  
    #endif      
        default:
        break;
    }
    return ret;
}

int32_t prodtst_ble_client_pid_change(uint16_t pid)
{
    return prodtst_ble_comm_pid_change(pid);
}

int32_t prodtst_ble_client_init(prodtst_ble_client_param_t *param)
{
	int ret = QM_EOK;
    QM_RETURN_ON_FALSE(param, -QM_EINVAL, LOG_TAG, "param NULL");

#if CONFIG_PRODTST_BLE_USE_ALLOC
    g_prodtst_ctx = (prodtst_ble_client_ctx_t*)qm_malloc(sizeof(prodtst_ble_client_ctx_t));
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_ENOMEM, LOG_TAG, "g_prodtst_ctx NULL");
#else
    g_prodtst_ctx = &_prodtst_ctx;
#endif
    memset(g_prodtst_ctx, 0, sizeof(prodtst_ble_client_ctx_t));

    ret = prodtst_ble_comm_init(PRODTST_BLE_CHANNEL_CLIENT, &param->ble_param);
    QM_GOTO_ON_ERROR(ret, _error, LOG_TAG, "prodtst_ble_comm_init error");
    
    qm_ble_gap_get_local_addr(g_prodtst_ctx->dst_addr);

    prodtst_ble_comm_event_register(prodtst_ble_event_notify);
_error:
    return ret;
}

int32_t prodtst_ble_client_deinit(void)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx NULL");
    prodtst_ble_comm_deinit();
#if CONFIG_PRODTST_BLE_USE_ALLOC
    qm_free(g_prodtst_ctx);
#endif
    g_prodtst_ctx = NULL;

    return QM_EOK;
}

int32_t prodtst_ble_client_event_register(prodtst_ble_client_event_t event_cb)
{
    QM_RETURN_ON_FALSE(g_prodtst_ctx, -QM_EINVAL, LOG_TAG, "g_prodtst_ctx NULL");

    g_prodtst_ctx->event_cb = event_cb;
    return QM_EOK;
}
#if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN
int32_t prodtst_ble_client_connect(void)
{
    return prodtst_ble_comm_send(PRODTST_BLE_CMD_CONNECT, 
                                &g_prodtst_ctx->conn_addr[0],
                                QM_TRUE, 
                                NULL, 
                                0);
}

int32_t prodtst_ble_client_test(uint8_t test_id, uint8_t *data, int len)
{
    uint8_t *hex_data = NULL;
    uint8_t *p_hex_data = NULL;
    uint8_t hex_buff[PRODTST_BLE_TMP_BUFF_LEN] = {0};

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
    
    hex_data[0] = test_id;
    memcpy(&hex_data[1], data, len);

    prodtst_ble_comm_send( PRODTST_BLE_CMD_TEST, 
                                            &g_prodtst_ctx->conn_addr[0],
                                            QM_FALSE, 
                                            hex_data, 
                                            len + 1);
    if(p_hex_data){
        qm_free(p_hex_data);
        p_hex_data = NULL;
    }
    return QM_EOK;
}
#endif

#if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT
int32_t prodtst_ble_client_prodtst_report(uint8_t test_id, uint8_t *data, int len)
{
    uint8_t *hex_data = NULL;
    uint8_t *p_hex_data = NULL;
    uint8_t hex_buff[PRODTST_BLE_TMP_BUFF_LEN] = {0};

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

    prodtst_ble_comm_send( PRODTST_BLE_CMD_REPORT, 
                                            &g_prodtst_ctx->conn_addr[0],
                                            QM_TRUE, 
                                            hex_data, 
                                            len + 1);
    if(p_hex_data){
        qm_free(p_hex_data);
        p_hex_data = NULL;
    }
    return QM_EOK;
}
#endif
#endif
