#include "prodtst_core.h"
#include "qm_errno.h"
#include "prodtst_adapter.h"
#include "qm_log.h"
#include "prodtst_cmd.h"
#include "prodtst_special.h"
#include "prodtst_cloud.h"
#include "cJSON.h"

/** Production-test box event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_PRODTST_BOX);

#define LOG_TAG "prodtst core"


/**********prodtst object declaration**********/
static void prodtst_obj_task(void *arg);
static int prodtst_obj_state_set(prodtst_obj_t *prodtst_obj, int state);
static int prodtst_obj_state_get(prodtst_obj_t *prodtst_obj, int *state);
static int prodtst_obj_destroy(prodtst_obj_t *prodtst_obj);
static prodtst_obj_t *prodtst_obj_creat(void);
static prodtst_obj_t *prodtst_obj_idle_get();
/***************************************/

static int prodtst_data_table_reset(prodtst_data_table_t *prodtst_tbl);
static int prodtst_data_table_set(prodtst_data_table_t *prodtst_tbl, prodtst_data_t *prodtst_data, int num);
static int prodtst_all_obj_destroy(void);
static int prodtst_all_obj_reset(void);
static int prodtst_generic_notify(prodtst_obj_t *prodtst_obj, int event);

static prodtst_context_t g_prodtst_ctx = {0};

extern prodtst_adapter_t wifi_prodtst_adapter;

static prodtst_testsuite_t  g_prodtst_testsuite[] = {
    {"harmony", "W70", "wifi", 4096, 1, &prodtst_wifi_generic_cmd, 5, &prodtst_hw_wifi_specific_cmd, 2, &wifi_prodtst_adapter},

 
};

#define PRODTST_TESTSUITE_NUM (sizeof(g_prodtst_testsuite)/sizeof(g_prodtst_testsuite[0]))

typedef struct{
    int type;
    char *name;
}prodtst_step_t;

static prodtst_step_t g_prodtst_step[] = {
    {PRODTST_STEP_TYPE_GENERIC, "generic"},
    {PRODTST_STEP_TYPE_FUN, "fun"},
    {PRODTST_STEP_TYPE_SPECIAL, "special"},
    {PRODTST_STEP_TYPE_ALL, "all"},
};

#define PRODTST_STEP_NUM  (sizeof(g_prodtst_step)/sizeof(g_prodtst_step[0]))


static prodtst_testsuite_t *prodtst_testsuite_alloc(char *platform, char *module_name, char *module_type)
{
    int i = 0;
    if(platform == NULL || module_name == NULL || module_type == NULL){
        return NULL;
    }

    for(i = 0; i < PRODTST_TESTSUITE_NUM; i++){
        if( (strlen(platform) == strlen(g_prodtst_testsuite[i].platform)) && (strcmp(platform, g_prodtst_testsuite[i].platform) == 0) && 
            (strlen(module_name) == strlen(g_prodtst_testsuite[i].module_name)) && (strcmp(module_name, g_prodtst_testsuite[i].module_name) == 0) &&
            (strlen(module_type) == strlen(g_prodtst_testsuite[i].module_type)) && (strcmp(module_type, g_prodtst_testsuite[i].module_type) == 0)){

                return &g_prodtst_testsuite[i];
        }
    }
    return NULL;
}

int prodtst_device_num_get(void)
{
    return g_prodtst_ctx.testsuite->obj_num;
}


static prodtst_adapter_t *prodtst_adapter_get(void)
{
    return g_prodtst_ctx.testsuite->adapter;
}

static prodtst_testsuite_t *prodtst_testsuite_get(void)
{
    return g_prodtst_ctx.testsuite;
}

int prodtst_start(prodtst_info_t *info)
{
    qm_err_t ret = QM_EOK;
    prodtst_testsuite_t *testsuite = NULL;
    if(info == NULL){
        return -QM_EINVAL;
    }

    testsuite = prodtst_testsuite_alloc(info->platform, info->module_name, info->module_type);
    if(testsuite == NULL){
        QM_LOGE(LOG_TAG, "prodtst param is invalid");
        return -QM_EINVAL;
    }

    if(g_prodtst_ctx.start){
        prodtst_stop();
        g_prodtst_ctx.start = 0;
    }

    g_prodtst_ctx.start = 1;
    g_prodtst_ctx.testsuite = testsuite;

    g_prodtst_ctx.adapter_handle = g_prodtst_ctx.testsuite->adapter->init(NULL);
    if(g_prodtst_ctx.adapter_handle == NULL){
        return -QM_ERROR;
    }

    return QM_EOK;
}


int prodtst_stop(void)
{
    qm_err_t ret = QM_EOK;
    if(g_prodtst_ctx.testsuite == NULL){
        return ret;
    }

    prodtst_all_obj_destroy();

    ret = g_prodtst_ctx.testsuite->adapter->deinit(g_prodtst_ctx.adapter_handle);
    if(ret != QM_EOK){
        return ret;
    }

    g_prodtst_ctx.start = 0;
    g_prodtst_ctx.testsuite = NULL;

    QM_LOGI(LOG_TAG, "prodtst stop success");

    return ret;
}


static int prodtst_data_table_reset(prodtst_data_table_t *prodtst_tbl)
{
    int i = 0, j = 0;
    prodtst_data_t *prodtst_data = NULL;
    prodtst_data_t *m_prodtst_data = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_DATA_TABLE_NUM; i++){
        m_prodtst_data = prodtst_tbl->data[i];
        if(m_prodtst_data == NULL){
            continue;
        }
                
        for(j = 0; j < prodtst_tbl->num[i]; j++){
            prodtst_data = (m_prodtst_data+j);  
            if(prodtst_data->name){
                qm_free(prodtst_data->name);
            }
            if(prodtst_data->type == PRODTST_DATA_STRING){
                if(prodtst_data->data){
                    qm_free(prodtst_data->data);
                }
            }else if(prodtst_data->type == PRODTST_DATA_JSON){
                if(prodtst_data->data){
                    cJSON_Delete((cJSON*)prodtst_data->data);
                }
            }        
        }
        qm_free(m_prodtst_data);
    }

    memset(prodtst_tbl, 0, sizeof(prodtst_data_table_t));
    return QM_EOK;

}

