#include "iot_generic_hal.h"

#include "qm_log.h"
#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#include "qm_iot_hal_wifi.h"
#include "qm_utils_string.h"

void *qm_hal_wifi_iot_handle = NULL;

// #define LOG_TAG "log"
#define MAX_ARGUMENTS           5
#define MIIO_REPORT_MAX_SIZE    10


static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);

static int iot_init(void *arg);
static int iot_reset(void *arg);
static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);


static int iot_init(void *arg)
{
    qm_iot_hal_wifi_init();
    qm_hal_wifi_iot_handle = arg;
    
    return QM_EOK;
}

static int iot_reset(void *arg)
{
    return qm_iot_hal_wifi_reset();
}

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    uint8_t report_count = 0;
    property_operation_t operation[6] = {0};

    qm_iot_hal_wifi_handle_set(map_handle);

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){

        ret = _xj_to_miio_transformat(map_handle, tlv_block, (operation + report_count));
        if (ret != QM_EOK){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
            continue;
        }
        
        if(++report_count >= 6){
            report_count = 0;
            qm_iot_hal_wifi_adapter_prop_report(&operation, 6);
            memset(&operation, 0, sizeof(property_operation_t) * 6);
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[qm_iot_hal_wifi_adapter_prop_report] report_count %d ",report_count );
#endif
    qm_iot_hal_wifi_adapter_prop_report(&operation, report_count);

    return ret;
}

static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)    
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t *operation = NULL;  

    operation = (property_operation_t *)data;  

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        return -QM_ENOMEM;
    }

    ret = _miio_to_xj_transformat(map_handle, tlv_block, operation);
    if (ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return ret;
    }

    ret = qm_tlv_block_add(tlv_ctx, tlv_block); 
    
    return QM_EOK;
}

static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        return -QM_ENOMEM;
    }  
    property_operation_t *operation = NULL;
    operation = (property_operation_t *)data;

    iot_map_mi_t id_map_data = {0};

    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return ret;
    }
    tlv_block->tag  = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;

    qm_tlv_block_add(tlv_ctx, tlv_block); 

    return QM_EOK;

}
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;

    property_operation_t *operation = NULL;
    operation = (property_operation_t *)data;

    iot_map_mi_t id_map_data = {0};

    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;
    
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return ret;
    }

    tlv_block = qm_tlv_block_find(tlv_ctx, id_map_data.attr_id);

    return _xj_to_miio_transformat(map_handle, tlv_block, operation);
}

#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
static int iot_action_req(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    int in_len = 0;
    action_operation_t *operation = NULL;
    operation = (action_operation_t *)data;
    iot_map_mi_t id_map_data = {0};

    id_map_data.type = 1;
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->aiid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return ret;
    }

    *aid = id_map_data.attr_id;
    
    while(in_len < operation->in->size){

        qm_tlv_block_t *tlv_block = NULL;
        property_operation_t  prop_operation = {0};

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            return -QM_ENOMEM;
        }  

        prop_operation.siid  =  operation->siid;
        prop_operation.piid  =  operation->in->arguments[in_len].piid;
        prop_operation.value =  operation->in->arguments[in_len].value;

        in_len++;
        ret = _miio_to_xj_transformat(map_handle, tlv_block, &prop_operation);
        if (ret != QM_EOK){
            qm_tlv_block_delete(tlv_block);
            break;
        }
        
        qm_tlv_block_add(tlv_ctx, tlv_block); 
    }

    return QM_EOK;


}

static int iot_action_rsp(void *map_handle, uint8_t aid, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int out_len = 0;
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    action_operation_t *out_operation = NULL;
    out_operation = (action_operation_t *)data;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block && tlv_ctx->count){
        property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret < 0){
            break;
        }
        out_operation->out->size++;
        out_operation->out->arguments[out_len].piid  = operation.piid;
        out_operation->out->arguments[out_len].value = operation.value;    
        out_len++;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;
}
#endif

