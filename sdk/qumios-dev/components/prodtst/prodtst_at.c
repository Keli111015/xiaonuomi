#include "prodtst_at.h"
#include "qm_errno.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_at.h"
#include "qm_kernel.h"
#include "data_channel.h"
#include "cJSON.h"
#include "prodtst_base.h"
#include "prodtst_res.h"

#define LOG_TAG "prodtst at"

#ifndef CONFIG_VIRTUAL_SERVER_CHANNEL_TASK_SIZE
#define CONFIG_VIRTUAL_SERVER_CHANNEL_TASK_SIZE     1024
#endif 

#ifndef CONFIG_VIRTUAL_SERVER_CHANNEL_TASK_PRIO
#define CONFIG_VIRTUAL_SERVER_CHANNEL_TASK_PRIO     20
#endif

#ifndef CONFIG_PRODTST_INNER_AT_CHANNEL_MAX_NUM
#define CONFIG_PRODTST_INNER_AT_CHANNEL_MAX_NUM     8
#endif

#ifndef CONFIG_PRODTST_EXT_AT_CHANNEL_MAX_NUM
#define CONFIG_PRODTST_EXT_AT_CHANNEL_MAX_NUM       8
#endif

#ifndef CONFIG_PRODTST_DATA_CHANNEL_SIZE
#define CONFIG_PRODTST_DATA_CHANNEL_SIZE     128
#endif

#ifndef CONFIG_PRODTST_AT_CMD_TABLE_MAX_NUM
#define CONFIG_PRODTST_AT_CMD_TABLE_MAX_NUM     4
#endif

#define PRODTST_AT_RESULT_OK     "ok"
#define PRODTST_AT_RESULT_FAIL   "fail"

typedef struct {
    int busy;
    void *dc_server;
    qm_at_server_t *at_server;
}prodtst_inner_at_server_t;

typedef struct {
    uint16_t cmd_num;  
    qm_mutex_t mutex;  
    uint16_t at_cmd_num[CONFIG_PRODTST_AT_CMD_TABLE_MAX_NUM];   
    qm_at_cmd_t *at_cmd_table[CONFIG_PRODTST_AT_CMD_TABLE_MAX_NUM];  
    prodtst_inner_at_server_t inner_at_server[CONFIG_PRODTST_INNER_AT_CHANNEL_MAX_NUM];
} prodtst_at_ctx_t;

static prodtst_at_ctx_t g_prodtst_at_ctx = {0};

static prodtst_inner_at_server_t *prodtst_inner_at_server_unused_get(void);
static prodtst_inner_at_server_t *prodtst_inner_at_server_idle_get(void);

static int prodtst_at_inner_send(void *handle, char *buf, int size);
static int prodtst_at_inner_recv(void *handle, char *buf, int size, int timeout);
static int prodtst_inner_at_server_busy_set(prodtst_inner_at_server_t *inner_at_server, int busy);
static qm_at_client_t *prodtst_at_client_init(uint32_t recv_size, prodtst_at_link_t *at_link);

int prodtst_at_register_commands(qm_at_cmd_t *cmd_tbl, uint16_t cmd_num)
{
    int i = 0;
    qm_mutex_lock(&g_prodtst_at_ctx.mutex, QM_WAIT_FOREVER);
    for(i = 0; i < CONFIG_PRODTST_AT_CMD_TABLE_MAX_NUM; i++){
        if(g_prodtst_at_ctx.at_cmd_table[i]){
            continue;
        }
        g_prodtst_at_ctx.at_cmd_table[i] = cmd_tbl;
        g_prodtst_at_ctx.at_cmd_num[i] = cmd_num;
        g_prodtst_at_ctx.cmd_num ++;
        break;
    }
    qm_mutex_unlock(&g_prodtst_at_ctx.mutex);
 
    return QM_EOK;
}

static qm_at_client_t *prodtst_at_client_init(uint32_t recv_size, prodtst_at_link_t *at_link)
{
    qm_at_client_param_t param = {0};

    param.sync = 1;
    param.recv_buf_len = recv_size;
    param.recv = at_link->read;
    param.send = at_link->write;
    param.handle = at_link->handle;

    return qm_at_client_init(&param);
}

