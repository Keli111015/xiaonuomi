#include "prodtst_dt.h"
#include "prodtst_data.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "cJSON.h"
#include "prodtst_base.h"
#include "qm_log.h"
#include "qm_utils_timer.h"
#include "serial_protocol.h"
#include "qm_utils_string.h"
#include "prodtst_channel.h"

#define LOG_TAG "prodtst dt"

//at
static int prodtst_dt_at_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_at_rule_pack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_at_unpack(void *data, prodtst_dt_data_t *dt_data);
static int prodtst_dt_at_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_at_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_at_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object);
static int prodtst_dt_at_destroy(prodtst_dt_data_t *dt_data);
static int prodtst_dt_at_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data);

//xjiang uart
static int prodtst_dt_xjiang_uart_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_xjiang_uart_rule_pack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_xjiang_uart_unpack(void *data, prodtst_dt_data_t *dt_data);
static int prodtst_dt_xjiang_uart_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_xjiang_uart_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_xjiang_uart_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object);
static int prodtst_dt_xjiang_uart_destroy(prodtst_dt_data_t *dt_data);
static int prodtst_dt_xjiang_uart_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data);

//hex uart
static int prodtst_dt_hex_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_hex_rule_pack(void *json_object, prodtst_dt_data_t *dt_data);
static int prodtst_dt_hex_unpack(void *data, prodtst_dt_data_t *dt_data);
static int prodtst_dt_hex_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_hex_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);
static int prodtst_dt_hex_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object);
static int prodtst_dt_hex_destroy(prodtst_dt_data_t *dt_data);
static int prodtst_dt_hex_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data);

//ble mesh hex
static int prodtst_dt_ble_mesh_at_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);

static prodtst_dt_handle_t *prodtst_dt_handle_get(prodtst_data_format_t data_format, prodtst_addr_t *addr);

static prodtst_data_format_map_t g_prodtst_data_format_map[] = {
    { "at",          PRODTST_DATA_FORMAT_AT          },
    { "xjiangUart",  PRODTST_DATA_FORMAT_XJIANG_UART },
    { "hex",         PRODTST_DATA_FORMAT_HEX         },
    { "string",      PRODTST_DATA_FORMAT_STRING      },
};

static prodtst_dt_handle_t g_prodtst_dt_handle[] = {

    {PRODTST_HD_TYPE_NONE,       PRODTST_DATA_FORMAT_AT,           prodtst_dt_at_rule_unpack,                   prodtst_dt_at_rule_pack,                prodtst_dt_at_unpack,               prodtst_dt_at_rsp_unpack,               prodtst_dt_at_handle,               prodtst_dt_at_rsp_pack,             prodtst_dt_at_destroy,             prodtst_dt_at_rsp_destroy            },
    {PRODTST_HD_TYPE_NONE,       PRODTST_DATA_FORMAT_XJIANG_UART,  prodtst_dt_xjiang_uart_rule_unpack,          prodtst_dt_xjiang_uart_rule_pack,       prodtst_dt_xjiang_uart_unpack,      prodtst_dt_xjiang_uart_rsp_unpack,      prodtst_dt_xjiang_uart_handle,      prodtst_dt_xjiang_uart_rsp_pack,    prodtst_dt_xjiang_uart_destroy,    prodtst_dt_xjiang_uart_rsp_destroy   },
    {PRODTST_HD_TYPE_BLE_MESH,   PRODTST_DATA_FORMAT_AT,           prodtst_dt_at_rule_unpack,                   prodtst_dt_at_rule_pack,                prodtst_dt_at_unpack,               prodtst_dt_at_rsp_unpack,               prodtst_dt_ble_mesh_at_handle,      prodtst_dt_at_rsp_pack,             prodtst_dt_at_destroy,             prodtst_dt_at_rsp_destroy            },
    {PRODTST_HD_TYPE_NONE,       PRODTST_DATA_FORMAT_HEX,          prodtst_dt_hex_rule_unpack,                  prodtst_dt_hex_rule_pack,               prodtst_dt_hex_unpack,              prodtst_dt_hex_rsp_unpack,              prodtst_dt_hex_handle,              prodtst_dt_hex_rsp_pack,            prodtst_dt_hex_destroy,            prodtst_dt_hex_rsp_destroy           },
    
};