static int prodtst_data_table_set(prodtst_data_table_t *prodtst_tbl, prodtst_data_t *prodtst_data, int num)
{
    int i = 0;
    for(i = 0; i < CONFIG_QM_PRODTST_DATA_TABLE_NUM; i++){
        if(prodtst_tbl->data[i] != NULL){
            continue;
        }
        prodtst_tbl->data[i] = prodtst_data;
        prodtst_tbl->num[i] = num;
        return QM_EOK;
    }

    return -QM_EFULL;
}

static int prodtst_data_table_int_set(prodtst_data_table_t *prodtst_tbl, char *name, int value)
{
    qm_err_t ret = QM_EOK;
    prodtst_data_t *prodtst_data = NULL;
    char *m_name = NULL;

    prodtst_data = (prodtst_data_t*)qm_malloc(sizeof(prodtst_data_t));
    if(prodtst_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    m_name = (char*)qm_malloc(strlen(name)+1);
    if(m_name == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(m_name, 0, strlen(name)+1);
    memcpy(m_name, name, strlen(name));

    prodtst_data->type = PRODTST_DATA_INT;
    prodtst_data->int_data = value;
    prodtst_data->name = m_name;

    ret = prodtst_data_table_set(prodtst_tbl, prodtst_data, 1);
    if(ret != QM_EOK){
        goto __exit;
    }

    return QM_EOK;

__exit:
    if(prodtst_data){
        qm_free(prodtst_data);
        prodtst_data = NULL;
    }

    if(m_name){
        qm_free(m_name);
        m_name = NULL;
    }

    return ret;
}

static int prodtst_data_table_string_set(prodtst_data_table_t *prodtst_tbl, char *name, char *value)
{
    qm_err_t ret = QM_EOK;
    prodtst_data_t *prodtst_data = NULL;
    char *m_name = NULL;
    char *m_value = NULL;

    prodtst_data = (prodtst_data_t*)qm_malloc(sizeof(prodtst_data_t));
    if(prodtst_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    m_name = (char*)qm_malloc(strlen(name)+1);
    if(m_name == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(m_name, 0, strlen(name)+1);
    memcpy(m_name, name, strlen(name));

    m_value = (char*)qm_malloc(strlen(value)+1);
    if(m_value == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(m_value, 0, strlen(value)+1);
    memcpy(m_value, value, strlen(value));

    prodtst_data->type = PRODTST_DATA_STRING;
    prodtst_data->name = m_name;
    prodtst_data->data = m_value;

    ret = prodtst_data_table_set(prodtst_tbl, prodtst_data, 1);
    if(ret != QM_EOK){
        goto __exit;
    }

    return QM_EOK;

__exit:
    if(prodtst_data){
        qm_free(prodtst_data);
        prodtst_data = NULL;
    }

    if(m_name){
        qm_free(m_name);
        m_name = NULL;
    }

    if(m_value){
        qm_free(m_value);
        m_value = NULL;
    }

    return ret;
}

static int prodtst_data_table_print(prodtst_data_table_t *prodtst_tbl)
{

    int i = 0, j = 0;
    prodtst_data_t *prodtst_data = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_DATA_TABLE_NUM; i++){
        prodtst_data = prodtst_tbl->data[i];
        if(prodtst_data == NULL){
            continue;
        }  
        for(j = 0; j < prodtst_tbl->num[i]; j++){
            prodtst_data = (prodtst_data+j);  
            if(prodtst_data->name){
                QM_LOGI(LOG_TAG, "cmd name:      %s", prodtst_data->name);
            }
            if(prodtst_data->type == PRODTST_DATA_STRING && prodtst_data->data){
                QM_LOGI(LOG_TAG, "response data: %s", prodtst_data->data);
            }
            if(prodtst_data->type == PRODTST_DATA_INT){
                QM_LOGI(LOG_TAG, "response data: %d", prodtst_data->int_data);
            }
        }
    }
    return QM_EOK;
}

static void prodtst_status_notify(prodtst_obj_t *prodtst_obj, int prodtst_status)
{
    int channel = 0;
    if(prodtst_obj == NULL){
        channel = 0;
    }else{
        channel = prodtst_obj->index + 1;
    }

    if(prodtst_obj ){
        prodtst_obj->status = prodtst_status;
    }
        
    if(g_prodtst_ctx.notify_cb){
        g_prodtst_ctx.notify_cb(channel, prodtst_status);    
    }
}

static int prodtst_generic_handler(prodtst_obj_t *prodtst_obj)
{
    int num = 0;
    int parse_num = 0;
    int index = 0;
    char *m_cmd = NULL;
    char *cmd = NULL;
    qm_err_t ret = QM_EOK;
    qm_at_response_t *resp = NULL;
    prodtst_testsuite_t *testsuite = NULL;
    prodtst_data_t *prodtst_data = NULL;

    testsuite = prodtst_testsuite_get();

    for(num = 0; num < testsuite->generic_cmd_num; num++){

        resp = qm_at_client_create_resp(testsuite->generic_cmd[num].recv_size + 64, 0, testsuite->generic_cmd[num].timeout);
        if(resp == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }
        if(testsuite->generic_cmd[num].creat){
            testsuite->generic_cmd[num].creat(testsuite->generic_cmd[num].cmd, NULL, &m_cmd);
            if(m_cmd == NULL){
                ret = -QM_ERROR;
                goto __exit;
            }
            cmd = m_cmd;     
        }else{
            cmd = testsuite->generic_cmd[num].cmd;
        }
        ret = qm_at_client_obj_exec_cmd(prodtst_obj->at_client, resp, cmd);
        if(ret == -QM_ETIMEOUT){
            QM_LOGE(LOG_TAG, "wait generic response timeout");
            goto __exit;
        }else if(ret == -QM_ERROR){
            QM_LOGE(LOG_TAG, "generic response failed");
            goto __exit;
        }

        if(m_cmd){
            qm_free(m_cmd);
            m_cmd = NULL;
        }

        ret = testsuite->generic_cmd[num].parser(resp, &prodtst_data, &parse_num);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "parse generic response failed");
            goto __exit; 
        }
        if(prodtst_data && parse_num > 0){
            ret = prodtst_data_table_set(&prodtst_obj->data_tbl, prodtst_data, parse_num);
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "prodtst table full");
                goto __exit; 
            }
        }
        qm_at_client_delete_resp(resp);
        resp = NULL;
        
    }

    return ret;

__exit:

    if(resp){
        qm_at_client_delete_resp(resp);
        resp = NULL;
    }

    prodtst_data_table_reset(&prodtst_obj->data_tbl);
    return ret;
}

