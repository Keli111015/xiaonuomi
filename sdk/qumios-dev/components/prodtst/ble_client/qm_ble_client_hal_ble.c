#include "qm_ble_client_hal_ble.h"
#include "qm_errno.h"

#include "qm.h"
#include "qm_log.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define LOG_TAG "qm_ble_client_hal"

#define PROFILE_NUM         (5)
#define CONNECT_TIMEOUT_S   (10)
#define RECV_DATA_MAX_LEN   (30)


typedef struct 
{
    uint8_t bluedroid_init          : 1;
    uint8_t bluedroid_enable        : 1;
    uint8_t bt_controller_init      : 1;
    uint8_t bt_controller_enable    : 1;
}qm_ble_client_hal_init_status_t;

typedef struct{
    bool is_use;
    bool connect;
    bool get_server;
    bool auth_complete;

    qm_sem_t sem;
    uint16_t app_id;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;

    uint16_t service_start_handle;
    uint16_t service_end_handle;

    uint16_t write_char_handle;
    uint16_t notify_char_handle;
}qm_ble_client_hal_handle_t;

typedef struct{
    uint8_t     on_scan;
    uint16_t    gattc_if;
    qm_mutex_t  conn_mutex;
    qm_queue_t  recv_queue;
    qm_task_t   recv_task;
    qm_ble_client_service_init_t *init_param;
    qm_ble_client_hal_handle_t connect_handle[PROFILE_NUM];
}qm_ble_client_hal_ctx_t;

typedef struct{
    void *handle;
    uint8_t *data;
    uint16_t length;
}qm_ble_client_hal_data_t;

static qm_ble_client_hal_ctx_t g_client_hal_ctx = {0};

static void qm_ble_client_recv_thread(void *arg);

static qm_ble_client_hal_ctx_t *qm_ble_client_hal_ctx_get(void);
static qm_ble_client_hal_handle_t *connect_handle_free_get(void);
static qm_ble_client_hal_handle_t *connect_handle_get_from_id(uint16_t id);
static qm_ble_client_hal_handle_t *connect_handle_get_from_mac(uint8_t mac[6]);

static void gatt_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);


int qm_ble_client_stack_init(qm_ble_client_service_init_t *ble_client_init)
{
    qm_err_t ret = QM_EOK;

    if(ble_client_init == NULL){
        return -QM_EINVAL;
    }

    ret = qm_queue_new(&g_client_hal_ctx.recv_queue, RECV_DATA_MAX_LEN, sizeof(qm_ble_client_hal_data_t));
    if(ret != QM_EOK){
        goto _error;
    }

    ret = qm_mutex_new(&g_client_hal_ctx.conn_mutex);
    if(ret != QM_EOK){
        goto _error;
    }

    ret = esp_ble_gattc_register_callback(gatt_event_handler);
    if(ret){
        QM_LOGE(LOG_TAG, "%s gattc register failed, error code = %x\n", __func__, ret);
        goto _error;
    }

    g_client_hal_ctx.init_param = ble_client_init;
    
    esp_ble_gattc_app_register(0);
    qm_task_new(&g_client_hal_ctx.recv_task, "BLE_CLIENT", qm_ble_client_recv_thread, NULL, 3072, 20);
    
    return QM_EOK;

_error:
    if(qm_mutex_is_valid(&g_client_hal_ctx.conn_mutex)){
        qm_mutex_free(&g_client_hal_ctx.conn_mutex);
    }

    if(qm_queue_is_valid(&g_client_hal_ctx.recv_queue)){
        qm_queue_free(&g_client_hal_ctx.recv_queue);
    }

    return ret;
}

int qm_ble_client_stack_deinit(void)
{   
    g_client_hal_ctx.init_param = NULL;

    if(qm_queue_is_valid(&g_client_hal_ctx.recv_queue)){
        qm_queue_free(&g_client_hal_ctx.recv_queue);
    }

    if(qm_mutex_is_valid(&g_client_hal_ctx.conn_mutex)){
        qm_mutex_free(&g_client_hal_ctx.conn_mutex);
    }

    return QM_EOK;
}