#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
static int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    arguments_t *arguments = NULL;
    event_operation_t operation = {0};

    iot_map_mi_t id_map_data = {0};

    id_map_data.attr_id = eid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return ret;
    }

    operation.eiid = id_map_data.xiid;
    operation.siid = id_map_data.siid;

    operation.arguments = arguments_new();
    if(operation.arguments == NULL){
        return -QM_ENOMEM;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    if(tlv_block == NULL ||  tlv_ctx->count == 0){
        ret = qm_iot_hal_wifi_adapter_event_report(&operation);
        return ret; 
    }

    arguments = operation.arguments;

    while(tlv_block && tlv_ctx->count){
        uint16_t count = arguments->size;
        property_operation_t operation_tmp = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation_tmp);
        if (ret != QM_EOK){
            break;
        }
        
        arguments->arguments[count].piid  = operation_tmp.piid;
        arguments->arguments[count].value = operation_tmp.value;
        
    #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_event] piid %d", arguments->arguments[count].piid);
    #endif
        if(arguments->size++ >= MAX_ARGUMENTS){
            ret = qm_iot_hal_wifi_adapter_event_report(&operation);
            if (ret != QM_EOK){
                return ret;
            }
            operation.arguments = arguments_new();
            if(operation.arguments == NULL){
                return -QM_ENOMEM;
            }
            arguments = operation.arguments;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    } 

    ret = qm_iot_hal_wifi_adapter_event_report(&operation);
  #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_event] ret %d", ret);
    #endif
    return ret;
}
#endif

static iot_generic_hal_t  generic_hal_mi_wifi= {

    .init = iot_init,
    .report = iot_report,
    .set = iot_set,
    .get_req = iot_get_req,
    .get_rsp = iot_get_rsp,
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
    .event   = iot_event,
#endif
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    .action_req = iot_action_req,
    .action_rsp = iot_action_rsp,
#endif
#if CONFIG_IOT_GENERIC_RAWDATA_SUPPORT
    .up_rawdata = NULL,
#endif
    .reset = iot_reset,
    .deinit = NULL,
};

int iot_generic_hal_mi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, &generic_hal_mi_wifi);
}