static int prodtst_specific_handler(prodtst_obj_t *prodtst_obj)
{

    int num = 0;
    int parse_num = 0;
    int index = 0;
    qm_err_t ret = QM_EOK;
    qm_at_response_t *resp = NULL;
    prodtst_testsuite_t *testsuite = NULL;
    prodtst_data_t *prodtst_data = NULL;

    testsuite = prodtst_testsuite_get();

    for(num = 0; num < testsuite->specific_cmd_num; num++){

        resp = qm_at_client_create_resp(testsuite->specific_cmd[num].recv_size + 64, 0, testsuite->specific_cmd[num].timeout);
        ret = qm_at_client_obj_exec_cmd(prodtst_obj->at_client, resp, testsuite->specific_cmd[num].cmd);
        if(ret == -QM_ETIMEOUT){
            QM_LOGE(LOG_TAG, "wait specific response timeout");
            goto __exit;
        }else if(ret == -QM_ERROR){
            QM_LOGE(LOG_TAG, "specific response failed");
            goto __exit;
        }

        ret = testsuite->specific_cmd[num].parser(resp, &prodtst_data, &parse_num);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "parse specific response failed");
            goto __exit;
        }
        if(prodtst_data && parse_num > 0){
            ret = prodtst_data_table_set(&prodtst_obj->data_tbl, prodtst_data, parse_num);
        }
        qm_at_client_delete_resp(resp);
        resp = NULL;
    }

    return ret;

__exit:

    if(resp){
        qm_at_client_delete_resp(resp);
        resp = NULL;
    }

    prodtst_data_table_reset(&prodtst_obj->data_tbl);
    return ret;
}

static int prodtst_fun_handler(prodtst_obj_t *prodtst_obj, char *type, char *method, char *data, int len)
{
    int num = 0;
    int index = 0;
    qm_err_t ret = QM_EOK;
    qm_at_response_t *resp = NULL;
    prodtst_cmd_t *fun_cmd = NULL;
    prodtst_data_t *prodtst_data = NULL;

    fun_cmd = prodtst_fun_cmd_get(type, method);
    if(fun_cmd == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    resp = qm_at_client_create_resp(fun_cmd->recv_size, 0, fun_cmd->timeout);
    if(resp == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    ret = qm_at_client_obj_exec_cmd(prodtst_obj->at_client, resp, "%s=\"%s\",\"%s\"", fun_cmd->cmd, method, data);
    if(ret == -QM_ETIMEOUT){
        QM_LOGE(LOG_TAG, "wait fun response timeout");
        goto __exit;
    }else if(ret == -QM_ERROR){
        QM_LOGE(LOG_TAG, "fun response failed");
        goto __exit;
    }

    if(fun_cmd->parser){
        ret = fun_cmd->parser(resp, &prodtst_data, &num);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "parse fun response failed");
            goto __exit;
        }
        if(prodtst_data && num > 0){
            ret = prodtst_data_table_set(&prodtst_obj->data_tbl, prodtst_data, num);
        }
    }

    qm_at_client_delete_resp(resp);
    resp = NULL;

    return ret;

__exit:

    if(resp){
        qm_at_client_delete_resp(resp);
        resp = NULL;
    }

    prodtst_data_table_reset(&prodtst_obj->data_tbl);
    return ret;
}

