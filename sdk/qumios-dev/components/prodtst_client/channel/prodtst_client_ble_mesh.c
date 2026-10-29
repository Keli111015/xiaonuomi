#include "qm.h"
#include "prodtst_client.h"

#if CONFIG_PRODTST_CLIENT_BLE_MESH_SUPPORT
#include "qm_ble_gap.h"
#include "data_channel.h"
#include "qm_utils_timer.h"
#include "prodtst_ble_comm.h"
#include "prodtst_ble_client.h"
#include "qm_work.h"
#include "qm_event.h"

#if 1
#define LOG_TAG "prodtst_ble"
#endif

#define PRODTST_BLE_DATA_SEND_NOTIFY            (0x01)
#define PRODTST_BLE_DATA_SEND_NOTIFY_NEED_ACK   (0x02)

/**
 * Event base used by prodtst BLE mesh client dispatch.
 */
static qm_event_base_t const PRODTST_CLIENT_MESH_EVENT = "PRODTST_CLIENT_MESH_EVENT";

#define PRODTST_BLE_RESENT_COUNT                (5)
#define PRODTST_BLE_REPORT_TIME_MS              (500)
#define PRODTST_BLE_RECV_TIMEOUT_MS             (50)


#define PRODTST_BLE_CMD_END_OK         "OK\r\n"
#define PRODTST_BLE_CMD_END_ERROR      "ERROR"

typedef struct 
{
    uint16_t pid; //pid
    uint8_t scan_time; //扫描时间
}prodtst_client_ble_config_t;

typedef struct 
{

    int test_num; //测试数量
    uint8_t test_id; //测试ID
    uint8_t test_status; //测试状态
    uint8_t timer_index;

    prodtst_ble_type_t test_type; // 测试类型
    char at_cmd[CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN];

    qm_timer_t timerout;
    qm_work_t send_dely_time;
    void *dc_client;
    void *dc_server;

    int resend_count;
}prodtst_client_ble_ctx_t;

#if !CONFIG_PRODTST_CLINET_BLE_MESH_ALLOC
static prodtst_client_ble_ctx_t _prodtst_ble_ctx = {0};
#endif
static prodtst_client_ble_config_t g_mesh_config = {0};
static prodtst_client_ble_ctx_t *g_prodtst_ble_ctx = NULL;

static void *prodtst_client_mesh_init(void);
static int prodtst_client_mesh_deinit(void *handle);
static int prodtst_client_mesh_send(void *handle, char *buf, int size);
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_mesh_recv(void *handle, char *buf, int size, int timeout);
#else
extern void *prodtst_client_handle_get(prodtst_client_channel_t channel);
#endif
static void prodtst_client_test_event_callback(void *event_handler_arg, qm_event_base_t event_base,
                                               int32_t event_id, void *event_data, size_t event_data_size);
static int32_t prodtst_ble_slave_fac_setup(prodtst_ble_prodtst_t *prodtst);
static int prodtst_ble_slave_exec_cmd_event_notify(uint8_t type, prodtst_ble_data_test_info_t *test_info);


prodtst_client_link_t prodtst_client_ble_mesh_link_ctx = 
{
    .init   = prodtst_client_mesh_init,
    .write  = prodtst_client_mesh_send,
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    .read   = prodtst_client_mesh_recv,
#endif
    .deinit = prodtst_client_mesh_deinit,
};

int prodtst_client_ble_mesh_pid_set(uint16_t pid, uint8_t scan_time)
{
    g_mesh_config.pid = pid;
    g_mesh_config.scan_time = scan_time;
    return QM_EOK;
}