int prodtst_at_client_generic_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, prodtst_at_link_t *at_link, prodtst_at_rsp_data_t *at_rsp_data)
{
    char *at_value = NULL;
    qm_err_t ret = QM_EOK;
    qm_at_response_t *at_resp = NULL;
    qm_at_client_t *client = NULL;

    if(cmd == NULL || recv_rule_at == NULL || at_link == NULL || at_rsp_data == NULL){
        return -QM_EINVAL;
    }

    client = prodtst_at_client_init(recv_rule_at->recv_size, at_link);
    if(client == NULL){
        QM_LOGE(LOG_TAG, "prodtst at client init fail");
        ret = -QM_ENOMEM;
        goto __exit;
    }

    at_resp = qm_at_client_create_resp(recv_rule_at->recv_size, 0, recv_rule_at->wait_time);
    if(at_resp == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    qm_at_client_obj_exec_cmd_timeout(client, recv_rule_at->wait_time, at_resp, cmd);
    if(ret != QM_EOK && ret != -QM_ERROR){
        QM_LOGE(LOG_TAG, "prodtst at client exec cmd fail: %d", ret);
        goto __exit;
    }

    if(client->resp_status == QM_AT_RESP_OK){
        at_rsp_data->reuslt = PRODTST_AT_RESULT_OK;
        if(at_resp->line_counts > 1){
            at_value = (char*)qm_at_client_resp_get_line(at_resp, 1);
            if(at_value == NULL){
                ret = -QM_EINVAL;
                goto __exit;
            }
            QM_LOGD(LOG_TAG, "prodtst at value: %s", at_value);

            at_rsp_data->value = malloc_and_copy(at_value, strlen(at_value));
            if(at_rsp_data->value == NULL){
                ret = -QM_ENOMEM;
                goto __exit;
            }
        }
    }else{
        at_rsp_data->reuslt = PRODTST_AT_RESULT_FAIL;
    }

__exit:
    if(at_resp){
        qm_at_client_delete_resp(at_resp);
    }
    if(client){
        qm_at_client_deinit(client);
    }
    return ret;
}

static int prodtst_at_res_read(void *handle, char *data, int len, int timeout)
{
    prodtst_addr_t *addr = NULL;
    qm_list_t *addr_list = (qm_list_t*)handle;

    addr = prodtst_addr_last_get(addr_list);
    if(addr == NULL){
        return -QM_EINVAL;
    }
    return prodtst_res_readtimeout(addr->hd_type, handle, (void*)data, len, timeout, NULL);
}

static int prodtst_at_res_write(void *handle, char *data, int len)
{
    prodtst_addr_t *addr = NULL;
    qm_list_t *addr_list = (qm_list_t*)handle;

    addr = prodtst_addr_last_get(addr_list);
    if(addr == NULL){
        return -QM_EINVAL;
    }
    return prodtst_res_write(addr->hd_type, handle, (void*)data, len, NULL);
}

static int prodtst_at_res_read_cfg(void *handle, char *data, int len, int timeout)
{
    prodtst_addr_t *addr = NULL;
    qm_list_t *addr_list = (qm_list_t*)handle;

    addr = prodtst_addr_first_get(addr_list);
    if(addr == NULL){
        return -QM_EINVAL;
    }
    return prodtst_res_read_cfg_timeout(PRODTST_HD_TYPE_UART, handle, (void*)data, len, timeout);
}

static int prodtst_at_res_write_cfg(void *handle, char *data, int len)
{
    prodtst_addr_t *addr = NULL;
    qm_list_t *addr_list = (qm_list_t*)handle;

    addr = prodtst_addr_first_get(addr_list);
    if(addr == NULL){
        return -QM_EINVAL;
    }
    return prodtst_res_write_cfg(PRODTST_HD_TYPE_UART, handle, (void*)data, len);
} 

int prodtst_at_client_ext_exec_cfg_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, void *addr, prodtst_at_rsp_data_t *at_rsp_data)
{   
    prodtst_at_link_t at_link = {0};

    if(cmd == NULL || recv_rule_at == NULL || addr == NULL || at_rsp_data == NULL){
        return -QM_EINVAL;
    }

    at_link.handle = (void*)addr;
    at_link.read = prodtst_at_res_read_cfg;
    at_link.write = prodtst_at_res_write_cfg;

    return prodtst_at_client_generic_exec_cmd(cmd, recv_rule_at, &at_link, at_rsp_data);
}

int prodtst_at_client_ext_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, void *addr, prodtst_at_rsp_data_t *at_rsp_data)
{   
    prodtst_at_link_t at_link = {0};

    if(cmd == NULL || recv_rule_at == NULL || addr == NULL || at_rsp_data == NULL){
        return -QM_EINVAL;
    }

    at_link.handle = (void*)addr;
    at_link.read = prodtst_at_res_read;
    at_link.write = prodtst_at_res_write;

    return prodtst_at_client_generic_exec_cmd(cmd, recv_rule_at, &at_link, at_rsp_data);
}