static int prodtst_notify_handler(prodtst_obj_t *prodtst_obj, char *notify_type, int notify_value)
{
    int num = 0;
    int index = 0;
    qm_err_t ret = QM_EOK;
    qm_at_response_t *resp = NULL;
    prodtst_cmd_t *misc_cmd = NULL;
    prodtst_data_t *prodtst_data = NULL;

    misc_cmd = prodtst_misc_cmd_get("notify");
    if(misc_cmd == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    resp = qm_at_client_create_resp(misc_cmd->recv_size, 0, misc_cmd->timeout);
    ret = qm_at_client_obj_exec_cmd(prodtst_obj->at_client, resp, "%s=\"%s\",%d", misc_cmd->cmd, notify_type, notify_value);
    if(ret == -QM_ETIMEOUT){
        QM_LOGE(LOG_TAG, "wait notify response timeout");
        goto __exit;
    }else if(ret == -QM_ERROR){
        QM_LOGE(LOG_TAG, "notify response failed");
        goto __exit;
    }

__exit:

    if(resp){
        qm_at_client_delete_resp(resp);
        resp = NULL;
    }
    return ret;
}

static int prodtst_obj_state_set(prodtst_obj_t *prodtst_obj, int state)
{
    qm_mutex_lock(&prodtst_obj->lock, QM_WAIT_FOREVER);
    prodtst_obj->state = state;
    qm_mutex_unlock(&prodtst_obj->lock);

    return QM_EOK;
}

static int prodtst_obj_state_get(prodtst_obj_t *prodtst_obj, int *state)
{

    qm_mutex_lock(&prodtst_obj->lock, QM_WAIT_FOREVER);
    *state = prodtst_obj->state;
    qm_mutex_unlock(&prodtst_obj->lock);

    return QM_EOK;
}

static prodtst_obj_t *prodtst_obj_idle_get(void)
{
    int index = -1, i = 0;
    prodtst_obj_t *prodtst_obj = NULL;
    void *channel_handle = NULL;
    prodtst_adapter_t *adapter = NULL;
    adapter = prodtst_adapter_get();

    for(i = 0; i < CONFIG_QM_PRODTST_OBJ_NUM; i++){
        prodtst_obj = g_prodtst_ctx.prodtst_obj[i];
        if(prodtst_obj){
            if(prodtst_obj->state == PRODTST_STATE_NONE){
                return prodtst_obj;
            }
        }else{
            index = i;
        }
    }

    if(index == -1){
        return NULL;
    }

    prodtst_obj = prodtst_obj_creat();
    if(prodtst_obj){
        g_prodtst_ctx.prodtst_obj[index] = prodtst_obj;
    }

    prodtst_obj->index = index;
    return prodtst_obj;
}

static int prodtst_obj_info_get(prodtst_obj_info_t *obj_info)
{
    int i = 0;
    int num = 0;
    prodtst_obj_t *prodtst_obj = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_OBJ_NUM; i++){
        prodtst_obj = g_prodtst_ctx.prodtst_obj[i];
        if(prodtst_obj){
            if(prodtst_obj->state != PRODTST_STATE_NONE){
                obj_info->run_num++;
            }else{
                obj_info->idle_num++;
            } 
            obj_info->active_num++;
        }else{
            obj_info->unactive_num++;
        }
    }

    obj_info->total = CONFIG_QM_PRODTST_OBJ_NUM;
    return QM_EOK;
}

static void prodtst_timer_callback( qm_timer_t *timer, void *arg)
{
    prodtst_obj_t *prodtst_obj = (prodtst_obj_t*)arg;
    if(prodtst_obj == NULL){
        return;
    }
    prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_TIMEOUT);
}

static prodtst_obj_t *prodtst_obj_creat(void)
{
    qm_err_t ret = QM_EOK;
    prodtst_obj_t *prodtst_obj = NULL;

    prodtst_obj = (prodtst_obj_t*)qm_malloc(sizeof(prodtst_obj_t));
    if(prodtst_obj == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(prodtst_obj, 0, sizeof(prodtst_obj_t));

    ret = qm_queue_new(&prodtst_obj->notify, CONFIG_QM_PRODTST_NOTIFY_QUEUE_NUM, sizeof(prodtst_notify_t));
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_mutex_new(&prodtst_obj->lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_timer_new(&prodtst_obj->timer, prodtst_timer_callback, (void*)prodtst_obj, g_prodtst_ctx.test_time*1000, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_task_new(&prodtst_obj->task, "device", prodtst_obj_task, (void*)prodtst_obj, 
                                            CONFIG_QM_PRODTST_TASK_SIZE, CONFIG_QM_PRODTST_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }

    return prodtst_obj;

__exit:

    if(prodtst_obj){

        if(prodtst_obj->notify.hdl){
            qm_queue_free(&prodtst_obj->notify);
        }

        if(prodtst_obj->lock.hdl){
            qm_mutex_free(&prodtst_obj->lock);
        }

        if(prodtst_obj->task.hdl){
            qm_task_exit(&prodtst_obj->task);
        }

        qm_free(prodtst_obj);
        prodtst_obj = NULL;
    }

    return NULL;
}

static int prodtst_obj_destroy(prodtst_obj_t *prodtst_obj)
{
    qm_err_t ret = QM_EOK;
    qm_task_t task = {0};
    prodtst_adapter_t *adapter = NULL;
    adapter = prodtst_adapter_get();

    if(prodtst_obj == NULL){
        return -QM_EINVAL;
    }

    ret = qm_timer_free(&prodtst_obj->timer);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_queue_free(&prodtst_obj->notify);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_mutex_free(&prodtst_obj->lock);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_at_client_deinit(prodtst_obj->at_client);
    if(ret != QM_EOK){
        return ret;
    }

    if(prodtst_obj->channel_handle){
        adapter->destroy(prodtst_obj->channel_handle);
        prodtst_obj->channel_handle = NULL;
    }

    prodtst_data_table_reset(&prodtst_obj->data_tbl);

    memcpy(&task, &prodtst_obj->task, sizeof(qm_task_t));

    qm_sem_signal(&g_prodtst_ctx.exit_sem);

    ret = qm_task_exit(&task);
    if(ret != QM_EOK){
        return ret;
    }

    return ret;
}

static int prodtst_all_obj_destroy(void)
{
    int i = 0;
    prodtst_notify_t notify = {0};
    prodtst_obj_t *prodtst_obj = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_OBJ_NUM; i++){
        prodtst_obj = g_prodtst_ctx.prodtst_obj[i];
        if(prodtst_obj){
            notify.event = PRODTST_EVENT_EXIT;
            qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
            qm_sem_wait(&g_prodtst_ctx.exit_sem, QM_WAIT_FOREVER);
            qm_free(g_prodtst_ctx.prodtst_obj[i]);
            g_prodtst_ctx.prodtst_obj[i] = NULL;
        }
    }
    return QM_EOK;
}

static int prodtst_all_obj_reset(void)
{
    int i = 0;
    prodtst_notify_t notify = {0};
    prodtst_obj_t *prodtst_obj = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_OBJ_NUM; i++){
        prodtst_obj = g_prodtst_ctx.prodtst_obj[i];
        if(prodtst_obj && prodtst_obj->state != PRODTST_STATE_NONE){
            notify.event = PRODTST_EVENT_RESET;
            qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
        }
    }
    return QM_EOK;
}


/********************************************************************************************/

int prodtst_wifi_dut_found_notify(int fd)
{
    prodtst_notify_t notify = {0};
 
    notify.event = PRODTST_EVENT_WIFI_DUT_FOUND;
    notify.dat.wifi_dut_info.fd = fd;
    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));
}