static void prodtst_ble_timerout_cb(qm_timer_t *timer, void *arg)
{
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;
    switch (slave_ctx->timer_index)
    {
    #if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT 
        case PRODTST_CLINET_PRODTST_RESULT_FAILED:
    
            if(slave_ctx->test_type == PRODTST_BLE_TYPE_ADV  && slave_ctx->resend_count++ < PRODTST_BLE_RESENT_COUNT){
                prodtst_ble_client_prodtst_report(slave_ctx->test_id, (uint8_t*)slave_ctx->at_cmd, strlen(slave_ctx->at_cmd));
                qm_timer_start(&slave_ctx->timerout);
                return;
            }
        break;
    #endif
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
    
        case PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT:
            if(slave_ctx->test_type == PRODTST_BLE_TYPE_CONN && slave_ctx->resend_count++ < PRODTST_BLE_RESENT_COUNT){
                prodtst_ble_client_connect();
                qm_timer_start(&slave_ctx->timerout);
                return;
            }
        break;

        case PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT:
            if(slave_ctx->test_type == PRODTST_BLE_TYPE_CONN && slave_ctx->resend_count++ < PRODTST_BLE_RESENT_COUNT){
                qm_timer_start(&slave_ctx->timerout);
                return;
            }
        break;
    #endif

        default:
        break;
    }

    slave_ctx->resend_count = 0;
    prodtst_client_event_post(slave_ctx->timer_index);
}
#if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
static int prodtst_ble_conn_type_state_set(prodtst_ble_prodtst_t *prodtst)
{
    static int set_connect = 0;
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;
    QM_RETURN_ON_FALSE(prodtst, -QM_EINVAL, LOG_TAG, "prodtst NULL");
    QM_RETURN_ON_FALSE(slave_ctx, -QM_EINVAL, LOG_TAG, "slave_ctx NULL");

    if(prodtst->prodtst_status == slave_ctx->test_status){
        return -QM_EINVAL;
    }

    switch (prodtst->prodtst_status)
    {
        case 1: //enter
            if(!set_connect){
                set_connect = 1;
                prodtst_ble_slave_fac_setup(prodtst);
            }
            prodtst_ble_client_connect();
        break;
        case 2: //error
            prodtst_client_event_post(PRODTST_CLINET_PRODTST_RESULT_FAILED);
        break;  
        case 0: //exit
            set_connect = 0;
            prodtst_ble_slave_fac_setup(prodtst);  
        break;  
        default:
        break;
    }
    return QM_EOK;
}
#endif
static int32_t prodtst_ble_slave_event_fn(prodtst_ble_client_event_type_t event_type, prodtst_ble_client_event_info_t *p_event)
{
    int32_t ret = QM_EOK;
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;

    switch (event_type)
    {
        case PRODTST_BLE_CLIENT_EVENT_REQ:
            #if 0
            if(slave_ctx->test_status && p_event->event_info.event == PRODTST_BLE_CLIENT_CONNECT){
                ret = -QM_EINVAL;
            }
            #endif
        break;

        case PRODTST_BLE_CLIENT_STATE_SET:
        #ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "PRODTST_BLE_CLIENT_STATE_SET %d %d!!!", p_event->prodtst_info.prodtst_status, p_event->prodtst_info.prodtst_type);
        #endif
            if(p_event->prodtst_info.prodtst_type == PRODTST_BLE_TYPE_ADV){
                prodtst_ble_slave_fac_setup(&p_event->prodtst_info);
            }else{
        #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
                prodtst_ble_conn_type_state_set(&p_event->prodtst_info);
        #endif
            }
            slave_ctx->test_type = p_event->prodtst_info.prodtst_type;
        break;
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
        case PRODTST_BLE_CLIENT_TEST_REQ:
            prodtst_ble_slave_exec_cmd_event_notify(PRODTST_BLE_DATA_SEND_NOTIFY_NEED_ACK, &p_event->gen_info);
        break;
    #endif
        case PRODTST_BLE_CLIENT_TEST:

            qm_timer_stop(&slave_ctx->timerout);
            prodtst_ble_slave_exec_cmd_event_notify(PRODTST_BLE_DATA_SEND_NOTIFY, &p_event->gen_info);
        break;
    #if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT 
        case PRODTST_BLE_CLIENT_REPORT:
        #ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "PRODTST_BLE_CLIENT_REPORT!!!");
        #endif
            qm_timer_stop(&g_prodtst_ble_ctx->timerout);
            prodtst_client_event_post(PRODTST_CLINET_PRODTST_RESULT_FAILED);
        break; 
    #endif
    
        default:
        break;
    }

    return ret;
}

