#include "qm.h"
#include "prodtst_client.h"

#if CONFIG_PRODTST_CLIENT_BLE_GATT_SUPPORT
#include "qm_work.h"
#include "qm_ringbuf.h"

#include "qm_at.h"
#include "qm_ble_gap.h"
#include "qm_ble_core.h"
#include "qm_ble_export.h"
#include "qm_ble_common.h"

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
#error "not support"
#endif

#define PRODTST_CLIENT_RECV_BUFF_LEN 128
#define PRODTST_CLIENT_BLE_ADV_NAME "xjtest-%d-%02x%02x"

typedef struct 
{
    uint8_t msg_id;
    uint8_t prodtst_start;
    char local_name[32];
    qm_work_t timout_work;
    
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_sem_t recv_sem;
    qm_mutex_t recv_mutex;
    qm_ringbuf_t ringbuff;
    uint8_t buff[PRODTST_CLIENT_RECV_BUFF_LEN];
#endif
}prodtst_client_ble_ctx_t;

static prodtst_client_ble_ctx_t *g_client_ctx = NULL;

static void *prodtst_client_ble_gatt_init(void);
static int prodtst_client_ble_gatt_deinit(void *handle);
static int prodtst_client_ble_gatt_send(void *handle, char *buf, int size);

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_ble_gatt_recv(void *handle, char *buf, int size, int timeout);
#else
extern void *prodtst_client_handle_get(prodtst_client_channel_t channel);
#endif

prodtst_client_link_t prodtst_client_ble_gatt_link_ctx = 
{
    .init   = prodtst_client_ble_gatt_init,
    .write  = prodtst_client_ble_gatt_send,
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    .read   = prodtst_client_ble_gatt_recv,
#endif
    .deinit = prodtst_client_ble_gatt_deinit,
};

static int qm_ble_prodtst_fn(uint8_t msg_id, uint8_t *buffer, uint32_t length)
{
#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
    void *handle = NULL;
#endif
    int ret = QM_AT_RESULT_OK;
    if(buffer == NULL || length == 0){
        return QM_AT_RESULT_FAILE;
    }
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_mutex_lock(&g_client_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif
    g_client_ctx->msg_id = msg_id;
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_mutex_unlock(&g_client_ctx->recv_mutex);
#endif

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_mutex_lock(&g_client_ctx->recv_mutex, QM_WAIT_FOREVER);
    ret = qm_ringbuf_push(&g_client_ctx->ringbuff, buffer, (int)length);
    qm_mutex_unlock(&g_client_ctx->recv_mutex);
    if (ret <= 0){
       return QM_AT_RESULT_FAILE;
    }

    qm_sem_signal(&g_client_ctx->recv_sem);
#else
    handle = prodtst_client_handle_get(PRODTST_CLIENT_CHANNEL_TYPE_BLE_GATT);

    prodtst_client_data_push(handle, buffer, length);
#endif

    return QM_EOK;
}

static void qm_ble_status_change_cb(qm_ble_event_t event)
{

    switch (event) {
        case QM_BLE_CONNECTED:
            QM_BLE_INFO("QM_BLE_CONNECTED");
            g_client_ctx->prodtst_start = QM_TRUE;
            qm_cancel_delayed_action(&g_client_ctx->timout_work);
            prodtst_client_event_post(PRODTST_CLIENT_ENTER_PRODTST_SUCCESS);
		break;
        case QM_BLE_DISCONNECTED:
            QM_BLE_INFO("QM_BLE_DISCONNECTED RESTART BLE ADV");
            if(g_client_ctx->prodtst_start){
                prodtst_client_event_post(PRODTST_CLIENT_EXIT_PRODTST_SUCCESS);
            }else{
                qm_ble_restart_advertising(); 
            }
            g_client_ctx->prodtst_start = QM_FALSE;
        break;

        case QM_BLE_AUTHENTICATED:
            QM_BLE_INFO("QM_BLE_AUTHENTICATED");
        break;

        default:
        break;
    }
}

static void qm_ble_set_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{

}

static void qm_ble_get_dev_status_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{

}

static void qm_ble_response_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length)
{
 
}

static void qm_extinfo_callback(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t len)
{

}

static void prodtst_client_timeout_action(void *arg)
{
    prodtst_client_ble_ctx_t *client_ble_ctx = (prodtst_client_ble_ctx_t *)arg;
    if(client_ble_ctx == NULL){
        return ;
    }

    prodtst_client_event_post(PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT);
}