int prodtst_data_notify(int event, char* type, char *uuid, int id, uint8_t *data, int len)
{
    prodtst_notify_t notify = {0};
    if(type == NULL){
        return -QM_EINVAL;
    }

    if(strlen(type) > PRODTST_DATA_TYPE_MAX_LEN){
        return -QM_EINVAL;
    }

    if(data == NULL || len <= 0){
        return -QM_EINVAL;
    }

    if(uuid == NULL){
        return -QM_EINVAL;
    }

    notify.event = event;

    notify.dat.data_info.data = (uint8_t*)qm_malloc(len+1);
    if(notify.dat.data_info.data == NULL){
        return -QM_ENOMEM;
    }
    memset(notify.dat.data_info.data, 0, len+1);

    notify.dat.data_info.id = id;
    memcpy(notify.dat.data_info.data, data, len);
    notify.dat.data_info.len = len;
    memcpy(notify.dat.data_info.uuid, uuid, strlen(uuid));

    memcpy(notify.dat.data_info.type, type, strlen(type));
    
    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));
}


int prodtst_config_notify(int test_time, char *platform, char *module_name, char *module_type)
{
    prodtst_notify_t notify = {0};
    if(test_time == 0 || platform == NULL || module_name == NULL || module_type == NULL){
        return -QM_EINVAL;
    }
    if(strlen(platform)    > PRODTST_PLATFORM_MAX_LEN    || 
       strlen(module_name) > PRODTST_MOUDLE_NAME_MAX_LEN ||
       strlen(module_type) > PRODTST_MOUDLE_TYPE_MAX_LEN){
        return -QM_EINVAL;
    }

    notify.event = PRODTST_EVENT_CONFIG;
    notify.dat.config_info.test_time = test_time;
    memcpy(notify.dat.config_info.platform, platform, strlen(platform));
    memcpy(notify.dat.config_info.module_name, module_name, strlen(module_name));
    memcpy(notify.dat.config_info.module_type, module_type, strlen(module_type));
    
    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));
}

int prodtst_special_down_notify(char *uuid, int id, char *data, int len)
{
    char *m_data = NULL;
    prodtst_notify_t notify = {0};
    if(uuid == NULL){
        return -QM_EINVAL;
    }

    notify.event = PRODTST_EVENT_SPCL_DOWN;
    memcpy(notify.dat.special_info.uuid, uuid, strlen(uuid));
    notify.dat.special_info.id = id;

    if(data && len > 0){
        m_data = (char*)qm_malloc(len+1);
        if(m_data == NULL){
            return -QM_ENOMEM;
        }
        memset(m_data, 0, len+1);
        memcpy(m_data, data, len);
    }

    notify.dat.special_info.data = m_data;
    notify.dat.special_info.len = len;

    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));
}

int prodtst_result_notify(char *uuid, int id, char *step, int result)
{
    prodtst_notify_t notify = {0};
    if(uuid == NULL || step == NULL){
        return -QM_EINVAL;
    }

    notify.event = PRODTST_EVENT_RESULT;
    notify.dat.result_info.result = result;
    if(strlen(step) > sizeof(notify.dat.result_info.step)-1){
        return -QM_EINVAL;
    }
    notify.dat.result_info.id = id;
    memcpy(notify.dat.result_info.step, step, strlen(step));
    memcpy(notify.dat.result_info.uuid, uuid, strlen(uuid));

    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));

}

int prodtst_next_round_notify(void)
{
    prodtst_notify_t notify = {0};
    notify.event = PRODTST_EVENT_NEXT_ROUND;
    return qm_queue_send(&g_prodtst_ctx.notify, (void*)&notify, sizeof(prodtst_notify_t));
}

static int prodtst_obj_init(prodtst_obj_t *prodtst_obj)
{
    prodtst_adapter_t *adapter = NULL;
    adapter = prodtst_adapter_get();

    qm_timer_stop(&prodtst_obj->timer);

    if(prodtst_obj->at_client){
        qm_at_client_deinit(prodtst_obj->at_client);
        prodtst_obj->at_client = NULL;
    }

    if(prodtst_obj->channel_handle){
        adapter->destroy(prodtst_obj->channel_handle);
        prodtst_obj->channel_handle = NULL;
    }
    prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_NONE);
    return QM_EOK;
}

static char *random_uuid( char buf[33] )
{
    const char *c = "89ab";
    char *p = buf;
    int n;

    srand(qm_now_ms());
    for( n = 0; n < 16; ++n )
    {
        int b = rand()%255;
        switch( n )
        {
            case 6:
                sprintf(p, "4%x", b%15 );
            break;
            case 8:
                sprintf(p, "%c%x", c[rand()%strlen(c)], b%15 );
            break;
            default:
                sprintf(p, "%02x", b);
            break;
        }

        p += 2;
    }
    *p = 0;
    return buf;
}

static prodtst_obj_t *prodtst_obj_get_by_uuid(char uuid[33])
{
    int i = 0;
    prodtst_obj_t *m_obj = NULL;
    for(i = 0; i < CONFIG_QM_PRODTST_OBJ_NUM; i++){
        m_obj = g_prodtst_ctx.prodtst_obj[i];
        if(m_obj && m_obj->state != PRODTST_STATE_NONE){
            if(m_obj == NULL || strlen(m_obj->uuid) != 32){
                continue;
            }
            if(0 == memcmp(m_obj->uuid, uuid, 32)){
                return m_obj;
            }
        }
    }
    return NULL;
}