static int32_t prodtst_ble_slave_fac_setup(prodtst_ble_prodtst_t *prodtst)
{
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;
    QM_RETURN_ON_FALSE(prodtst, -QM_EINVAL, LOG_TAG, "prodtst NULL");
    QM_RETURN_ON_FALSE(slave_ctx, -QM_EINVAL, LOG_TAG, "slave_ctx NULL");

    if(prodtst->prodtst_status == slave_ctx->test_status){
        return -QM_EINVAL;
    }

    if(prodtst->prodtst_status){
        qm_timer_stop(&slave_ctx->timerout);
        slave_ctx->test_status = prodtst->prodtst_status;
        slave_ctx->timer_index = PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT;
        prodtst_client_event_post(PRODTST_CLIENT_ENTER_PRODTST_SUCCESS);
#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
        g_prodtst_ble_ctx->dc_client = data_channel_client_creat();
        g_prodtst_ble_ctx->dc_server = data_channel_server_creat(CONFIG_PRODTST_BLE_BUFF_LEN, CONFIG_PRODTST_BLE_BUFF_LEN);
        data_channel_connect(g_prodtst_ble_ctx->dc_server, g_prodtst_ble_ctx->dc_client);
#endif
    }else{

        if(!slave_ctx->test_num){  //未开始时不允许退出产测
            return -QM_EINVAL;
        }

        slave_ctx->test_num = 0;
        qm_timer_stop(&slave_ctx->timerout);
        slave_ctx->test_status = prodtst->prodtst_status;
        prodtst_client_event_post(PRODTST_CLIENT_EXIT_PRODTST_SUCCESS);
    }

    return QM_EOK;
}

static void prodtst_client_test_notify(uint8_t type, void *arg)
{
    int ret = QM_EOK;
    uint32_t read_len = 0;
    uint32_t client_recv_len= 0;
    uint8_t ch = 0, last_ch = 0;
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;
    char str_cmd[CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN] = {0};
    char client_recv_cmd[CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN] = {0};

    slave_ctx->resend_count = 0;

    while(1){
        ch = 0;
        last_ch = 0;
        read_len = 0;
        memset(str_cmd, 0, CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
        while (1)
        {
            ret = data_channel_read(slave_ctx->dc_client, &ch, 1, QM_WAIT_FOREVER);
            if(ret <= 0){
                goto _next;
            }

            if (read_len <= CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN - 2){
                str_cmd[read_len++] = ch;
            }

            if ((ch == '\n' && last_ch == '\r')){
                break;
            }

            last_ch = ch;
        }

        if (client_recv_len + read_len < CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN){
            memcpy(client_recv_cmd + client_recv_len, str_cmd, read_len);
            client_recv_len += read_len;
        } 

        if(memcmp(str_cmd, PRODTST_BLE_CMD_END_OK, strlen(PRODTST_BLE_CMD_END_OK)) == 0){
            ret = QM_EOK;
            break;
        }else if(strstr((const char *)str_cmd, PRODTST_BLE_CMD_END_ERROR)){
            ret = -QM_ERROR;
            break;
        }  
    }
_next:
    if(!client_recv_len){
        return;
    }

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "ret %d exec_cmd %s", ret, client_recv_cmd);
#endif

    slave_ctx->test_num++;

    switch (type)
    {
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
        case PRODTST_BLE_DATA_SEND_NOTIFY_NEED_ACK:
            prodtst_ble_client_test(slave_ctx->test_id, (uint8_t*)client_recv_cmd, strlen(client_recv_cmd));
            memcpy(slave_ctx->at_cmd, client_recv_cmd, strlen(client_recv_cmd));
        break;
    #endif
    #if CONFIG_PRODTST_BLE_CLIENT_ADV_REPORT 
        case PRODTST_BLE_DATA_SEND_NOTIFY:
            if(ret == QM_EOK){
                break;
            }
            slave_ctx->test_status = QM_FALSE;
            slave_ctx->timer_index = PRODTST_CLINET_PRODTST_RESULT_FAILED;
        #if !CONFIG_QM_AT_SERVER_OS_SUPPORT
            qm_event_handler_unregister(PRODTST_CLIENT_MESH_EVENT, QM_EVENT_ANY_ID,
                                             prodtst_client_test_event_callback, NULL);
        #endif
            prodtst_ble_client_prodtst_report(slave_ctx->test_id, (uint8_t*)client_recv_cmd, strlen(client_recv_cmd));
            memcpy(slave_ctx->at_cmd, client_recv_cmd, strlen(client_recv_cmd));
        break;
    #endif    
        default:
        break;
    }

#if CONFIG_PRODTST_CLIENT_BLE_MESH_RECV_TIMEOUT_S
    qm_timer_change(&slave_ctx->timerout, CONFIG_PRODTST_CLIENT_BLE_MESH_RECV_TIMEOUT_S);
    qm_timer_start(&slave_ctx->timerout);
#endif
}

