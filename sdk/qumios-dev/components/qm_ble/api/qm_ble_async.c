#include "qm_ble_export.h"
#include "qm_ble_common.h"
#include "qm_ble_transport.h"
#include "qm_ble_async.h"
#include "qm_ble_bzopt.h"

#if QB_ENABLE_POST_ASYNC


#define  MAX_ASYNC_LIST_NUM   10

static qm_ble_async_t g_qm_ble_async;

uint32_t qm_ble_async_init(void)
{   
    memset(&g_qm_ble_async, 0, sizeof(qm_ble_async_t));
    qm_ble_mutex_new(&g_qm_ble_async.mutex);
    g_qm_ble_async.list = qm_list_new();
    if(g_qm_ble_async.list == NULL){
        return QB_EMEM;
    }
    
    return QB_SUCCESS;
}

uint32_t qm_ble_async_deinit(void)
{   
    qm_ble_mutex_free(&g_qm_ble_async.mutex);

    qm_list_destroy(g_qm_ble_async.list);
    g_qm_ble_async.list = NULL;
    g_qm_ble_async.is_busy = 0;
    
    return QB_SUCCESS;
}



void qm_ble_async_done(void)
{
    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    g_qm_ble_async.is_busy = 0;
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);
}

void qm_ble_async_busy(void)
{
    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    g_qm_ble_async.is_busy = 1;
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);
}


static uint8_t async_tx_msg_id_get(void)
{
    uint8_t msg_id = 0;
    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    msg_id = qm_ble_tx_msg_id_get();
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);
    return msg_id;
}

/*
    node + qm_ble_async_data_t + buffer
*/

static uint8_t tmp_buf[QB_MAX_PAYLOAD_SIZE] = {0};

uint32_t qm_ble_generic_post(uint8_t is_ack, uint8_t tx_type, uint8_t *msg_id, uint8_t cmd, uint8_t *err_code, uint8_t *buffer, uint32_t length)
{
    uint8_t *buf = NULL;
    uint16_t buf_len = 0;
    qm_list_node_t *node = NULL;
    qm_ble_async_data_t *async_data = NULL;
    uint32_t list_len = 0;
    uint32_t err = 0;
    uint8_t id = 0;
    uint32_t tmp_len = 0;

    if (length > QB_MAX_PAYLOAD_SIZE) {
        return QB_EDATASIZE;
    }
   
    if(tx_type != QB_TX_INDICATION && tx_type != QB_TX_NOTIFICATION){
        return QB_EINVALIDPARAM;
    }

    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    list_len = g_qm_ble_async.list->len;
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);
    
    if(is_ack){
        id = *msg_id;
    }else{
        id = async_tx_msg_id_get();
        *msg_id = id;
    }
       
    if(!g_qm_ble_async.is_busy && list_len == 0){

        if(err_code){
            tmp_buf[0] = *err_code;
            if(buffer && length > 0){
                memcpy(tmp_buf+1, buffer, length);
            }
            tmp_len = length+1;
        }else{
            if(buffer && length > 0){
                memcpy(tmp_buf, buffer, length);
            }
            tmp_len = length;
        }

        buf = &tmp_buf[0];
           
        if(tx_type == QB_TX_INDICATION){
            err = qm_ble_transport_tx(QB_TX_INDICATION, &id, cmd, buf, tmp_len);
            if(err == QB_SUCCESS){
                qm_ble_async_busy();
            }
        }else{
            err = qm_ble_transport_tx(QB_TX_NOTIFICATION, &id, cmd, buf, tmp_len);
        }
        return err;
    }
   
    if(list_len >= MAX_ASYNC_LIST_NUM){
        QM_BLE_LOGI(QM_BLE_LOG_UPLINK, "ble post list full");
        return QB_EBUSY;
    }
    
    buf_len = sizeof(qm_ble_async_data_t) + sizeof(qm_list_node_t) + length +1;
    buf = qm_ble_malloc(buf_len);
    if(buf == NULL){
        return QB_EMEM;
    }
    
    node = (qm_list_node_t*)buf;

    qm_list_node_init(node, buf+sizeof(qm_list_node_t));
    async_data = (qm_ble_async_data_t*)(buf+sizeof(qm_list_node_t));
    async_data->cmd = cmd;
    async_data->msg_id = id;
    async_data->tx_type = tx_type;
    async_data->buffer = buf + sizeof(qm_ble_async_data_t) + sizeof(qm_list_node_t);
    
    if(err_code){
        async_data->len = length+1;
        memcpy(async_data->buffer, err_code, 1);
        if(buffer && length>0){
            memcpy(async_data->buffer+1, buffer, length);
        }
    }else{
        if(buffer && length>0){
            async_data->len = length;
            memcpy(async_data->buffer, buffer, length);  
        }            
    }
    
    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    qm_list_rpush(g_qm_ble_async.list, node);
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);

    if(!g_qm_ble_async.is_busy){
        qm_ble_async_post_internal();
    }

    *msg_id = id;

    return QB_SUCCESS;


}