prodtst_data_format_t prodtst_data_format_get(char *str_format)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_data_format_map); i++){
        if(0 == strcmp(g_prodtst_data_format_map[i].str_format, str_format)){
            return g_prodtst_data_format_map[i].data_format;
        }
    }
    return PRODTST_DATA_FORMAT_NONE;
}

char *prodtst_data_format_str_get(prodtst_data_format_t data_format)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_data_format_map); i++){
        if(data_format == g_prodtst_data_format_map[i].data_format){
            return g_prodtst_data_format_map[i].str_format;
        }
    }
    return NULL;
}

static prodtst_dt_handle_t *prodtst_dt_handle_get(prodtst_data_format_t data_format, prodtst_addr_t *addr)
{
    int i = 0;

    if(addr == NULL){
        return NULL;
    }

    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_dt_handle); i++){
        if(data_format == g_prodtst_dt_handle[i].data_format && 
            addr->hd_type == g_prodtst_dt_handle[i].hd_type){
            return &g_prodtst_dt_handle[i];
        }
    }

    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_dt_handle); i++){
        if(data_format == g_prodtst_dt_handle[i].data_format){
            return &g_prodtst_dt_handle[i];
        }
    }
    return NULL;
}

int prodtst_dt_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(json_object == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_data->data_format, &dt_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }

    return dt_handle->rule_unpack(json_object, dt_data);
}

int prodtst_dt_rule_pack(void *json_object, prodtst_dt_data_t *dt_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(json_object == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_data->data_format, &dt_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }

    return dt_handle->rule_pack(json_object, dt_data);        
}

int prodtst_dt_unpack(void *data, prodtst_dt_data_t *dt_data)
{    
    prodtst_dt_handle_t *dt_handle = NULL;

    if(data == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_data->data_format, &dt_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }

    return dt_handle->unpack(data, dt_data);
}

int prodtst_dt_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(data == NULL || dt_rsp_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_rsp_data->data_format, &dt_rsp_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }

    return dt_handle->rsp_unpack(data, dt_rsp_data);
}

int prodtst_dt_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(dt_data == NULL || dt_rsp_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_data->data_format, &dt_rsp_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }

    return dt_handle->handle(dt_data, dt_rsp_data);
}

int prodtst_dt_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(json_object == NULL || dt_rsp_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_rsp_data->data_format, &dt_rsp_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }
    return dt_handle->rsp_pack(dt_rsp_data, json_object);
}

int prodtst_dt_destroy(prodtst_dt_data_t *dt_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(dt_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_data->data_format, &dt_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }
    return dt_handle->destroy(dt_data);
}

int prodtst_dt_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_dt_handle_t *dt_handle = NULL;

    if(dt_rsp_data == NULL){
        return -QM_EINVAL;
    }

    dt_handle = prodtst_dt_handle_get(dt_rsp_data->data_format, &dt_rsp_data->addr);
    if(dt_handle == NULL){
        return -QM_EINVAL;
    }
    return dt_handle->rsp_destroy(dt_rsp_data);
}

static int prodtst_dt_at_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data)
{
    int wait_time = 0;
    int ret = QM_EOK;
    cJSON *root = (cJSON*)json_object;
    if(root == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, PRODTST_JSON_KEY_WAIT_TIME, &wait_time);
    if(ret != QM_EOK){
        return ret;
    }
    dt_data->recv_rule.rule.rule_at.wait_time = wait_time;
    dt_data->recv_rule.timeout = wait_time;

    return QM_EOK;
}

static int prodtst_dt_at_rule_pack(void *json_object, prodtst_dt_data_t *dt_data)
{
    cJSON *root = (cJSON*)json_object;
    if(root == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    cJSON_AddNumberToObject(root, PRODTST_JSON_KEY_WAIT_TIME, dt_data->recv_rule.rule.rule_at.wait_time);

    return QM_EOK;
}

static int prodtst_dt_at_unpack(void *data, prodtst_dt_data_t *dt_data)
{
    uint32_t len  = strlen(data);
    prodtst_at_data_t *at_data = &dt_data->data.at_data;
    if(len == 0){
        return -QM_EINVAL;
    }

    at_data->len = (int)len;
    at_data->data = malloc_and_copy(data, len);
    if(at_data->data == NULL){
        return -QM_ENOMEM;
    }
    return QM_EOK;
}

static int prodtst_dt_at_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    qm_err_t ret = QM_EOK;
    char *str = NULL;
    cJSON *root  = (cJSON*)data;
    prodtst_at_rsp_data_t *at_rsp_data = NULL;

    at_rsp_data = &dt_rsp_data->data.at_rsp_data;
    
    str = cjson_str_get(root, PRODTST_JSON_KEY_VALUE);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    at_rsp_data->value = (char*)malloc_and_copy(str, strlen(str));

    str = cjson_str_get(root, PRODTST_JSON_KEY_RESULT);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    at_rsp_data->reuslt = (char*)malloc_and_copy(str, strlen(str));
    
    return QM_EOK;
    
__exit:
    prodtst_dt_at_rsp_destroy(dt_rsp_data);
    return ret;
}