static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation)
{
    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};
    

    id_map_data.type = 0;
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[_miio_to_xj_transformat] siid %d, piid %d type %d ", id_map_data.siid, id_map_data.xiid, id_map_data.type_id);
#endif
    tlv_block->tag  = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;
    
    switch(id_map_data.type_id)
    {
        case QM_DTTV_TYPE_BOOL:
            qm_tlv_pack_bool(tlv_block,  operation->value->data.boolean.value);
        break;
        case QM_DTTV_TYPE_STRING:{
            qm_tlv_pack_string(tlv_block, operation->value->data.string.value,operation->value->data.string.length);
        }break;
        case QM_DTTV_TYPE_ARRAY:{
            uint16_t bytes_len = 0;
            uint8_t bytes[10] = {0};
            uint64_t bytes_tmp = operation->value->data.number.value.integerValue;
            memcpy(bytes, &bytes_tmp, sizeof(uint64_t));
            bytes_len = strlen(bytes) ;
            bytes_tmp = cpu_to_be64(bytes_tmp) >> ((sizeof(uint64_t) - bytes_len) * 8);
            memcpy(bytes, &bytes_tmp, sizeof(uint64_t));
            qm_tlv_pack_bytes(tlv_block, bytes, bytes_len);
        }break;
        case QM_DTTV_TYPE_STRING_ARRAY:{
            uint16_t bytes_len = 0;
            uint8_t *pdata = NULL;
            uint8_t bytes[64] = {0};
            bytes_len = operation->value->data.string.length / 2 + operation->value->data.string.length % 2;
            if (bytes_len > 64){
                pdata = (char *)qm_malloc(bytes_len);
                if(pdata == NULL){
                    return -QM_ENOMEM;
                }
                hex_str_to_nums(operation->value->data.string.value, 
                                operation->value->data.string.length, 
                                pdata);
                qm_tlv_pack_bytes(tlv_block, pdata, bytes_len);               
                qm_free(pdata);
                pdata = NULL;
            }else{
                hex_str_to_nums(operation->value->data.string.value, 
                                operation->value->data.string.length, 
                                bytes);
                qm_tlv_pack_bytes(tlv_block, bytes, bytes_len);                    
            }          
        }break;
        case QM_DTTV_TYPE_INT8 : {
            qm_tlv_pack_int8(tlv_block,operation->value->data.number.value.integerValue);   
        }break;
        case QM_DTTV_TYPE_UINT8 : {
            qm_tlv_pack_uint8(tlv_block, operation->value->data.number.value.integerValue);   
        }break;
        case QM_DTTV_TYPE_INT16 : {
            qm_tlv_pack_int16(tlv_block, operation->value->data.number.value.integerValue);   
        }break;
        case QM_DTTV_TYPE_UINT16 : {
            qm_tlv_pack_uint16(tlv_block, operation->value->data.number.value.integerValue);     
        }break;
        case QM_DTTV_TYPE_INT32 : {
            qm_tlv_pack_int32(tlv_block, operation->value->data.number.value.integerValue);   
        }break;
        case QM_DTTV_TYPE_UINT32 : {
            qm_tlv_pack_uint32(tlv_block, operation->value->data.number.value.integerValue);   
        }break;
        case QM_DTTV_TYPE_FLOAT32:{
            qm_tlv_pack_float32(tlv_block, operation->value->data.number.value.floatValue);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT32:{
            int32_t temp = operation->value->data.number.value.floatValue * 10;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 10;
            }
            qm_tlv_pack_int32(tlv_block, temp);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT16:{
            int16_t temp = operation->value->data.number.value.floatValue * 10;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 10;
            }
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:{
            uint32_t temp = operation->value->data.number.value.floatValue * 10;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 10;
            }

            qm_tlv_pack_uint32(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:{
            uint16_t temp = operation->value->data.number.value.floatValue * 10;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 10;
            }
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;

        case QM_DTTV_TYPE_FLOAT_TWO_INT16:{
            int16_t temp = operation->value->data.number.value.floatValue * 100;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 100;
            }
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:{
            uint16_t temp = operation->value->data.number.value.floatValue * 100;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 100;
            }
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:{
            int32_t temp = operation->value->data.number.value.floatValue * 100;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 100;
            }
            qm_tlv_pack_int32(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:{
            uint32_t temp = operation->value->data.number.value.floatValue * 100;
            if(temp == 0){
                temp = operation->value->data.number.value.integerValue * 100;
            }
            qm_tlv_pack_uint32(tlv_block, temp);
        }break;	   
    default:
        break;
    }
    return QM_EOK;
}

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation)
{
    
    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    iot_map_mi_t id_map_data = {0};
    property_value_t *prop_value = NULL;

    id_map_data.attr_id = tlv_block->tag;

    int ret = QM_EOK;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[_xj_to_miio_transformat] siid %d, piid %d type %d ", id_map_data.siid, id_map_data.xiid, id_map_data.type_id);
#endif
    switch(id_map_data.type_id)
    {
        case QM_DTTV_TYPE_BOOL:{
            bool_t bool_value;
            qm_tlv_unpack_bool(tlv_block, &bool_value);
            prop_value = property_value_new_boolean(bool_value);
        }break;
        case QM_DTTV_TYPE_INT8:{
            int8_t int8_value;
            qm_tlv_unpack_int8(tlv_block, &int8_value);
            prop_value = property_value_new_integer(int8_value);
        }
        break;
        case QM_DTTV_TYPE_UINT8:{
            uint8_t uint8_value;
            qm_tlv_unpack_uint8(tlv_block, &uint8_value);
            prop_value = property_value_new_integer(uint8_value);

        }break;
        case QM_DTTV_TYPE_INT16:
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            prop_value = property_value_new_integer(int16_value);

        }
        break;
        case QM_DTTV_TYPE_UINT16:{
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            prop_value = property_value_new_integer(uint16_value);

        }break;
        case QM_DTTV_TYPE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            prop_value = property_value_new_integer(int32_value);    
        }
        break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            prop_value = property_value_new_integer(uint32_value);
        }
        break;
        case QM_DTTV_TYPE_STRING:
        {
            int temp_len = 0;
            char buff[64] = {0};
            char *pdata   = NULL;

            qm_tlv_unpack_string(tlv_block, NULL, &temp_len);
            if (temp_len > 64){
                pdata = (char *)qm_malloc(temp_len + 1);
                if(pdata == NULL){
                    return -QM_ENOMEM;
                }
                memset(pdata, 0, temp_len + 1);
                qm_tlv_unpack_string(tlv_block, pdata, &temp_len);
                prop_value = property_value_new_string(pdata);
                qm_free(pdata);
                pdata = NULL;
            }else{
                qm_tlv_unpack_string(tlv_block, buff, &temp_len);
                prop_value = property_value_new_string(buff);
            }
        }break;
        case QM_DTTV_TYPE_ARRAY:
        {
            int temp_len = 0;
            char buff[8] = {0};
            uint64_t array_num = 0;

            qm_tlv_unpack_bytes(tlv_block, NULL, &temp_len);
            if (temp_len <= sizeof(uint64_t)){
                qm_tlv_unpack_bytes(tlv_block, buff, &temp_len);
                memcpy(&array_num, buff, temp_len);
                array_num = cpu_to_be64(array_num);
                array_num = array_num >> ((sizeof(uint64_t) - temp_len) * 8);
                prop_value = property_value_new_integer(array_num);
            }
        }break;
        case QM_DTTV_TYPE_STRING_ARRAY:{
            int temp_len = 0;
            char buff[64] = {0};
            char *pdata   = NULL;
            char *p_strdata   = NULL;
            qm_tlv_unpack_bytes(tlv_block, NULL, &temp_len);
            p_strdata = (char *)qm_malloc(temp_len * 2 + 1);
            if(p_strdata == NULL){
                return -QM_ENOMEM;
            }
            memset(p_strdata, 0, temp_len * 2 + 1);
            if (temp_len > 64){
                pdata = (char *)qm_malloc(temp_len);
                if(pdata == NULL){
                    qm_free(p_strdata);
                    p_strdata = NULL;
                    return -QM_ENOMEM;
                }
                qm_tlv_unpack_bytes(tlv_block, pdata, &temp_len);
                hex_to_strs(pdata, temp_len, p_strdata);  
                prop_value = property_value_new_string(p_strdata);
                qm_free(pdata);
                pdata = NULL;
            }else{
                qm_tlv_unpack_bytes(tlv_block, buff, &temp_len);
                hex_to_strs(buff, temp_len, p_strdata);  
                prop_value = property_value_new_string(p_strdata);
            }  

            qm_free(p_strdata);
            p_strdata = NULL;                  
        }break;
        case QM_DTTV_TYPE_FLOAT32:{
            float32_t float_value;
            qm_tlv_unpack_float32(tlv_block, &float_value);
            prop_value = property_value_new_float(float_value);
        }break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT16:					
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            float32_t temp = (float32_t )((float32_t)int16_value / 10.0f); 
            prop_value = property_value_new_float(temp);
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float32_t temp = (float32_t )((float32_t)uint16_value / 10.0f); 
            prop_value = property_value_new_float(temp);       
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_TWO_INT16:						
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            float32_t temp = (float32_t )((float32_t)int16_value / 100.0f);
            prop_value = property_value_new_float(temp);
            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float32_t temp = (float32_t )((float32_t)uint16_value / 100.0f);   
            prop_value = property_value_new_float(temp);        
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float32_t temp = (float32_t )((float32_t)int32_value / 10.0f);   
            prop_value = property_value_new_float(temp);            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float32_t temp = (float32_t )((float32_t)uint32_value / 10.0f);   
            prop_value = property_value_new_float(temp);         
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float32_t temp = (float32_t )((float32_t)int32_value / 100.0f);   
            prop_value = property_value_new_float(temp);
                
        }
        break;	
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float32_t temp = (float32_t )((float32_t)uint32_value / 100.0f);   
            prop_value = property_value_new_float(temp);  
        }
        break;
        default:
        break;

    }

    operation->siid  = id_map_data.siid;
    operation->piid  = id_map_data.xiid;
    operation->value = prop_value;

    return QM_EOK;
}

// app_attr_delay_report();