uint32_t qm_ble_post_async(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length)
{   
    return qm_ble_generic_post(0, QB_TX_INDICATION, msg_id, cmd, NULL, buffer, length);
}


uint32_t qm_ble_post_async_fast(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length)
{  
    return qm_ble_generic_post(0, QB_TX_NOTIFICATION, msg_id, cmd, NULL, buffer, length);
}


uint32_t qm_ble_ack_async(uint8_t msg_id, uint8_t cmd, uint8_t err_code, uint8_t *buffer, uint32_t length)
{    
    return qm_ble_generic_post(1, QB_TX_INDICATION, &msg_id, cmd, &err_code, buffer, length);
}

uint32_t qm_ble_ack_async_fast(uint8_t msg_id, uint8_t cmd, uint8_t err_code, uint8_t *buffer, uint32_t length)
{
    return qm_ble_generic_post(1, QB_TX_NOTIFICATION, &msg_id, cmd, &err_code, buffer, length);
}


uint32_t qm_ble_async_post_internal(void)
{
    qm_list_node_t *node = NULL;
    qm_ble_async_data_t *async_data = NULL;
    uint32_t list_len = 0;
    static uint8_t *buf = NULL;
    uint32_t err = 0;

    if(buf){
        qm_ble_free(buf);
        buf = NULL;
    }

    qm_ble_mutex_lock(&g_qm_ble_async.mutex, QM_WAIT_FOREVER);
    list_len = g_qm_ble_async.list->len;
    if(list_len > 0){
        node = qm_list_lpop(g_qm_ble_async.list);
    }
    qm_ble_mutex_unlock(&g_qm_ble_async.mutex);
    
    if(node){     
        buf = (uint8_t*)node;
        async_data = (qm_ble_async_data_t*)(buf + sizeof(qm_list_node_t));
        if(async_data->tx_type == QB_TX_INDICATION){
            err = qm_ble_transport_tx(QB_TX_INDICATION, &async_data->msg_id, async_data->cmd, async_data->buffer, async_data->len);
            if(err == QB_SUCCESS){
                qm_ble_async_busy();
            }
        }else{
            err = qm_ble_transport_tx(QB_TX_NOTIFICATION, &async_data->msg_id, async_data->cmd, async_data->buffer, async_data->len);
        }
    }

    return err;
}


uint32_t qm_ble_async_clear(void)
{
    if(g_qm_ble_async.list){
        qm_list_destroy(g_qm_ble_async.list);
        g_qm_ble_async.list = NULL;
        g_qm_ble_async.is_busy = 0;

        g_qm_ble_async.list = qm_list_new();
        if(g_qm_ble_async.list == NULL){
            QM_BLE_LOGI(QM_BLE_LOG_UPLINK, "list node allocation failed");
            return QB_EMEM;
        }
    }
    return QB_SUCCESS;
}



#endif