static int prodtst_dt_at_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    qm_err_t ret = QM_EOK;
    prodtst_addr_t *addr = &dt_rsp_data->addr;
    prodtst_at_data_t *at_data = &dt_data->data.at_data;
    at_data->addr = &dt_rsp_data->addr;
    prodtst_channel_type_t channel_type = PRODTST_CHANNEL_TYPE_NONE;

    prodtst_channel_ctrl(addr, PRODTST_CHANNEL_CMD_CLEAR);

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){

        channel_type = prodtst_channel_type_get(addr->hd_type);
        
        if(channel_type != PRODTST_CHANNEL_TYPE_NONE && addr->sub_id){
            dt_data->recv_rule.rule.rule_at.recv_size = CONFIG_PRODTST_EXT_AT_BUF_MAX_SIZE;
            ret = prodtst_at_client_ext_exec_cmd(&dt_data->recv_rule.rule.rule_at, at_data, &dt_rsp_data->data.at_rsp_data);
            if(ret != QM_EOK){
                goto __exit;
            }
        }else{
            dt_data->recv_rule.rule.rule_at.recv_size = CONFIG_PRODTST_INNER_AT_BUF_MAX_SIZE;
            ret = prodtst_at_client_inner_exec_cmd(dt_data->data.at_data.data, &dt_data->recv_rule.rule.rule_at, &dt_rsp_data->data.at_rsp_data);   
            if(ret != QM_EOK){
                goto __exit;
            }
        }

    }else{
        if(addr->sub_id){
            QM_LOGD(LOG_TAG, "at ext exec start...");
            dt_data->recv_rule.rule.rule_at.recv_size = CONFIG_PRODTST_EXT_AT_BUF_MAX_SIZE;
            ret = prodtst_at_client_ext_exec_cmd(&dt_data->recv_rule.rule.rule_at, at_data, &dt_rsp_data->data.at_rsp_data);
            if(ret != QM_EOK){
                goto __exit;
            }
        }else{
            QM_LOGD(LOG_TAG, "config at ext exec start...");
            dt_data->recv_rule.rule.rule_at.recv_size = CONFIG_PRODTST_EXT_CFG_AT_BUF_MAX_SIZE;
            ret = prodtst_at_client_ext_exec_cfg_cmd(dt_data->data.at_data.data, &dt_data->recv_rule.rule.rule_at, &dt_data->addr, &dt_rsp_data->data.at_rsp_data);
            if(ret != QM_EOK){
                goto __exit;
            }
        }
    }

__exit:
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "at exec fail");
        return ret;
    }
    return QM_EOK;
}

static int prodtst_dt_at_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object)
{
    cJSON *root = (cJSON*)json_object;

    prodtst_at_rsp_data_t *at_rsp_data = &dt_rsp_data->data.at_rsp_data;

    if(at_rsp_data->value){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, at_rsp_data->value);
    }else{
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, "");
    }
    if(at_rsp_data->reuslt){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_RESULT, at_rsp_data->reuslt);
    }
    return QM_EOK;
}