static int prodtst_step_type_get(char *step)
{
    int i = 0;
    prodtst_step_t *prodtst_step = NULL;
    if(strlen(step) == 0){
        return PRODTST_STEP_TYPE_NONE;
    }

    for(i = 0; i < PRODTST_STEP_NUM; i++){
        prodtst_step = &g_prodtst_step[i];
        if(strlen(step) == strlen(prodtst_step->name) && 0 == strcmp(step, prodtst_step->name)){
            return prodtst_step->type;
        }
    }

    return PRODTST_STEP_TYPE_NONE;
}

static int prodtst_result_report(prodtst_data_table_t *data_tbl, int step, char uuid[33], int result, char *desc)
{
    qm_err_t ret = QM_EOK;

    prodtst_data_table_string_set(data_tbl, "uuid", uuid);
    prodtst_data_table_int_set(data_tbl, "result", result);
    if(step == PRODTST_STEP_TYPE_GENERIC){
        prodtst_data_table_string_set(data_tbl, "step", "generic");
    }else if(step == PRODTST_STEP_TYPE_FUN){
        prodtst_data_table_string_set(data_tbl, "step", "fun");
    }else if(step == PRODTST_STEP_TYPE_SPECIAL){
        prodtst_data_table_string_set(data_tbl, "step", "special");
    }
    if(desc){
        prodtst_data_table_string_set(data_tbl, "desc", desc);
    }
    prodtst_data_table_print(data_tbl);
    ret = prodtst_generic_send(PRODTST_EVENT_RESULT_REPORT, 0, data_tbl);
    prodtst_data_table_reset(data_tbl);

    return ret;
}

static int prodtst_generic_notify(prodtst_obj_t *prodtst_obj, int event)
{
    prodtst_notify_t notify = {0};
    notify.event = event;
    return qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
}

static void prodtst_at_result_callback(int result, void *arg)
{
    prodtst_obj_t *prodtst_obj = (prodtst_obj_t*)arg;
    if(prodtst_obj == NULL){
        return;
    }
    if(result == QM_AT_RESULT_CHANNEL_DOWN){
        prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_CHANNEL_DOWN);
     }
}