int prodtst_at_client_inner_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, prodtst_at_rsp_data_t *at_rsp_data)
{
    qm_err_t ret = QM_EOK;
    void *dc_client = NULL;
    prodtst_at_link_t at_link = {0};
    prodtst_inner_at_server_t *inner_at_server = NULL;

    dc_client = data_channel_client_creat();
    if(dc_client == NULL){
        return -QM_ENOMEM;
    }
    
    inner_at_server = prodtst_inner_at_server_idle_get();
    if(inner_at_server == NULL){
        return -QM_ERROR;
    }
    data_channel_connect(inner_at_server->dc_server, dc_client);
    at_link.handle = dc_client;
    at_link.read = prodtst_at_inner_recv;
    at_link.write = prodtst_at_inner_send;
    ret = prodtst_at_client_generic_exec_cmd(cmd, recv_rule_at, &at_link, at_rsp_data);
    data_channel_destroy(dc_client);
    prodtst_inner_at_server_busy_set(inner_at_server, 0);

    return ret;
}

static prodtst_inner_at_server_t *prodtst_inner_at_server_unused_get(void)
{
    int i = 0;
    prodtst_inner_at_server_t *inner_at_server = NULL;
    for(i = 0; i < CONFIG_PRODTST_INNER_AT_CHANNEL_MAX_NUM; i++){
        inner_at_server = &g_prodtst_at_ctx.inner_at_server[i];
        if(inner_at_server->at_server == NULL){
            return inner_at_server;
        }
    }
    return NULL;
}

static int prodtst_at_inner_send(void *handle, char *buf, int size)
{
    return data_channel_write(handle, (uint8_t*)buf, size, 1000);
}

static int prodtst_at_inner_recv(void *handle, char *buf, int size, int timeout)
{
    return data_channel_read(handle, (uint8_t*)buf, size, timeout);
}

static prodtst_inner_at_server_t *prodtst_inner_at_server_idle_get(void)
{
    int i = 0;
    qm_at_server_param_t param = {0};
    prodtst_inner_at_server_t *inner_at_server = NULL;

    qm_mutex_lock(&g_prodtst_at_ctx.mutex, QM_WAIT_FOREVER);
    for(i = 0; i < CONFIG_PRODTST_INNER_AT_CHANNEL_MAX_NUM; i++){
        if(g_prodtst_at_ctx.inner_at_server[i].at_server && 
            g_prodtst_at_ctx.inner_at_server[i].busy == 0){
            g_prodtst_at_ctx.inner_at_server[i].busy = 1;
            qm_mutex_unlock(&g_prodtst_at_ctx.mutex);
            return &g_prodtst_at_ctx.inner_at_server[i];
        }
    }

    inner_at_server = prodtst_inner_at_server_unused_get();
    qm_mutex_unlock(&g_prodtst_at_ctx.mutex);

    if(inner_at_server == NULL){
        return NULL;
    }

    inner_at_server->busy = 1;
    inner_at_server->dc_server = data_channel_server_creat(CONFIG_PRODTST_DATA_CHANNEL_SIZE, CONFIG_PRODTST_DATA_CHANNEL_SIZE);
    if(inner_at_server->dc_server == NULL){
        return NULL;
    }

    param.handle = inner_at_server->dc_server;
    param.recv = prodtst_at_inner_recv;
    param.send = prodtst_at_inner_send;
    param.recv_buf_len = CONFIG_PRODTST_DATA_CHANNEL_SIZE;

    inner_at_server->at_server = qm_at_server_init(&param);
    if(inner_at_server->at_server == NULL){
        data_channel_destroy(inner_at_server->dc_server);
        return NULL;
    }

    for(int i = 0; i < g_prodtst_at_ctx.cmd_num; i++){
        qm_at_register_commands(inner_at_server->at_server, g_prodtst_at_ctx.at_cmd_table[i], g_prodtst_at_ctx.at_cmd_num[i]);
    }

    return inner_at_server;
}

static int prodtst_inner_at_server_busy_set(prodtst_inner_at_server_t *inner_at_server, int busy)
{
    qm_mutex_lock(&g_prodtst_at_ctx.mutex, QM_WAIT_FOREVER);
    inner_at_server->busy = busy;
    qm_mutex_unlock(&g_prodtst_at_ctx.mutex);
    return QM_EOK;
}

int prodtst_at_init(void)
{   
    qm_err_t ret = QM_EOK;
    ret = qm_mutex_new(&g_prodtst_at_ctx.mutex);
    if(ret != QM_EOK){
        return ret;
    }

    return ret;
}




