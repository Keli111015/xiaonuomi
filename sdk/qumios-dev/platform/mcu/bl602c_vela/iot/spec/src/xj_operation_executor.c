/**
 * Copyright (C) 2013-2015
 *
 *
 * @file   operation_executor.c
 *
 * @remark
 *
 */
#include "qm.h"
#include "qm_tlv.h"
#include "qm_utils_string.h"
#include "xj_operation_typedef.h"
#include "xj_operation_executor.h"

#include "jsmi.h"
#include "util.h"
#include "arch_chip.h"
#include "miio_api.h"
#include "qm_utils_list.h"
#include "qm_work.h"
#include "qm_utils_timer.h"

#define TAG "executor"

#define MIIO_PROPERTY_PARAMS    "{\"did\":%s,\"siid\":%d,\"piid\":%d,\"value\":%s}"

#define MIIO_PROPERTY_REQ   "{\"id\":%d,\"method\":\"%s\",\"params\":%s}"

#define MIIO_PROPERTY_RSP  "{\"id\":%d,\"result\":%s}"

#define MIIO_PROPERTY_CHANGED_METHOD    "properties_changed"

#define MIIO_ID_DEFAULT_LEN         (10)
#define MIIO_DID_DEFAULT_LEN        (128)
#define MIIO_SIID_DEFAULT_LEN       (10)
#define MIIO_XIID_DEFAULT_LEN       (10)
#define MIIO_DATA_DEFAULT_LEN       (10)


#define MIIO_RERORT_PROP_RETRY_COUNT    (3)
#define MIIO_RERORT_TIMEROUT_MS         (5 * 100)
#define MIIO_RERORT_PROP_TIMEROUT_MS    (2 * 1000)

#define MIIO_RERORT_PROP_MUTEX_TIMEOUT  (10 * 1000)

void miio_did_ext_get(char miio_did[MIIO_DID_DEFAULT_LEN + 1])
{
    uint64_t did = 0;
    arch_get_did(&did);
    snprintf(&miio_did[0], MIIO_DID_DEFAULT_LEN, "%lld" , did);
}


/**
 * request
 * ------------------------------------------------
 * {
 *     "id": 123,
 *     "method": "properties_changed",
 *     "params": [
 *         {
 *            "did" : "1234",
 *            "siid" : 1,
 *            "piid": 1,
 *            "value": 17
 *         },
 *         {
 *            "did" : "1234",
 *            "siid" : 3,
 *            "piid": 2,
 *            "value": 11.3
 *         }
 *    ]
 * }
 */

static uint32_t operation_changed_param_len_get(void)
{
    uint32_t data_len = 0;
    data_len += MIIO_ID_DEFAULT_LEN;
    data_len += strlen(MIIO_PROPERTY_REQ);
    data_len += strlen(MIIO_PROPERTY_CHANGED_METHOD);

    return data_len;
}

static uint32_t operation_changed_data_len_get(char *did, qm_tlv_ctx_t *tlv_ctx)
{
    uint32_t data_len = 0;
    qm_tlv_block_t *tlv_block = NULL;
    xj_property_operation_t *operation = NULL;

    if(tlv_ctx == NULL || did == NULL){
        return 0;
    }
    
    while(1){

        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
        if(tlv_block == NULL){
            break;
        }

        operation = (xj_property_operation_t *)tlv_block->arg;
        if(operation == NULL){
            continue;
        }

        data_len += strlen(MIIO_PROPERTY_PARAMS) + 1; // " " + ","

        data_len += tlv_block->len;
        if((tlv_block->type & 0x7f) == QM_DTTV_TYPE_STRING_ARRAY){
            data_len += (tlv_block->len * 2) + 1;
        }
        data_len += strlen(did);
        data_len += MIIO_SIID_DEFAULT_LEN;
        data_len += MIIO_XIID_DEFAULT_LEN;
    }
    
    return data_len;
}

typedef struct 
{
    miio_handle_t handle;
    qm_mutex_t mutex;
    qm_list_t list;
    qm_timer_t timer;
}xj_prop_report_t;

typedef struct 
{
    uint32_t id;
    uint32_t count;
    uint32_t len;
    qm_utils_time_t timeout;
    uint8_t payload[1];
}xj_prop_report_node_t;

static xj_prop_report_t g_prop_report = {0};