static void prodtst_client_test_event_callback(void *event_handler_arg, qm_event_base_t event_base,
                                               int32_t event_id, void *event_data, size_t event_data_size)
{
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data);
    QM_UNUSED_ARG(event_data_size);

    switch (event_id)
    {
        case PRODTST_BLE_DATA_SEND_NOTIFY:
        case PRODTST_BLE_DATA_SEND_NOTIFY_NEED_ACK:
            prodtst_client_test_notify(event_id, event_handler_arg);
        break;

        default:
        break;
    }
}

static int prodtst_ble_slave_exec_cmd_event_notify(uint8_t type, prodtst_ble_data_test_info_t *test_info)
{
    int test_len = 0;
    prodtst_client_ble_ctx_t *slave_ctx = g_prodtst_ble_ctx;
    char str_cmd[CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN] = {0};
    QM_RETURN_ON_FALSE(test_info, -QM_EINVAL, LOG_TAG, "test_info NULL");

#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
    void *handle = prodtst_client_handle_get(PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH);
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle NULL");
#endif

#if !CONFIG_PRODTST_CLIENT_RUN_ALWAYS
    if(slave_ctx->test_status == QM_FALSE){
        return QM_EOK;
    }
#endif

    test_len =  strlen(slave_ctx->at_cmd);
    if(slave_ctx->test_id == test_info->test_id){
    #if CONFIG_PRODTST_BLE_CLIENT_TYPE_CONN 
        if(test_len){
            prodtst_ble_client_test(slave_ctx->test_id, (uint8_t*)slave_ctx->at_cmd, test_len); //resend reply
        }
    #endif
        return QM_EOK;
    }

    memcpy(str_cmd, test_info->pdata, strlen((const char *)test_info->pdata));
#if CONFIG_PRODTST_CLIENT_BLE_MESH_WITH_WRAP_ENTER
    memcpy(str_cmd + strlen((const char *)test_info->pdata), "\r\n", 2);
#endif

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "PRODTST_TEST: ID %d data:%.*s...!!!", test_info->test_id, strlen(str_cmd), str_cmd);
#endif

    slave_ctx->test_id = test_info->test_id;
    memset(slave_ctx->at_cmd, 0, CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
    
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    data_channel_write(slave_ctx->dc_client, (uint8_t *)str_cmd, strlen(str_cmd), 0);
#else
    prodtst_client_data_push(handle, str_cmd, strlen((char*)str_cmd));
#endif
    return QM_EOK;
}

static void prodtst_client_post_delay(void *arg)
{
    if(g_prodtst_ble_ctx->test_type == PRODTST_BLE_TYPE_ADV){
        qm_event_post(PRODTST_CLIENT_MESH_EVENT, PRODTST_BLE_DATA_SEND_NOTIFY, NULL, 0, QM_NO_WAIT);
    }else{
        qm_event_post(PRODTST_CLIENT_MESH_EVENT, PRODTST_BLE_DATA_SEND_NOTIFY_NEED_ACK, NULL, 0, QM_NO_WAIT);
    }
}

static int prodtst_client_mesh_send(void *handle, char *buf, int size)
{
    int ret = QM_EOK;

    qm_cancel_delayed_action(&g_prodtst_ble_ctx->send_dely_time);
    ret = data_channel_write(g_prodtst_ble_ctx->dc_server, (uint8_t*)buf, size, 0);
    qm_post_delayed_action(&g_prodtst_ble_ctx->send_dely_time, prodtst_client_post_delay, NULL, PRODTST_BLE_RECV_TIMEOUT_MS);

    return ret;
}

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_mesh_recv(void *handle, char *buf, int size, int timeout)
{
    return data_channel_read(g_prodtst_ble_ctx->dc_server, (uint8_t*)buf, size, timeout);
}
#endif