int qm_ble_client_send_notification(void *conn_handle, uint8_t *p_data, uint16_t length)
{
    qm_err_t ret = QM_EOK;
    qm_ble_client_hal_handle_t *connect_handle = (qm_ble_client_hal_handle_t *)conn_handle;

    if(conn_handle == NULL || p_data == NULL || length == 0){
        return -QM_EINVAL;
    }

    ret = esp_ble_gattc_write_char( g_client_hal_ctx.gattc_if,
                                    connect_handle->conn_id,
                                    connect_handle->write_char_handle,
                                    length,
                                    p_data,
                                    ESP_GATT_WRITE_TYPE_NO_RSP,
                                    ESP_GATT_AUTH_REQ_NONE);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "SEND NOTIFY ERROR %d", ret);
        ret = -QM_ERROR;
    }
    return  ret;
}

void *qm_ble_client_connect(uint8_t mac[6])
{
    qm_err_t ret = QM_EOK;
    qm_ble_client_hal_handle_t *connect_handle = NULL;

    connect_handle = connect_handle_get_from_mac(mac);
    if(connect_handle){
        QM_LOGD(LOG_TAG, "connect_handle already mac");    
        return NULL;
    }

    connect_handle = connect_handle_free_get();
    if(connect_handle == NULL){
        QM_LOGD(LOG_TAG, "connect_handle_free_get failed");    

        return NULL;
    }

    ret = qm_sem_new(&connect_handle->sem, 0);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "qm_ble_client_connect qm_sem_new failed");    
        return NULL;
    }

    memcpy(connect_handle->remote_bda, mac, 6);
    esp_ble_gattc_open(g_client_hal_ctx.gattc_if, connect_handle->remote_bda, 0, true);                

    ret = qm_sem_wait(&connect_handle->sem, CONNECT_TIMEOUT_S * 1000);

    if(qm_sem_is_valid(&connect_handle->sem)){
        qm_sem_free(&connect_handle->sem);
    }

    if(ret != QM_EOK){
        if(g_client_hal_ctx.init_param->on_connect_failed){
            g_client_hal_ctx.init_param->on_connect_failed(connect_handle);
        }

        if(connect_handle->connect){
            esp_ble_gattc_close(g_client_hal_ctx.gattc_if, connect_handle->conn_id);
            esp_ble_gap_disconnect(mac);
        }else{
            memset(connect_handle, 0, sizeof(qm_ble_client_hal_handle_t));
        }

        return NULL;
    }   

    return (void *)connect_handle;

}

void qm_ble_client_disconnect(void *conn_handle)
{
    qm_ble_client_hal_handle_t *connect_handle = (qm_ble_client_hal_handle_t *)conn_handle;
    if(conn_handle == NULL){
        return;
    }

    if(connect_handle->connect){
        esp_ble_gattc_close(g_client_hal_ctx.gattc_if, connect_handle->conn_id);
    }
}

static qm_ble_client_hal_ctx_t *qm_ble_client_hal_ctx_get(void)
{
    return &g_client_hal_ctx;
}

static qm_ble_client_hal_handle_t *connect_handle_free_get(void)
{
    int i = 0;
    qm_mutex_lock(&g_client_hal_ctx.conn_mutex, QM_WAIT_FOREVER);

    for (i = 0; i < PROFILE_NUM; i++)
    {
        if(g_client_hal_ctx.connect_handle[i].is_use == QM_FALSE){
            g_client_hal_ctx.connect_handle[i].is_use = QM_TRUE;
            g_client_hal_ctx.connect_handle[i].app_id = i;
            qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);

            return &g_client_hal_ctx.connect_handle[i];
        }
    }
    qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);
    return NULL;
}


static qm_ble_client_hal_handle_t *connect_handle_get_from_id(uint16_t id)
{
    int i = 0;
    qm_mutex_lock(&g_client_hal_ctx.conn_mutex, QM_WAIT_FOREVER);

    for (i = 0; i < PROFILE_NUM; i++)
    {
        if(g_client_hal_ctx.connect_handle[i].is_use == QM_TRUE){
            if(g_client_hal_ctx.connect_handle[i].conn_id == id){
                qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);   

                return &g_client_hal_ctx.connect_handle[i];
            }
        }
    }
    qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);   

    return NULL;
}

static qm_ble_client_hal_handle_t *connect_handle_get_from_mac(uint8_t mac[6])
{
    int i = 0;
    qm_mutex_lock(&g_client_hal_ctx.conn_mutex, QM_WAIT_FOREVER);

    for (i = 0; i < PROFILE_NUM; i++)
    {
        if(g_client_hal_ctx.connect_handle[i].is_use == QM_TRUE){
            if(!memcmp(mac, g_client_hal_ctx.connect_handle[i].remote_bda, 6)){
                qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);   

                return &g_client_hal_ctx.connect_handle[i];
            }
        }
    }
    qm_mutex_unlock(&g_client_hal_ctx.conn_mutex);   

    return NULL;
}