void *prodtst_client_ble_gatt_init(void)
{
    int ret = QM_EOK;
    qm_bd_addr_t addr = {0};
    qm_ble_dev_info_t dinfo = {0};

    qm_ble_gap_get_local_addr(addr);
    prodtst_client_ble_ctx_t *client_ble_ctx = NULL;

    client_ble_ctx = (prodtst_client_ble_ctx_t *)qm_malloc(sizeof(prodtst_client_ble_ctx_t));
    if(client_ble_ctx == NULL){
        return NULL;
    }
    memset(client_ble_ctx, 0, sizeof(prodtst_client_ble_ctx_t));

    g_client_ctx = client_ble_ctx;

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    ret = qm_sem_new(&client_ble_ctx->recv_sem, 0);
    if (ret != QM_EOK){
        goto _error;
    }

    ret = qm_mutex_new(&client_ble_ctx->recv_mutex);
    if (ret != QM_EOK){
        goto _error;
    }

    ret = qm_ringbuf_init(&client_ble_ctx->ringbuff, client_ble_ctx->buff, CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
    if (ret != QM_EOK){
        goto _error;
    }
#endif

    dinfo.product_id = CONFIG_PRODTST_CLIENT_BLE_GATT_PID;
    dinfo.product_secret = 0;
    dinfo.dev_adv_mac = addr;

    qm_ble_init(&dinfo,
                 qm_ble_status_change_cb,
                 qm_ble_set_dev_status_callback,
                 qm_ble_get_dev_status_callback,
                 qm_ble_response_callback,
                 qm_extinfo_callback,
                 NULL,
                 NULL);

    qm_ble_core_device_id_set(addr, 6);
    qm_snprintf(g_client_ctx->local_name, 
                32, 
                PRODTST_CLIENT_BLE_ADV_NAME, 
                CONFIG_PRODTST_CLIENT_BLE_GATT_PID, 
                addr[4], addr[5]);

    qm_ble_prodtst_register_callback(qm_ble_prodtst_fn);

    ret = qm_ble_start_advertising(2, 0, 0, g_client_ctx->local_name);
    if (ret != QM_EOK){
        goto _error;
    }


    qm_post_delayed_action(&g_client_ctx->timout_work,  
                            prodtst_client_timeout_action,
                            g_client_ctx,
                            CONFIG_PRODTST_CLIENT_BLE_GATT_ENTER_TIMEOUT_S * 1000);
    return (void *)client_ble_ctx; 

_error:

    qm_ble_end();
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    if(qm_sem_is_valid(&client_ble_ctx->recv_sem)){
        qm_sem_free(&client_ble_ctx->recv_sem);
    }

    if(qm_mutex_is_valid(&client_ble_ctx->recv_mutex)){
        qm_mutex_free(&client_ble_ctx->recv_mutex);
    }

#endif
    if(client_ble_ctx){
        qm_free(client_ble_ctx);
        client_ble_ctx = NULL;
    }
    return NULL;
}

static int prodtst_client_ble_gatt_send(void *handle, char *buf, int size)
{
    uint8_t msg_id = 0;
    prodtst_client_ble_ctx_t *client_ble_ctx = (prodtst_client_ble_ctx_t *)handle;
    
    if(handle == NULL || buf == NULL || size == 0){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_mutex_lock(&client_ble_ctx->recv_mutex, QM_WAIT_FOREVER);
#endif    
    msg_id = client_ble_ctx->msg_id;
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_mutex_unlock(&client_ble_ctx->recv_mutex);
#endif
    return qm_ble_prodtst_post(msg_id, (uint8_t *)buf, (uint32_t)size);
}

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_uart_recv(void *handle, char *buf, int size, int timeout)
{
    int isempty = 0;
    int temp_len = 0;
    int recv_len = 0;
    uint32_t t_left = 0;
    qm_err_t ret = QM_EOK;
    uint16_t ext_port = 0;
    qm_utils_time_t time = {0};
    prodtst_client_ble_ctx_t *client_ble_ctx = (prodtst_client_ble_ctx_t *)handle;
    
    if(handle == NULL || (buf == NULL) || (size == 0)){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }
    
    qm_utils_time_countdown_ms(&time, timeout);   
    qm_utils_time_init(&time);

    do{
        t_left = qm_utils_time_left(&time);

        qm_mutex_lock(&client_ble_ctx->recv_mutex, QM_WAIT_FOREVER);
        isempty = qm_ringbuf_isempty(&client_ble_ctx->ringbuff);
        qm_mutex_unlock(&client_ble_ctx->recv_mutex);

        if(!isempty){
            qm_mutex_lock(&client_ble_ctx->recv_mutex, QM_WAIT_FOREVER);
            temp_len = qm_ringbuf_pop(&client_ble_ctx->ringbuff, (uint8_t *)(buf + recv_len), size - recv_len);
            qm_mutex_unlock(&client_ble_ctx->recv_mutex);

            if(temp_len > 0){
                recv_len += temp_len;
            }else{
                ret = -QM_ERROR;
                break;
            }
        }else{
            if(t_left == 0){
                break;
            }
            ret = qm_sem_wait(&client_ble_ctx->recv_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret;
}
#endif

int prodtst_client_ble_gatt_deinit(void *handle)
{
    int ret = QM_AT_RESULT_OK;
    prodtst_client_ble_ctx_t *client_ble_ctx = (prodtst_client_ble_ctx_t *)handle;
    if (handle == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    ret = qm_ble_end();
    if (ret != QM_AT_RESULT_OK){
         return QM_AT_RESULT_FAILE;
    }

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    ret = qm_sem_free(&client_ble_ctx->recv_sem);
    if (ret != QM_AT_RESULT_OK){
         return QM_AT_RESULT_FAILE;
    }    

    ret = qm_mutex_free(&client_ble_ctx->recv_mutex);
    if (ret != QM_AT_RESULT_OK){
         return QM_AT_RESULT_FAILE;
    }  
#endif
    qm_free(handle);
    handle = NULL;

    return ret;
}

#endif