static void *prodtst_client_mesh_init(void)
{
    prodtst_ble_client_param_t client_param = {0};

    client_param.ble_param.encrypted = 0;
    client_param.ble_param.data_type = PRODTST_BLE_DATA_TYPE_AT;

    if(g_mesh_config.pid){
        client_param.ble_param.pid = g_mesh_config.pid;
    }else{
        client_param.ble_param.pid = CONFIG_PRODTST_CLIENT_BLE_MESH_PID;
    }

    client_param.ble_param.min_rssi = CONFIG_PRODTST_CLIENT_BLE_MESH_MIN_RSSI;

#if CONFIG_PRODTST_CLINET_BLE_MESH_ALLOC
    g_prodtst_ble_ctx = (prodtst_client_ble_ctx_t *)qm_malloc(sizeof(prodtst_client_ble_ctx_t));
    if(g_prodtst_ble_ctx == NULL){
        return NULL;
    }
#else
    g_prodtst_ble_ctx = &_prodtst_ble_ctx;
#endif
    memset(g_prodtst_ble_ctx, 0, sizeof(prodtst_client_ble_ctx_t));

    prodtst_ble_client_init(&client_param);
    prodtst_ble_client_event_register(prodtst_ble_slave_event_fn);
    qm_event_handler_register(PRODTST_CLIENT_MESH_EVENT, QM_EVENT_ANY_ID,
                                   prodtst_client_test_event_callback, NULL);

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    g_prodtst_ble_ctx->dc_client = data_channel_client_creat();
    g_prodtst_ble_ctx->dc_server = data_channel_server_creat(CONFIG_PRODTST_BLE_BUFF_LEN, CONFIG_PRODTST_BLE_BUFF_LEN);
    data_channel_connect(g_prodtst_ble_ctx->dc_server, g_prodtst_ble_ctx->dc_client);
#endif
    g_prodtst_ble_ctx->timer_index = PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT;

    if(g_mesh_config.scan_time){
        qm_timer_new(&g_prodtst_ble_ctx->timerout, prodtst_ble_timerout_cb, NULL, 
                    g_mesh_config.scan_time * 1000, 0);
    }else{
        qm_timer_new(&g_prodtst_ble_ctx->timerout, prodtst_ble_timerout_cb, NULL, 
                    CONFIG_PRODTST_CLIENT_BLE_MESH_ENTER_TIMEOUT_S * 1000, 0);
    }
    qm_timer_start(&g_prodtst_ble_ctx->timerout);

    return (void *)g_prodtst_ble_ctx->dc_server;

}

static int prodtst_client_mesh_deinit(void *handle)
{
    QM_RETURN_ON_FALSE(g_prodtst_ble_ctx, -QM_EINVAL, LOG_TAG, "handle NULL");
    

    qm_event_handler_unregister(PRODTST_CLIENT_MESH_EVENT, QM_EVENT_ANY_ID,
                                     prodtst_client_test_event_callback, NULL);

    prodtst_ble_client_deinit();
    
    qm_timer_stop(&g_prodtst_ble_ctx->timerout);
    qm_timer_free(&g_prodtst_ble_ctx->timerout);
    
    if(g_prodtst_ble_ctx->dc_server){
        data_channel_destroy(g_prodtst_ble_ctx->dc_server);
        g_prodtst_ble_ctx->dc_server = NULL;
    }

    if(g_prodtst_ble_ctx->dc_client){
        data_channel_destroy(g_prodtst_ble_ctx->dc_client);
        g_prodtst_ble_ctx->dc_client = NULL;
    }

#if PRODTST_CLINET_BLE_MESH_ALLOC
    qm_free(g_prodtst_ble_ctx);
#endif
    g_prodtst_ble_ctx = NULL;

    return QM_EOK;
}

#endif