static int report_change_ack(miio_rpc_delegate_arg_t* arg, void* ctx);

static int report_change_ack(miio_rpc_delegate_arg_t* arg, void* ctx)
{
    int v_id = 0;
    qm_list_node_t *node = NULL;
    xj_prop_report_node_t *report = NULL;
    jsmi_parser_t jsmi_parser = JSMI_PARSER_INIT(arg->pload, arg->pload_len);
    qm_list_iterator_t self = {
        .direction = LIST_HEAD,
        .next = g_prop_report.list.head,
    };
    
#if 0
    if(QM_EOK == jsmi_parse_start(&jsmi_parser)){
        int v_int = 0;
        jsmi_tok_path_t path[] = {
            {
                .key = "error",
                .type = JSMN_OBJECT
            },
            {
                .key = "code",
                .type = JSMN_PRIMITIVE
            }
        };

        if(MIIO_OK == jsmi_get_value_sint(&jsmi_parser, NULL, path, NELEMENTS(path), &v_int)){
            code = v_int;   
            if(v_int != QM_EOK){
            }
        } 
        jsmi_parse_finish(&jsmi_parser);
    }
#endif
    
    if(QM_EOK == jsmi_parse_start(&jsmi_parser)){
        jsmi_tok_path_t path[] = {
            {
                .key = "id",
                .type = JSMN_PRIMITIVE
            },
        };

        if(MIIO_OK == jsmi_get_value_sint(&jsmi_parser, NULL, path, NELEMENTS(path), &v_id)){
            
        } 
        jsmi_parse_finish(&jsmi_parser);
    }

    QM_LOGD("1", "[report_change_ack] [%d]paylod : %s", v_id, arg->pload);

    if(v_id == 0){
        return QM_EOK;
    }

    qm_mutex_lock(&g_prop_report.mutex, MIIO_RERORT_PROP_MUTEX_TIMEOUT);
    node = qm_list_iterator_next(&self);
    if(node == NULL){
        qm_mutex_unlock(&g_prop_report.mutex);
        return QM_EOK;
    }
    report  = (xj_prop_report_node_t *)qm_list_node_val_get(node);
    
    if(v_id == report->id){
        qm_list_remove(&g_prop_report.list, node);
        qm_list_node_destroy(node);
    }
    qm_mutex_unlock(&g_prop_report.mutex);

    qm_timer_change(&g_prop_report.timer, MIIO_RERORT_TIMEROUT_MS);
    
    return QM_EOK;
}

static void report_timeout_cb(qm_timer_t *timer, void *arg)
{
    int len = 0;
    miio_rpc_delegate_context_t context = {0};
    qm_list_iterator_t self = {
        .direction = LIST_HEAD,
        .next = g_prop_report.list.head,
    };
    qm_list_node_t *node = NULL;
    xj_prop_report_node_t *report = NULL;

    qm_mutex_lock(&g_prop_report.mutex, MIIO_RERORT_PROP_MUTEX_TIMEOUT);

    len = qm_list_len_get(&g_prop_report.list);
    if(!len){
        goto __next;
    }

    while (1)
    {
        node = qm_list_iterator_next(&self);
        if(node == NULL){
            break;
        }

        report  = (xj_prop_report_node_t *)qm_list_node_val_get(node);

        if(!qm_utils_time_left(&report->timeout) || 
            report->count >= MIIO_RERORT_PROP_RETRY_COUNT){
            qm_list_remove(&g_prop_report.list, node);
            qm_list_node_destroy(node);
        }

        report->count++;
    }

    self.direction = LIST_HEAD;
    self.next = g_prop_report.list.head;

    node = qm_list_iterator_next(&self);
    if(node == NULL){
        goto __next;
    }
    report  = (xj_prop_report_node_t *)qm_list_node_val_get(node);

    miio_rpc_context_init(&context);
    miio_rpc_context_config_delegate_arg(&context, report->id, MIIO_DELEGATE_JSON, report->payload, report->len);
    miio_rpc_context_config_delegate_ack(&context, report_change_ack, NULL);
    miio_set_up_rpc_delegate(g_prop_report.handle, &context);

__next:

    qm_mutex_unlock(&g_prop_report.mutex);

}