static void prodtst_obj_task(void *arg)
{
    uint32_t size = sizeof(prodtst_notify_t);
    qm_err_t ret = QM_EOK;
    prodtst_adapter_t *adapter = NULL;
    prodtst_testsuite_t *testsuite = NULL;
    prodtst_notify_t notify = {0};
    qm_at_client_param_t at_param = {0};
    prodtst_special_t *special = NULL;
    prodtst_step_type_t step_type = 0;
    prodtst_obj_t *prodtst_obj = (prodtst_obj_t*)arg;
    int obj_over = 0;

    prodtst_obj->state = PRODTST_STATE_SETUP;
    prodtst_obj->status = PRODTST_STATUS_NONE;

    adapter = prodtst_adapter_get();

    testsuite = prodtst_testsuite_get();

    at_param.recv_buf_len = testsuite->recv_size;
    at_param.send = adapter->send;
    at_param.recv = adapter->recv;
    
    while(1){
        ret = qm_queue_recv(&prodtst_obj->notify, &notify, &size, QM_WAIT_FOREVER);
        if(ret != QM_EOK){
            continue;
        }

        switch(notify.event){

        case PRODTST_EVENT_WIFI_DUT_FOUND:

            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_START);

            prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_SETUP);
            prodtst_obj->channel_handle = adapter->establish(&notify.dat);
            if(prodtst_obj->channel_handle == NULL){
                QM_LOGE(LOG_TAG, "channel fail");
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            at_param.handle = prodtst_obj->channel_handle;

            prodtst_obj->at_client = qm_at_client_init(&at_param);
            if(prodtst_obj->at_client == NULL){
                ret = -QM_ERROR;
                QM_LOGE(LOG_TAG, "at client init fail");
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            qm_at_client_register_result_cb(prodtst_obj->at_client, prodtst_at_result_callback, (void*)prodtst_obj);

            QM_LOGI(LOG_TAG, "channel established");

            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_GENERIC_START);

            prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_GENERIC);
            ret = prodtst_generic_handler(prodtst_obj);
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "generic fail");
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            qm_timer_start(&prodtst_obj->timer);

            QM_LOGI(LOG_TAG, "generic success");

            prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_SPECIFIC);
            ret = prodtst_specific_handler(prodtst_obj);
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "specific fail");
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            QM_LOGI(LOG_TAG, "specific test success");

            random_uuid(prodtst_obj->uuid);

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);
            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_test_send(&prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            break;

        case PRODTST_EVENT_SET:
            
            prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_FUN);

            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FUN_START);
            
            ret = prodtst_fun_handler(prodtst_obj, notify.dat.data_info.type, "set", (char*)notify.dat.data_info.data, notify.dat.data_info.len);
            if(notify.dat.data_info.data){
                qm_free(notify.dat.data_info.data);
                notify.dat.data_info.data = NULL;
            }
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "fun set fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_FUN, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);
            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_generic_send(PRODTST_EVENT_SET_REPLY, notify.dat.data_info.id, &prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            QM_LOGI(LOG_TAG, "fun set success");

            break;
            
        case PRODTST_EVENT_GET:

            prodtst_obj_state_set(prodtst_obj, PRODTST_STATE_FUN);

            ret = prodtst_fun_handler(prodtst_obj, notify.dat.data_info.type, "get", (char*)notify.dat.data_info.data, notify.dat.data_info.len);
            if(notify.dat.data_info.data){
                qm_free(notify.dat.data_info.data);
                notify.dat.data_info.data = NULL;
            }
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "fun get fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_FUN, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            QM_LOGI(LOG_TAG, "fun get success");

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);
            
            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_generic_send(PRODTST_EVENT_GET_REPLY, notify.dat.data_info.id, &prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            break;

        case PRODTST_EVENT_SPCL_DOWN:

            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_SPECIAL_START);
            special = prodtst_special_get("NFC");
            if(special == NULL){
                QM_LOGI(LOG_TAG, "special action fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_SPECIAL, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            prodtst_obj->spcl_handle = special->establish(NULL);
            if(prodtst_obj->spcl_handle == NULL){
                QM_LOGE(LOG_TAG, "special establish fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_SPECIAL, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            char *spcl_buf = (char*)qm_malloc(special->recv_size);
            if(spcl_buf == NULL){
                QM_LOGE(LOG_TAG, "special alloc mem fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_SPECIAL, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);
            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_generic_send(PRODTST_EVENT_SPCL_DOWN_REPLY, notify.dat.special_info.id, &prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            memset(spcl_buf, 0, special->recv_size);
            
            ret = special->process(prodtst_obj->spcl_handle, notify.dat.special_info.data, notify.dat.special_info.len, spcl_buf, special->recv_size);
            if(notify.dat.special_info.data){
                qm_free(notify.dat.special_info.data);
                notify.dat.special_info.data = NULL;
            }
            if(ret != QM_EOK){
                QM_LOGE(LOG_TAG, "special process fail");
                prodtst_result_report(&prodtst_obj->data_tbl, PRODTST_STEP_TYPE_SPECIAL, prodtst_obj->uuid, PRODTST_RESULT_FAIL, NULL);
                prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                qm_free(spcl_buf);
                break;
            }

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "data", spcl_buf);
            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);

            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_generic_send(PRODTST_EVENT_SPCL_UP, notify.dat.special_info.id, &prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            qm_free(spcl_buf);

            break;

        case PRODTST_EVENT_RESULT:

            step_type = prodtst_step_type_get(notify.dat.result_info.step);
            if(step_type == PRODTST_STEP_TYPE_NONE){
                QM_LOGE(LOG_TAG, "special deinit fail");
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                break;
            }

            prodtst_data_table_string_set(&prodtst_obj->data_tbl, "uuid", prodtst_obj->uuid);
            prodtst_data_table_print(&prodtst_obj->data_tbl);
            prodtst_generic_send(PRODTST_EVENT_RESULT_REPLY, notify.dat.result_info.id, &prodtst_obj->data_tbl);
            prodtst_data_table_reset(&prodtst_obj->data_tbl);

            if(step_type == PRODTST_STEP_TYPE_ALL){
                if(notify.dat.result_info.result == PRODTST_RESULT_OK ){
                    prodtst_notify_handler(prodtst_obj, "FAC", PRODTST_RESULT_OK);
                    prodtst_status_notify(prodtst_obj, PRODTST_STATUS_SUCCESS);
                    QM_LOGI(LOG_TAG, "--- prodtst success ---");
                }else{
                    prodtst_notify_handler(prodtst_obj, "FAC", PRODTST_RESULT_FAIL);
                    prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);
                    QM_LOGI(LOG_TAG, "--- prodtst failed result:%d ---", notify.dat.result_info.result);
                }
                prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);
                obj_over = 1;
            }else if(step_type == PRODTST_STEP_TYPE_GENERIC){
                if(notify.dat.result_info.result == PRODTST_RESULT_OK){
                    QM_LOGI(LOG_TAG, "--- prodtst generic success ---");
                    prodtst_status_notify(prodtst_obj, PRODTST_STATUS_GENERIC_SUCCESS);
                }else{
                    QM_LOGI(LOG_TAG, "--- prodtst generic fail ---");
                }
            }else if(step_type == PRODTST_STEP_TYPE_FUN){
                if(notify.dat.result_info.result == PRODTST_RESULT_OK){
                    QM_LOGI(LOG_TAG, "--- prodtst fun success ---");
                    prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FUN_SUCCESS);
                }else{
                    QM_LOGI(LOG_TAG, "--- prodtst fun fail ---");
                }
            }else if(step_type == PRODTST_STEP_TYPE_SPECIAL){
                if(notify.dat.result_info.result == PRODTST_RESULT_OK){
                    QM_LOGI(LOG_TAG, "--- prodtst special success ---");
                    prodtst_status_notify(prodtst_obj, PRODTST_STATUS_SPECIAL_SUCCESS);
                }else{
                    QM_LOGI(LOG_TAG, "--- prodtst special fail ---");
                }
            }

            break;

        case PRODTST_EVENT_TIMEOUT:

            QM_LOGI(LOG_TAG, "prodtst timeout failed");

            if(obj_over){
                break;
            }

            prodtst_notify_handler(prodtst_obj, "FAC", PRODTST_RESULT_FAIL);
            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL);

            prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET);

        break;

        case PRODTST_EVENT_CHANNEL_DOWN:
            QM_LOGI(LOG_TAG, "prodtst channel down");
            QM_LOGI(LOG_TAG, "prodtst status: %d", prodtst_obj->status);
            if(obj_over){
                break;
            }
        
            prodtst_status_notify(prodtst_obj, PRODTST_STATUS_FAIL); 
            prodtst_generic_notify(prodtst_obj, PRODTST_EVENT_RESET); 
            
        break;

        case PRODTST_EVENT_RESET:
            QM_LOGI(LOG_TAG, "prodtst reset");
            prodtst_obj_init(prodtst_obj);
            prodtst_obj->status = PRODTST_STATUS_NONE;
            obj_over = 0;

        break;

        case PRODTST_EVENT_EXIT:

            if(special){
                special->destroy(prodtst_obj->spcl_handle);
            }
            
            prodtst_obj_destroy(prodtst_obj);
            QM_LOGI(LOG_TAG, "prodtst obj exit");

        break;
        
        default:
            break;
        }
    }

    qm_task_exit(&prodtst_obj->task);
}