static int prodtst_dt_at_destroy(prodtst_dt_data_t *dt_data)
{
    if(dt_data->data.at_data.data){
        qm_free(dt_data->data.at_data.data);
        dt_data->data.at_data.data = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_at_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_at_rsp_data_t *at_rsp_data = &dt_rsp_data->data.at_rsp_data;

    if(at_rsp_data->value){
        qm_free(at_rsp_data->value);
        at_rsp_data->value = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data)
{
    int wait_time = 0;
    int ret = QM_EOK;
    cJSON *root = (cJSON*)json_object;
    if(root == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, PRODTST_JSON_KEY_WAIT_TIME, &wait_time);
    if(ret != QM_EOK){
        return ret;
    }
    dt_data->recv_rule.rule.rule_xjiang_uart.wait_time = wait_time;
    dt_data->recv_rule.timeout = wait_time;

    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_rule_pack(void *json_object, prodtst_dt_data_t *dt_data)
{
    cJSON *root = (cJSON*)json_object;
    if(root == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    cJSON_AddNumberToObject(root, PRODTST_JSON_KEY_WAIT_TIME, dt_data->recv_rule.rule.rule_xjiang_uart.wait_time);

    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_unpack(void *data, prodtst_dt_data_t *dt_data)
{
    qm_err_t ret = QM_EOK;
    prodtst_xjiang_uart_data_t *xjiang_uart_data = &dt_data->data.xjiang_uart_data;

    ret = hexstrs_to_hex(data, strlen(data), &xjiang_uart_data->data, &xjiang_uart_data->len);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    qm_err_t ret = QM_EOK;
    char *str = NULL;
    cJSON *root  = (cJSON*)data;
    prodtst_xjiang_uart_rsp_data_t *xjiang_uart_rsp_data = NULL;

    xjiang_uart_rsp_data = &dt_rsp_data->data.xjiang_uart_rsp_data;
    
    str = cjson_str_get(root, PRODTST_JSON_KEY_VALUE);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    xjiang_uart_rsp_data->value = (char*)malloc_and_copy(str, strlen(str));
    
    return QM_EOK;
    
__exit:
    prodtst_dt_xjiang_uart_rsp_destroy(dt_rsp_data);
    return ret;
}

static int xjiang_uart_cmd_get(uint8_t *data, uint16_t len, uint8_t *cmd)
{
    if(data == NULL || len == 0 || cmd == NULL){
        return -QM_EINVAL;
    }

    if(len < 7){
        return -QM_EINVAL;
    }
    *cmd = *(data + 3);
    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    int need_len = 0;
    int recv_len = 0;
    int pos = 0;
    uint8_t req_cmd = 0;
    uint8_t rsp_cmd = 0;
    uint32_t left_t = 0;
    uint16_t packet_len = 0;
    int state = PARSE_STATE_NONE;
    qm_err_t ret = QM_EOK;
    #define XJIANG_UART_RECV_LEN 128
    uint8_t *recv_data = NULL;
    qm_utils_time_t timer = {0};
    prodtst_addr_t *addr = &dt_rsp_data->addr;
    prodtst_recv_rule_t *recv_rule = &dt_data->recv_rule;
    prodtst_xjiang_uart_data_t *xjiang_uart_data = &dt_data->data.xjiang_uart_data;
    prodtst_xjiang_uart_rsp_data_t *xjiang_uart_rsp_data = &dt_rsp_data->data.xjiang_uart_rsp_data;

    prodtst_channel_ctrl(addr, PRODTST_CHANNEL_CMD_CLEAR);
    prodtst_channel_write(addr, xjiang_uart_data->data, xjiang_uart_data->len, NULL);

    ret = xjiang_uart_cmd_get(xjiang_uart_data->data, xjiang_uart_data->len, &req_cmd);
    if(ret != QM_EOK){
        goto __exit;
    }

    recv_data = (uint8_t*)qm_malloc(XJIANG_UART_RECV_LEN);
    if(recv_data == NULL){
        return -QM_ENOMEM;
    }

    ret = -QM_ETIMEOUT;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, recv_rule->timeout);

    while((left_t = qm_utils_time_left(&timer))  > 0){

        if(ret != QM_EOK){
            state = PARSE_STATE_NONE;
            need_len = 1;
            recv_len = 0;
            pos = 0;
            ret = QM_EOK;
        }
        recv_len = prodtst_channel_read(addr, recv_data + pos, need_len, left_t, NULL);
        if(recv_len <= 0){
            ret = -QM_ETIMEOUT;
            continue;
        }
        pos += recv_len;

        switch(state){

            case PARSE_STATE_NONE:

                if(recv_data[pos - 1] != FRAME_HEAD_PART){
                    QM_LOGE(LOG_TAG, "xjiang uart head error");
                    ret = -QM_EINVAL;
                    continue;
                }else{
                    state = PARSE_STATE_HEAD;
                    need_len = 1;
                }

            break;
            case PARSE_STATE_HEAD:
                if(need_len <= recv_len){
                    if(recv_data[pos - 1] != FRAME_HEAD_PART){
                        QM_LOGE(LOG_TAG, "xjiang uart head error");
                        ret = -QM_EINVAL;
                        continue;
                    }else{
                        state = PARSE_STATE_VER;
                        need_len = 1;
                    }
                }else{
                    need_len = need_len - recv_len;
                }

            break;
            case PARSE_STATE_VER:
                if(need_len <= recv_len){
                    if(recv_data[pos - 1] != PROTOCOL_VER){
                        QM_LOGE(LOG_TAG, "xjiang uart ver error");
                        ret = -QM_EINVAL;
                        continue;
                    }else{
                        state = PARSE_STATE_LEN;
                        need_len = 3;
                    }
                }else{
                    need_len = need_len - recv_len;
                }

            break;
            case PARSE_STATE_LEN:

                if(need_len <= recv_len){
                    packet_len = be16_to_cpu((recv_data[5] << 8) + (recv_data[4]));
                    if(packet_len > (XJIANG_UART_RECV_LEN - pos)){
                        QM_LOGE(LOG_TAG, "xjiang uart recv buffer over flow");
                        ret = -QM_EINVAL;
                        continue;
                    }else{
                        state = PARSE_STATE_DATA;
                        need_len = packet_len + 1;
                    }
                }else{
                    need_len = need_len - recv_len;
                }

            break;
            case PARSE_STATE_DATA:

                if(need_len <= recv_len){
                    QM_LOGD(LOG_TAG, "xjiang uart recv len %d", pos);
                    QM_HEX_LOGD(LOG_TAG, "xjiang uart recv data", recv_data, pos);
                    
                    xjiang_uart_cmd_get(recv_data, pos, &rsp_cmd);
                    if(rsp_cmd == req_cmd){
                        ret = QM_EOK;
                        goto __success;
                    }else{
                        ret = -QM_EINVAL;
                    }
                }else{
                    need_len = need_len - recv_len;
                }

            break;
            default:
                break;
        }    
    }

__success:
    if(ret != QM_EOK){
        ret = -QM_ETIMEOUT;
        goto __exit;
    }

    xjiang_uart_rsp_data->value = (char*)qm_malloc(2*pos + 1);
    if(xjiang_uart_rsp_data->value == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(xjiang_uart_rsp_data->value, 0 , 2*pos + 1);
    hex_to_strs(recv_data, pos, xjiang_uart_rsp_data->value);
    xjiang_uart_rsp_data->len = 2*pos;

    return ret;

__exit:
    if(recv_data){
        qm_free(xjiang_uart_rsp_data->value);
        xjiang_uart_rsp_data->value = NULL;
    }
    return ret;
}

static int prodtst_dt_xjiang_uart_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object)
{
    cJSON *root = (cJSON*)json_object;

    prodtst_xjiang_uart_rsp_data_t *xjiang_uart_rsp_data = &dt_rsp_data->data.xjiang_uart_rsp_data;

    if(xjiang_uart_rsp_data->value){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, xjiang_uart_rsp_data->value);
    }
    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_destroy(prodtst_dt_data_t *dt_data)
{
    if(dt_data->data.xjiang_uart_data.data){
        qm_free(dt_data->data.xjiang_uart_data.data);
        dt_data->data.xjiang_uart_data.data = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_xjiang_uart_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_xjiang_uart_rsp_data_t *xjiang_uart_rsp_data = &dt_rsp_data->data.xjiang_uart_rsp_data;

    if(xjiang_uart_rsp_data->value){
        qm_free(xjiang_uart_rsp_data->value);
        xjiang_uart_rsp_data->value = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_hex_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data)
{   
    char *str = NULL;
    int wait_time = 0;
    qm_err_t ret = QM_EOK;
    cJSON *root = (cJSON*)json_object;
    prodtst_recv_rule_hex_t *rule_hex = &dt_data->recv_rule.rule.rule_hex;
    if(root == NULL || dt_data == NULL){
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, PRODTST_JSON_KEY_WAIT_TIME, &wait_time);
    if(ret != QM_EOK){
        return ret;
    }
    rule_hex->wait_time = wait_time;
    dt_data->recv_rule.timeout = wait_time;

    str = cjson_str_get(root, PRODTST_JSON_KEY_HEAD);
    if(str != NULL && strlen(str) > 0){
        ret = hexstrs_to_hex(str, strlen(str), &rule_hex->head, &rule_hex->head_len);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_TAIL);
    if(str != NULL && strlen(str) > 0){
        ret = hexstrs_to_hex(str, strlen(str), &rule_hex->head, &rule_hex->head_len);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_LEN_RANGE);
    if(str != NULL && strlen(str) > 0){
        sscanf(str, "%d_%d", &rule_hex->len_range_start, &rule_hex->len_range_end);
    }

    cjson_int_get(root, PRODTST_JSON_KEY_LEN_POS, &rule_hex->len_pos);

    if(rule_hex->len_range_end - rule_hex->len_range_start == 2){
        str = cjson_str_get(root, PRODTST_JSON_KEY_BYTE_ORDER);
        if(str != NULL && strlen(str) > 0){
            if(0 == strcmp(str, PRODTST_BYTEORDER_BIG_ENDIAN)){
                rule_hex->byteorder = BYTE_ORDER_BIG_ENDIAN;
            }else if(0 == strcmp(str, PRODTST_BYTEORDER_LITTLE_ENDIAN)){
                rule_hex->byteorder = BYTE_ORDER_LITTLE_ENDIAN;
            }
        }else{
            ret = -QM_EINVAL;
            goto __exit;
        }
    }
    cjson_int_get(root, PRODTST_JSON_KEY_FIX_LEN, &rule_hex->fix_len);
    cjson_int_get(root, PRODTST_JSON_KEY_REMAIN_LEN, &rule_hex->remain_len);
    cjson_int_get(root, PRODTST_JSON_KEY_MATCH_POS, &rule_hex->match_pos);


    str = cjson_str_get(root, PRODTST_JSON_KEY_MATCH_HEX);
    if(str != NULL && strlen(str) > 0){
        ret = hexstrs_to_hex(str, strlen(str), &rule_hex->match_hex, &rule_hex->match_hex_len);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    return QM_EOK;

__exit:
    if(rule_hex->head){
        qm_free(rule_hex->head);
        rule_hex->head = NULL;
    }
    if(rule_hex->tail){
        qm_free(rule_hex->tail);
        rule_hex->tail = NULL;
    }
    if(rule_hex->match_hex){
        qm_free(rule_hex->match_hex);
        rule_hex->match_hex = NULL;
    }
    return ret;
}

static int prodtst_dt_hex_rule_pack(void *json_object, prodtst_dt_data_t *dt_data)
{
    return QM_EOK;
}

static int prodtst_dt_hex_unpack(void *data, prodtst_dt_data_t *dt_data)
{
    qm_err_t ret = QM_EOK;
    prodtst_hex_data_t *hex_data = &dt_data->data.hex_data;

    ret = hexstrs_to_hex(data, strlen(data), &hex_data->data, &hex_data->len);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

static int prodtst_dt_hex_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    qm_err_t ret = QM_EOK;
    char *str = NULL;
    cJSON *root  = (cJSON*)data;
    prodtst_hex_rsp_data_t *hex_rsp_data = &dt_rsp_data->data.hex_rsp_data;
    
    str = cjson_str_get(root, PRODTST_JSON_KEY_VALUE);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    hex_rsp_data->value = (char*)malloc_and_copy(str, strlen(str));
    
    return QM_EOK;
    
__exit:
    prodtst_dt_hex_rsp_destroy(dt_rsp_data);
    return ret;
}

static int prodtst_dt_hex_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    int need_len = 0;
    int recv_len = 0;
    int pos = 0;
    uint32_t left_t = 0;
    int state = PRODTST_HEX_PARSE_STATE_NONE;
    qm_err_t ret = QM_EOK;
    #define HEX_RECV_LEN 64
    uint8_t *recv_data = NULL;
    uint16_t unfix_len = 0;
    int head_pos = 0;
    int len_range = 0;
    prodtst_addr_t *addr = &dt_rsp_data->addr;
    qm_utils_time_t timer = {0};
    prodtst_recv_rule_hex_t *hex_rule = &dt_data->recv_rule.rule.rule_hex;
    prodtst_hex_data_t *hex_data = &dt_data->data.hex_data;
    prodtst_hex_rsp_data_t *hex_rsp_data = &dt_rsp_data->data.hex_rsp_data;

    prodtst_channel_ctrl(addr, PRODTST_CHANNEL_CMD_CLEAR);
    prodtst_channel_write(addr, hex_data->data, hex_data->len, NULL);

    recv_data = (uint8_t*)qm_malloc(HEX_RECV_LEN);
    if(recv_data == NULL){
        return -QM_ENOMEM;
    }

    ret = -QM_ETIMEOUT;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, hex_rule->wait_time);

    while((left_t = qm_utils_time_left(&timer))  > 0){

        switch(state){

            case PRODTST_HEX_PARSE_STATE_NONE:

                if(hex_rule->head == NULL || hex_rule->head_len == 0){
                    state = PRODTST_HEX_PARSE_STATE_LEN;
                    need_len = 0;
                }else{
                    head_pos = hex_rule->head_len;
                    state = PRODTST_HEX_PARSE_STATE_HEAD;
                    need_len = 1;
                }

            break;
            case PRODTST_HEX_PARSE_STATE_HEAD:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                if(recv_data[pos - 1] != hex_rule->head[head_pos]){
                    ret = -QM_EINVAL;
                    QM_LOGE(LOG_TAG, "hex head error");
                    continue;
                }else{
                    head_pos++;
                    if(head_pos < hex_rule->head_len){
                        state = PRODTST_HEX_PARSE_STATE_HEAD;
                        need_len = 1;
                    }else{
                        need_len = 0;
                        state = PRODTST_HEX_PARSE_STATE_LEN;
                    }
                }

            break;
            case PRODTST_HEX_PARSE_STATE_LEN:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                if(hex_rule->fix_len){
                    state = PRODTST_HEX_PARSE_STATE_FIX_LEN;
                }else{
                    state = PRODTST_HEX_PARSE_STATE_PRE_LEN;
                    if(hex_rule->len_range_start == 0 || hex_rule->len_range_end == 0){
                        ret = -QM_EINVAL;
                        QM_LOGE(LOG_TAG, "len range error");
                        goto __exit;
                    }
                    if(hex_rule->len_pos == 0){
                        ret = -QM_EINVAL;
                        QM_LOGE(LOG_TAG, "len pos error");
                        goto __exit;
                    }
                    need_len = hex_rule->len_range_start - pos;     
                } 
            break;
            case PRODTST_HEX_PARSE_STATE_PRE_LEN:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                need_len = hex_rule->len_range_end - hex_rule->len_range_start + 1;
                if(need_len >= 3){
                    ret = -QM_EINVAL;
                    goto __exit;
                }
                state = PRODTST_HEX_PARSE_STATE_UNFIX_LEN;

            break;
            case PRODTST_HEX_PARSE_STATE_UNFIX_LEN:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }
                if(hex_rule->byteorder == BYTE_ORDER_BIG_ENDIAN){
                    memcpy(&unfix_len, &recv_data[pos - 2], sizeof(uint16_t));
                    unfix_len = be16_to_cpu(unfix_len);
                    state = PRODTST_HEX_PARSE_STATE_DATA;
                }else if(hex_rule->byteorder == BYTE_ORDER_LITTLE_ENDIAN){
                    memcpy(&unfix_len, &recv_data[pos - 2], sizeof(uint16_t));
                    state = PRODTST_HEX_PARSE_STATE_DATA;
                }

                if(unfix_len < pos){
                    ret = -QM_EINVAL;
                    QM_LOGD(LOG_TAG, "hex len range error");
                    goto __exit;
                }

                len_range = hex_rule->len_range_end - hex_rule->len_range_start + 1;

                if(hex_rule->len_pos <= hex_rule->len_range_start){
                    need_len = unfix_len - (len_range + hex_rule->len_range_start - hex_rule->len_pos);
                }else if(hex_rule->len_pos >= hex_rule->len_range_end){
                    need_len = unfix_len + hex_rule->len_pos - hex_rule->len_range_end;
                }else{
                    ret = -QM_EINVAL;
                    QM_LOGD(LOG_TAG, "hex len pos error");
                    goto __exit;
                }

            break;
            case PRODTST_HEX_PARSE_STATE_FIX_LEN:

                if(hex_rule->fix_len < pos){
                    ret = -QM_EINVAL;
                    QM_LOGE(LOG_TAG, "hex fix len smaller than pos: %d", pos);
                    goto __exit;
                }else{
                    need_len = hex_rule->fix_len - pos;
                }
                state = PRODTST_HEX_PARSE_STATE_DATA;
    
            break;
            case PRODTST_HEX_PARSE_STATE_DATA:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                need_len = hex_rule->remain_len;
                state = PRODTST_HEX_PARSE_STATE_REMAIN_LEN;

            break;
            case PRODTST_HEX_PARSE_STATE_REMAIN_LEN:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                if(hex_rule->tail == NULL || hex_rule->tail_len == 0){
                    need_len = 0;
                    state = PRODTST_HEX_PARSE_STATE_MATCH;
                }else{
                    need_len = 0;
                    state = PRODTST_HEX_PARSE_STATE_TAIL;
                }

            break;

            case PRODTST_HEX_PARSE_STATE_TAIL:

                if(need_len > recv_len){
                    need_len -= recv_len;
                    break;
                }

                if(0 == memcmp(&recv_data[pos- hex_rule->tail_len - 1], &hex_rule->tail[0], hex_rule->tail_len)){
                    need_len = 0;
                    state = PRODTST_HEX_PARSE_STATE_MATCH;
                }else{
                    ret = -QM_EINVAL;
                    QM_LOGE(LOG_TAG, "hex tail error");
                }

            break;

            case PRODTST_HEX_PARSE_STATE_MATCH:

                if(0 == memcmp(&recv_data[hex_rule->match_pos - 1], &hex_rule->match_hex[0], hex_rule->match_hex_len)){
                    need_len = 0;
                    state = PRODTST_HEX_PARSE_STATE_COMPLETE;
                }else{
                    ret = -QM_EINVAL;
                    QM_LOGE(LOG_TAG, "hex match error");
                }

            break;

            case PRODTST_HEX_PARSE_STATE_COMPLETE:

                ret = hex_to_hexstrs(recv_data, pos, &hex_rsp_data->value, &hex_rsp_data->len);
                if(ret != QM_EOK){
                    goto __exit;
                }
                ret = QM_EOK;
                goto __exit;

            break;

            break;
            default:
                break;
        }    

        if(ret != QM_EOK){
            state = PRODTST_HEX_PARSE_STATE_NONE;
            need_len = 1;
            recv_len = 0;
            pos = 0;
            ret = QM_EOK;
        }
        if(need_len == 0){
            continue;
        }
        recv_len = prodtst_channel_read(addr, recv_data + pos, need_len, left_t, NULL);
        if(recv_len == 0){
            ret = -QM_ETIMEOUT;
            continue;
        }
        pos += recv_len;
    }

__exit:
    if(recv_data){
        qm_free(recv_data);
        recv_data = NULL;
    }
    return ret;
}

static int prodtst_dt_hex_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object)
{

    cJSON *root = (cJSON*)json_object;
    prodtst_hex_rsp_data_t *hex_rsp_data = &dt_rsp_data->data.hex_rsp_data;

    if(hex_rsp_data->value){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, hex_rsp_data->value);
    }
    return QM_EOK;

}

static int prodtst_dt_hex_destroy(prodtst_dt_data_t *dt_data)
{
    prodtst_hex_data_t *hex_data = &dt_data->data.hex_data;
    if(hex_data->data){
        qm_free(hex_data->data);
        hex_data->data = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_hex_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data)
{
    prodtst_hex_rsp_data_t *hex_rsp_data = &dt_rsp_data->data.hex_rsp_data;

    if(hex_rsp_data->value){
        qm_free(hex_rsp_data->value);
        hex_rsp_data->value = NULL;
    }
    return QM_EOK;
}

static int prodtst_dt_ble_mesh_at_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data)
{
    qm_err_t ret = QM_EOK;
    prodtst_addr_t *addr = &dt_rsp_data->addr;
    prodtst_base_info_t *base_info = (prodtst_base_info_t*)dt_data->base_info;
    prodtst_at_data_t *at_data = &dt_data->data.at_data;
    at_data->addr = &dt_rsp_data->addr;
    at_data->arg = dt_data->base_info;

    prodtst_channel_ctrl(addr, PRODTST_CHANNEL_CMD_CLEAR);

    if(!base_info->ack){
        QM_LOGD(LOG_TAG, "no ack");
        prodtst_channel_write(addr, dt_data->data.at_data.data, dt_data->data.at_data.len, dt_data->base_info);
        return QM_EOK;
    }

    QM_LOGD(LOG_TAG, "at ext exec start...");
    dt_data->recv_rule.rule.rule_at.recv_size = CONFIG_PRODTST_EXT_AT_BUF_MAX_SIZE;
    ret = prodtst_at_client_ext_exec_cmd(&dt_data->recv_rule.rule.rule_at, at_data, &dt_rsp_data->data.at_rsp_data);
    if(ret != QM_EOK){
        goto __exit;
    }

__exit:
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "at exec fail");
        return ret;
    }
    return QM_EOK;
}