int xj_send_property_changed(miio_handle_t handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = 0;
    int index = 0;
    static int is_init = 1;
    uint32_t param_len = 0;
    uint32_t data_len = 0;
    size_t jsmi_length = 0;
    qm_tlv_block_t *tlv_block = NULL;
    uint32_t id = miio_get_rpc_id(handle);
    xj_property_operation_t *operation = NULL;
	miio_rpc_delegate_context_t context = {0};
    char miio_did[MIIO_DID_DEFAULT_LEN + 1] = {0};

    qm_list_node_t *node = NULL;
    xj_prop_report_node_t *report = NULL;

    if(handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    
    if(is_init){
        is_init = 0;
        g_prop_report.handle = handle;
        qm_mutex_new(&g_prop_report.mutex);
        qm_timer_new(&g_prop_report.timer, report_timeout_cb, NULL, MIIO_RERORT_TIMEROUT_MS, 1);
        qm_timer_start(&g_prop_report.timer);
    }

    miio_did_ext_get(miio_did);
    param_len = operation_changed_param_len_get();
    if(param_len == 0){
        return -QM_EINVAL;
    }

    data_len = operation_changed_data_len_get(miio_did, tlv_ctx);
    if(data_len == 0){
        return -QM_EINVAL;
    }

    node = qm_list_node_extra_new(sizeof(xj_prop_report_node_t) + param_len + data_len + 1);
    if(node == NULL){
        goto __next;
    }
    report  = (xj_prop_report_node_t *)qm_list_node_val_get(node);

    jsmi_composer_t composer = JSMI_COMPOSER_INIT(report->payload + param_len, data_len + 1);
    

    jsmi_compose_start(&composer);
    jsmi_set_array_begin(&composer);

    while (1)
    {
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
        if(tlv_block == NULL){
            break;
        }

        operation = (xj_property_operation_t *)tlv_block->arg;
        if(operation == NULL){
            continue;
        }

        jsmi_set_value_object_begin(&composer);

        jsmi_set_key(&composer, "did");
        jsmi_set_value_str(&composer, miio_did, 0);

        jsmi_set_key(&composer, "siid");
        jsmi_set_value_uint(&composer, operation->siid);

        jsmi_set_key(&composer, "piid");
        jsmi_set_value_uint(&composer, operation->piid);

        jsmi_set_key(&composer, "value");

        switch (tlv_block->type)
        {
            case QM_DTTV_TYPE_BOOL:
            {
                bool_t bool_value;
                qm_tlv_unpack_bool(tlv_block, &bool_value);
                jsmi_set_value_bool(&composer, bool_value);
            }
            break;

            case QM_DTTV_TYPE_STRING:{
                int string_len = 0;
                char *string = NULL;
                qm_tlv_unpack_string_direct(tlv_block, &string, &string_len);
                jsmi_set_value_str(&composer, string, string_len);   
            }break;

            case QM_DTTV_TYPE_STRING_ARRAY:{
                int temp_len = 0;
                char *p_strbuf = NULL;
                uint8_t *p_strdata = NULL;
                qm_tlv_unpack_bytes_direct(tlv_block, &p_strdata, &temp_len);
                p_strbuf = (char *)qm_malloc(temp_len * 2 + 1);
                if(p_strbuf == NULL){
                    return -QM_ENOMEM;
                }
                memset(p_strbuf, 0, temp_len * 2 + 1);

                hex_to_strs(p_strdata, temp_len, p_strbuf);  
                jsmi_set_value_str(&composer, p_strbuf, temp_len * 2);

                qm_free(p_strbuf);
            }break;
            case QM_DTTV_TYPE_INT8 : {
                int8_t int8_value;
                qm_tlv_unpack_int8(tlv_block, &int8_value);
                jsmi_set_value_sint(&composer, int8_value);
            }break;
            case QM_DTTV_TYPE_UINT8 : {
                uint8_t uint8_value;
                qm_tlv_unpack_uint8(tlv_block, &uint8_value);
                jsmi_set_value_uint(&composer, uint8_value);
            }break;
            case QM_DTTV_TYPE_INT16 : {
                int16_t int16_value;
                qm_tlv_unpack_int16(tlv_block, &int16_value);
                jsmi_set_value_sint(&composer, int16_value);
            }break;
            case QM_DTTV_TYPE_UINT16 : {
                uint16_t uint16_value;
                qm_tlv_unpack_uint16(tlv_block, &uint16_value);
                jsmi_set_value_uint(&composer, uint16_value);
            }break;
            case QM_DTTV_TYPE_INT32 : {
                int32_t int32_value;
                qm_tlv_unpack_int32(tlv_block, &int32_value);
                jsmi_set_value_sint(&composer, int32_value);

            }break;
            case QM_DTTV_TYPE_UINT32 : {
                uint32_t uint32_value;
                qm_tlv_unpack_uint32(tlv_block, &uint32_value);
                jsmi_set_value_uint(&composer, uint32_value);
            }break;
            case QM_DTTV_TYPE_FLOAT_ONE_INT32:{
                int32_t int32_value;
                qm_tlv_unpack_int32(tlv_block, &int32_value);
                double temp = (double )((double)int32_value / 10.0f); 
                jsmi_set_value_double(&composer, temp, 4);
            }break;
            case QM_DTTV_TYPE_FLOAT_ONE_INT16:{
                int16_t int16_value;
                qm_tlv_unpack_int16(tlv_block, &int16_value);
                double temp = (double )((double)int16_value / 10.0f);
                jsmi_set_value_double(&composer, temp, 4); 
            }break;
            case QM_DTTV_TYPE_FLOAT_ONE_UINT32:{
                uint32_t uint32_value;
                qm_tlv_unpack_uint32(tlv_block, &uint32_value);
                double temp = (double )((double)uint32_value / 10.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;
            case QM_DTTV_TYPE_FLOAT_ONE_UINT16:{
                uint16_t uint16_value;
                qm_tlv_unpack_uint16(tlv_block, &uint16_value);
                double temp = (double )((double)uint16_value / 10.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;

            case QM_DTTV_TYPE_FLOAT_TWO_INT16:{
                int16_t int16_value;
                qm_tlv_unpack_int16(tlv_block, &int16_value);
                double temp = (double )((double)int16_value / 100.0f);
                jsmi_set_value_double(&composer, temp, 4); 
            }break;
            case QM_DTTV_TYPE_FLOAT_TWO_UINT16:{
                uint16_t uint16_value;
                qm_tlv_unpack_uint16(tlv_block, &uint16_value);
                double temp = (double )((double)uint16_value / 100.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;  
            case QM_DTTV_TYPE_FLOAT_TWO_INT32:{
                int32_t int32_value;
                qm_tlv_unpack_int32(tlv_block, &int32_value);
                double temp = (double )((double)int32_value / 100.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;  
            case QM_DTTV_TYPE_FLOAT_TWO_UINT32:{
                uint32_t uint32_value;
                qm_tlv_unpack_uint32(tlv_block, &uint32_value);
                double temp = (double )((double)uint32_value / 100.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;	  
            case QM_DTTV_TYPE_FLOAT32:{
                float32_t float32_value;
                qm_tlv_unpack_float32(tlv_block, &float32_value);
                double temp = (double )((double)float32_value * 1.0f);
                jsmi_set_value_double(&composer, temp, 4);
            }break;	 
            default:
            break;
        }
        
        jsmi_set_value_object_end(&composer); 
    }

    jsmi_set_array_end(&composer);
    jsmi_compose_finish(&composer, NULL, &jsmi_length);


    qm_snprintf(report->payload, param_len + data_len, MIIO_PROPERTY_REQ, id, MIIO_PROPERTY_CHANGED_METHOD, (report->payload + param_len));

    report->id = id;
    report->len = strlen(report->payload);
    qm_utils_time_countdown_ms(&report->timeout, MIIO_RERORT_PROP_TIMEROUT_MS);

    qm_mutex_lock(&g_prop_report.mutex, MIIO_RERORT_PROP_MUTEX_TIMEOUT);

    int len = qm_list_len_get(&g_prop_report.list);
    
    qm_list_rpush(&g_prop_report.list, node);

    qm_mutex_unlock(&g_prop_report.mutex);

    if(!len){
        
        QM_LOGD("1", "updata1: %s", report->payload);

        miio_rpc_context_init(&context);
        miio_rpc_context_config_delegate_arg(&context, id, MIIO_DELEGATE_JSON, report->payload, report->len);
        miio_rpc_context_config_delegate_ack(&context, report_change_ack, NULL);
        miio_set_up_rpc_delegate(handle, &context);
        
    }

    qm_timer_change(&g_prop_report.timer, MIIO_RERORT_TIMEROUT_MS);
__next :

    return ret;
}