int is_prodtst_ready(void)
{
    return g_prodtst_ctx.ready;
}

static void prodtst_core_task(void *arg)
{
    qm_err_t ret = QM_EOK;
    prodtst_notify_t notify = {0};
    uint32_t size = 0;
    prodtst_info_t info = {0};
    prodtst_obj_t *prodtst_obj = NULL;
    prodtst_context_t *prodtst_ctx = (prodtst_context_t*)arg;
    prodtst_adapter_t *adapter = NULL;
    void *channel_handle = NULL;
    prodtst_obj_info_t obj_info = {0};
    adapter = prodtst_adapter_get();
    int prodtst_status = PRODTST_STATUS_NONE;

    while(1){
        ret = qm_queue_recv(&prodtst_ctx->notify, &notify, &size, QM_WAIT_FOREVER);
        if(ret != QM_EOK){
            continue;
        }

        switch(notify.event){

            case PRODTST_EVENT_CONFIG:

                prodtst_ctx->ready = 0;
                
                info.platform = &notify.dat.config_info.platform;
                info.module_name = &notify.dat.config_info.module_name;
                info.module_type = &notify.dat.config_info.module_type;
                QM_LOGE(LOG_TAG, "prodtst info platform: %s, module_name: %s, module_type: %s",
                                            info.platform, info.module_name, info.module_type);

                memset(&prodtst_ctx->config_info, 0, sizeof(prodtst_config_info_t));
                prodtst_ctx->test_time = notify.dat.config_info.test_time;
                memcpy(&prodtst_ctx->config_info.platform, notify.dat.config_info.platform, strlen(notify.dat.config_info.platform));
                memcpy(&prodtst_ctx->config_info.dev_type, notify.dat.config_info.dev_type, strlen(notify.dat.config_info.dev_type));
                memcpy(&prodtst_ctx->config_info.module_type, notify.dat.config_info.module_type, strlen(notify.dat.config_info.module_type));
                memcpy(&prodtst_ctx->config_info.module_name, notify.dat.config_info.module_name, strlen(notify.dat.config_info.module_name));

                prodtst_stop();

                ret = prodtst_start(&info);
                if(ret != QM_EOK){
                    prodtst_status = PRODTST_STATUS_NOT_READY;
                    prodtst_status_notify(NULL, PRODTST_STATUS_NOT_READY);
                    QM_LOGE(LOG_TAG, "prodtst start failed");
                }else{
                    prodtst_status = PRODTST_STATUS_CONFIG_READY;
                    prodtst_status_notify(NULL, PRODTST_STATUS_CONFIG_READY);
                    QM_LOGI(LOG_TAG, "prodtst start success");
                }

            break;

            case PRODTST_EVENT_WIFI_DUT_FOUND:

                prodtst_obj = prodtst_obj_idle_get();
                if(!prodtst_obj){
                    QM_LOGI(LOG_TAG, "prodtst object is busy");
                }else{
                    qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
                }

                prodtst_ctx->ready = 0;

                memset(&obj_info, 0, sizeof(prodtst_obj_info_t));
                prodtst_obj_info_get(&obj_info);
                QM_LOGI(LOG_TAG, "prodtst obj run: %d, idle: %d, active: %d, unactive: %d, total: %d", 
                obj_info.run_num, obj_info.idle_num, obj_info.active_num, obj_info.unactive_num, obj_info.total);

            break;

            case PRODTST_EVENT_SET:
            case PRODTST_EVENT_GET:

                prodtst_obj = prodtst_obj_get_by_uuid(notify.dat.data_info.uuid);
                if(prodtst_obj == NULL){
                    QM_LOGE(LOG_TAG, "prodtst uuid is invalid");
                }else{
                    qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
                }
                
            break;

            case  PRODTST_EVENT_SPCL_DOWN:

                prodtst_obj = prodtst_obj_get_by_uuid(notify.dat.special_info.uuid);
                if(prodtst_obj == NULL){
                    QM_LOGE(LOG_TAG, "prodtst uuid is invalid");
                }else{
                    qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
                }

            break;

            case PRODTST_EVENT_RESULT:

                prodtst_obj = prodtst_obj_get_by_uuid(notify.dat.result_info.uuid);
                if(prodtst_obj == NULL){
                    QM_LOGE(LOG_TAG, "prodtst uuid is invalid");
                }else{
                    qm_queue_send(&prodtst_obj->notify, (void*)&notify, sizeof(prodtst_notify_t));
                }

            break;

            case PRODTST_EVENT_NEXT_ROUND:
                
                prodtst_ctx->ready = 1;
                prodtst_all_obj_reset();
                if(prodtst_status == PRODTST_STATUS_CONFIG_READY){
                    QM_LOGI(LOG_TAG, "prodtst ready");
                    prodtst_status_notify(NULL, PRODTST_STATUS_READY);
                }else{
                    QM_LOGI(LOG_TAG, "first prodtst config");
                }

            break;

            default:
            break;
        }
    }

}

int  prodtst_init(prodtst_notify_cb notify_cb)
{
    qm_err_t ret = QM_EOK;
    memset(&g_prodtst_ctx, 0, sizeof(prodtst_context_t));

    ret = qm_queue_new(&g_prodtst_ctx.notify, CONFIG_QM_PRODTST_NOTIFY_QUEUE_NUM, sizeof(prodtst_notify_t));
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_sem_new(&g_prodtst_ctx.exit_sem, 0);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_task_new(&g_prodtst_ctx.task, "prodtst core", prodtst_core_task, (void*)&g_prodtst_ctx, CONFIG_QM_PRODTST_TASK_SIZE, CONFIG_QM_PRODTST_TASK_PRIO);
    if(ret != QM_EOK){
        return ret;
    }

    g_prodtst_ctx.notify_cb = notify_cb;

    return QM_EOK;
}