static void qm_ble_client_recv_thread(void *arg)
{
    uint32_t size;
    qm_ble_client_hal_data_t recv_msg = {0};
    qm_ble_client_hal_ctx_t *client_hal_ctx = NULL;

    client_hal_ctx = qm_ble_client_hal_ctx_get();
    if(client_hal_ctx == NULL){
        return;
    }
    
    while (1)
    {
        memset(&recv_msg, 0, sizeof(qm_ble_client_hal_data_t));
        qm_queue_recv(&client_hal_ctx->recv_queue, &recv_msg, &size, QM_WAIT_FOREVER);

        if(client_hal_ctx->init_param->nc.ble_write){
            client_hal_ctx->init_param->nc.ble_write(recv_msg.handle, recv_msg.data, recv_msg.length);
        }      

        if(recv_msg.data){
            qm_free(recv_msg.data);
            recv_msg.data = NULL;
        }
    }
    
}

static void gatt_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    uint16_t count = 0;
    uint16_t notify_en = 2;
    esp_bt_uuid_t service_uuid = {0};
    esp_gatt_status_t status = ESP_GATT_OK;
    qm_ble_client_hal_ctx_t *client_hal_ctx = NULL;
    qm_ble_client_hal_handle_t *connect_handle = NULL;
    esp_gattc_char_elem_t *char_elem_result   = NULL;
    esp_gattc_descr_elem_t *descr_elem_result = NULL;
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    client_hal_ctx = qm_ble_client_hal_ctx_get();
    if(client_hal_ctx == NULL){
        return;
    }

    switch (event) {

        case ESP_GATTC_UNREG_EVT:

            QM_LOGD(LOG_TAG, "ESP_GATTC_UNREG_EVT ID [%d]", gattc_if);
        break;
        
        case ESP_GATTC_REG_EVT:
            QM_LOGD(LOG_TAG, "REG_EVT APP_ID %d", param->reg.app_id);       
            if (param->reg.status == ESP_GATT_OK) {
                client_hal_ctx->gattc_if = gattc_if;
            } else {
                QM_LOGE(LOG_TAG, "reg app failed, app_id %04x, status %d",
                        param->reg.app_id,
                        param->reg.status);
                return;
            }
        break;

        case ESP_GATTC_CONNECT_EVT:{

            connect_handle = connect_handle_get_from_mac(p_data->connect.remote_bda);
            if(connect_handle == NULL){
                break;     
            }   

            connect_handle->connect = QM_TRUE;
            connect_handle->conn_id = p_data->open.conn_id;

            esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
            if (mtu_ret){
                QM_LOGE(LOG_TAG,  "config MTU error, error code = %x", mtu_ret);
            }    
        }break;

        case ESP_GATTC_CLOSE_EVT:

            if(param->close.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG, "close failed, status %d", p_data->close.status);
                break;
            }

            connect_handle = connect_handle_get_from_id(p_data->close.conn_id);
            if(connect_handle == NULL){  
                break;
            }

            QM_LOGD(LOG_TAG, "ESP_GATTC_CLOSE_EVT success ID %d",connect_handle->conn_id);
            if(client_hal_ctx->init_param->on_disconnected ){
                client_hal_ctx->init_param->on_disconnected(connect_handle);
            }            
            memset(connect_handle, 0, sizeof(qm_ble_client_hal_handle_t));
            esp_ble_gap_disconnect(p_data->close.remote_bda);
        break;

        case ESP_GATTC_OPEN_EVT:

            if (param->open.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG, "open failed, status %d", p_data->open.status);
                break;
            }

            connect_handle = connect_handle_get_from_mac(p_data->open.remote_bda);
            if(connect_handle == NULL){
                break;    
            }

            connect_handle->connect = QM_TRUE;
            connect_handle->conn_id = p_data->open.conn_id;
            QM_LOGD(LOG_TAG, "open success ID %d",connect_handle->conn_id);
        break;

        case ESP_GATTC_DIS_SRVC_CMPL_EVT:
            connect_handle = connect_handle_get_from_id(p_data->dis_srvc_cmpl.conn_id);
            if(connect_handle == NULL){
                break;      
            }

            if (param->dis_srvc_cmpl.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
                break;
            }
            service_uuid.len = ESP_UUID_LEN_16;
            service_uuid.uuid.uuid16 = client_hal_ctx->init_param->uuid_svc;
            esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, &service_uuid);        
        break;

        case ESP_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
            }
        break;
        
        case ESP_GATTC_SEARCH_RES_EVT: {
            connect_handle = connect_handle_get_from_id(p_data->search_res.conn_id);
            if(connect_handle == NULL){
                break;    
            }
            
            if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == client_hal_ctx->init_param->uuid_svc) {
                connect_handle->get_server = QM_TRUE;
                connect_handle->service_start_handle = p_data->search_res.start_handle;
                connect_handle->service_end_handle = p_data->search_res.end_handle;
            }
        }break;

        case ESP_GATTC_SEARCH_CMPL_EVT:

            connect_handle = connect_handle_get_from_id(p_data->search_cmpl.conn_id);
            if(connect_handle == NULL){
                break;     
            }        

            if (p_data->search_cmpl.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG, "ID[%d] search service failed, error status = %x", connect_handle->conn_id, p_data->search_cmpl.status);
                break;
            }


            if(connect_handle->get_server == QM_FALSE){
                QM_LOGE(LOG_TAG, "ID[%d] ESP_GATTC_SEARCH_CMPL_EVT get_server NULL", connect_handle->conn_id);
                break;   
            }
   
            status = esp_ble_gattc_get_attr_count( gattc_if,
                                                    p_data->search_cmpl.conn_id,
                                                    ESP_GATT_DB_CHARACTERISTIC,
                                                    connect_handle->service_start_handle,
                                                    connect_handle->service_end_handle,
                                                    0,
                                                    &count);
            if (status != ESP_GATT_OK || count <= 0){
                QM_LOGE(LOG_TAG, "esp_ble_gattc_get_attr_count error");
                break;
            }

            char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
            if (char_elem_result == NULL){
                QM_LOGE(LOG_TAG, "gattc no mem");
                break;
            }

            service_uuid.len = ESP_UUID_LEN_16;
            service_uuid.uuid.uuid16 = client_hal_ctx->init_param->nc.uuid;
            memset(char_elem_result, 0, sizeof(esp_gattc_char_elem_t) * count);

            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                    p_data->search_cmpl.conn_id,
                                                    connect_handle->service_start_handle,
                                                    connect_handle->service_end_handle,
                                                    service_uuid,
                                                    char_elem_result,
                                                    &count);
            if (status != ESP_GATT_OK || count <= 0){
                QM_LOGE(LOG_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result);
                break;
            }

            if ((char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)){
                connect_handle->notify_char_handle = char_elem_result[0].char_handle;
                esp_ble_gattc_register_for_notify(gattc_if, connect_handle->remote_bda, connect_handle->notify_char_handle);
            }

            service_uuid.len = ESP_UUID_LEN_16;
            service_uuid.uuid.uuid16 = client_hal_ctx->init_param->wwnrc.uuid;
            memset(char_elem_result, 0, sizeof(esp_gattc_char_elem_t) * count);

            status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                                                    p_data->search_cmpl.conn_id,
                                                    connect_handle->service_start_handle,
                                                    connect_handle->service_end_handle,
                                                    service_uuid,
                                                    char_elem_result,
                                                    &count);
            if (status != ESP_GATT_OK || count <= 0){
                QM_LOGE(LOG_TAG, "esp_ble_gattc_get_char_by_uuid error");
                free(char_elem_result);
                break;
            }

            if ((char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR)){
                connect_handle->write_char_handle = char_elem_result[0].char_handle;
            }

            /* free char_elem_result */
            free(char_elem_result);

            notify_en = 1;
            status = esp_ble_gattc_get_attr_count( gattc_if,
                                                    connect_handle->conn_id,
                                                    ESP_GATT_DB_DESCRIPTOR,
                                                    connect_handle->service_start_handle,
                                                    connect_handle->service_end_handle,
                                                    connect_handle->notify_char_handle,
                                                    &count);  
            if (status != ESP_GATT_OK || count <= 0){
                QM_LOGE(LOG_TAG, "esp_ble_gattc_get_attr_count error");
                break; 
            }        

            descr_elem_result = (esp_gattc_descr_elem_t *)malloc(sizeof(esp_gattc_descr_elem_t) * count);
            if (descr_elem_result == NULL){
                QM_LOGE(LOG_TAG, "gattc no mem");
                break; 
            }

            service_uuid.len = ESP_UUID_LEN_16;
            service_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            memset(descr_elem_result, 0, sizeof(esp_gattc_descr_elem_t) * count);

            status = esp_ble_gattc_get_descr_by_char_handle(gattc_if,
                                                            connect_handle->conn_id,
                                                            connect_handle->notify_char_handle,
                                                            service_uuid,
                                                            descr_elem_result,
                                                            &count);
            if (status != ESP_GATT_OK || count <= 0 ){
                QM_LOGE(LOG_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                qm_free(descr_elem_result);
                break; 
            }

            if (descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG){
                status = esp_ble_gattc_write_char_descr(gattc_if,
                                                        connect_handle->conn_id,
                                                        descr_elem_result[0].handle,
                                                        sizeof(notify_en),
                                                        (uint8_t *)&notify_en,
                                                        ESP_GATT_WRITE_TYPE_RSP,
                                                        ESP_GATT_AUTH_REQ_NONE);
                if (status != ESP_GATT_OK){
                    QM_LOGE(LOG_TAG, "esp_ble_gattc_write_char_descr error");
                }
                
            }
            free(descr_elem_result);
        break;
        
        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
            if (p_data->reg_for_notify.status != ESP_GATT_OK){
                break;
            }
        }break;
        
        case ESP_GATTC_NOTIFY_EVT:

            connect_handle = connect_handle_get_from_id(p_data->notify.conn_id);
            if(connect_handle == NULL){
                break;    
            } 

            if (p_data->notify.is_notify){
                qm_ble_client_hal_data_t recv_data = {
                    .data = NULL,
                    .handle = connect_handle,
                    .length = p_data->notify.value_len,
                };

                recv_data.data = (uint8_t *)qm_malloc(p_data->notify.value_len);
                if(recv_data.data == NULL){
                    QM_LOGE(LOG_TAG, "ESP_GATTC_NOTIFY_EVT  malloc error");
                    break;
                }
                memset(recv_data.data, 0, p_data->notify.value_len);
                memcpy(recv_data.data, p_data->notify.value, p_data->notify.value_len);
                qm_queue_send(&client_hal_ctx->recv_queue, &recv_data, sizeof(qm_ble_client_hal_data_t));
            }
        break;

        case ESP_GATTC_WRITE_DESCR_EVT:
            connect_handle = connect_handle_get_from_id(p_data->write.conn_id);
            if(connect_handle == NULL){
                break;    
            } 
            
            if (p_data->write.status != ESP_GATT_OK){
                QM_LOGE(LOG_TAG, "ID[%d] write descr failed, error status = %x", connect_handle->conn_id, p_data->write.status);
                break;
            }
                if(qm_sem_is_valid(&connect_handle->sem)){
                    qm_sem_signal(&connect_handle->sem);
                    if(client_hal_ctx->init_param->on_connected){
                        client_hal_ctx->init_param->on_connected(connect_handle);
                    }
                }
        break;
        
        case ESP_GATTC_SRVC_CHG_EVT: {
            esp_bd_addr_t bda;
            connect_handle = connect_handle_get_from_mac(p_data->srvc_chg.remote_bda);
            if(connect_handle == NULL){ 
                break;    
            } 
            memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
            QM_LOGD(LOG_TAG,"ID[%d]  ESP_GATTC_SRVC_CHG_EVT", connect_handle->conn_id);
            QM_HEX_LOGD(LOG_TAG, "bd_addr:", bda, sizeof(esp_bd_addr_t));
        }break;
        
        case ESP_GATTC_WRITE_CHAR_EVT:
            if (p_data->write.status != ESP_GATT_OK){
                QM_LOGD(LOG_TAG,"ESP_GATTC_WRITE_CHAR_EVT ERROR %d", p_data->write.status);
                break;
            }
        break;

        case ESP_GATTC_DISCONNECT_EVT:
            connect_handle = connect_handle_get_from_id(p_data->disconnect.conn_id);
            if(connect_handle == NULL){
                break;    
            } 

            connect_handle->get_server = QM_FALSE;

           
            QM_LOGD(LOG_TAG, "ID[%d] ESP_GATTC_DISCONNECT_EVT, reason = %d", connect_handle->conn_id, p_data->disconnect.reason);
        break;

        default:
            break;
    }
}

