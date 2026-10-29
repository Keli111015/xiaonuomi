#include "iot_generic_hal.h"

#include "qm_log.h"
#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#include "qm_utils_string.h"

#include "util.h"
#include "virserial.h"
#include "miio_operation.h"

extern void *qm_iot_handle;

static int iot_init(void *arg);
static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);

static int _miio_adapter_prop_report(property_operation_t *operation);
static int _miio_adapter_event_report(event_operation_t  *operation);

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);

static int digits_judge(int num)
{
    int count = 1;
    while(num){
        count++;
        num /= 10;
    }
    return count;
}

void on_property_set(property_operation_t *o)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
#endif
    iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t));
    o->code = 0;
}
int on_property_get(property_operation_t *o)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_property_get] siid %d, piid %d ", o->siid, o->piid);
#endif
    int ret = QM_EOK;
    o->code = 0;
    int out_len = 0;
    ret = iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    if(ret != QM_EOK){
        return ret;
    }

    return out_len;
}
void on_action_invoke(action_operation_t *o)
{
    o->code = 0;
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
#endif
    int out_len = 0;
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
#endif
}

/* BRIEF :
 *     mcu may receive state string following : \n
 *     1. unprov        : module timed out while waiting for user configure. \n
 *     2. uap           : module is waitting for user configure. \n
 *     3. offline       : module is not connected with router. \n
 *     4. local         : module connected with router, but not connected with MI cloud. \n
 *     5. cloud         : module connected with MI cloud. \n
 *     6. updating      : module received normal app/mcu ota rpc, and module is downloading file. \n
 *     7. updating auto : module received auto app/mcu ota rpc, and module is downloading file. \n
 *     8. updating force: module recieved auto app/mcu ota rpc, module is downloading file, and mcu must be ota. \n
 * NOTICE :
 *     1. if mcu received "updating auto", we must ensure that the ota status will not be noticed by users. \n
 *     2. if mcu received "updating force", we should try our best to accept later ota commands, and fulfill condition above.
 */
typedef enum{
	UNPROV,
	UAP,
	OFFLINE,
	LOCAL,
	CLOUD,
    UPADTEING,
}_miio_net_status_t;

static const char *net_change_buff[] = {"unprov", "uap", "offline", "local", "cloud", "updating"};

int do_net_changed(void *handle, char *data, uint32_t len)
{
	int index = 0;
    static int is_uprov = UNPROV; 
	for(int i = 0; i < 5; i++){
		if(NULL != strstr(data, net_change_buff[i])){
			index = i;
            break;
		}
	}
    
	switch (index)
	{
        case UNPROV:
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
        break;		
        case UAP:
            if(is_uprov == CLOUD){
                is_uprov = UAP;
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
                break;
            }
            is_uprov = UAP;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_WIFI_CONFIG);
        break;
        case OFFLINE:
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
        break;		
        case LOCAL:
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTED_ROUTER);
        break;	
        case CLOUD:
            if(is_uprov == UAP){
                is_uprov = CLOUD;
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
                break;
            }
            is_uprov = CLOUD;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
        break;	
        case UPADTEING:
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_OTA_START);
        break;	
        default:
        break;
	}

	return QM_EOK;
}

static int iot_init(void *arg)
{
    return QM_EOK;
}


static int _miio_adapter_prop_report(property_operation_t *operation)
{
    if (operation == NULL || operation->value == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;
    char *data    = NULL;
    char buff[64 + 5] = {0}; // data +  "\r" + "\0"
    switch (operation->value->format)
    {
        case PROPERTY_FORMAT_BOOLEAN:
            if(operation->value->data.boolean.value == QM_FALSE){
                snprintf(buff, 64, "%s %d %d %s","properties_changed",operation->siid, operation->piid,"false");
            }else{
                snprintf(buff, 64, "%s %d %d %s","properties_changed",operation->siid, operation->piid,"true");
            }
        break;
        case PROPERTY_FORMAT_NUMBER:
            switch (operation->value->data.number.type)
            {
                case DATA_NUMBER_INTEGER:
                    snprintf(buff, 64, "%s %d %d %ld","properties_changed",operation->siid, operation->piid, operation->value->data.number.value.integerValue);
                break;
                case DATA_NUMBER_UINTEGER:
                    snprintf(buff, 64, "%s %d %d %lu","properties_changed",operation->siid, operation->piid, operation->value->data.number.value.integerValue);
                break;
                case DATA_NUMBER_FLOAT:
                    snprintf(buff, 64, "%s %d %d %f" ,"properties_changed",operation->siid, operation->piid, operation->value->data.number.value.floatValue);
                break;
                default:

                break;
            }          
        break;
        case PROPERTY_FORMAT_STRING :
            if(operation->value->data.string.length + 30 > 64){
                int len = operation->value->data.string.length + 30;
                data = (char *)qm_malloc(len + 5);
                if(data == NULL){
                    ret = -QM_ENOMEM;
                    goto _exit;
                }
                snprintf(data, len, "%s %d %d \"%s\"","properties_changed",operation->siid, operation->piid, operation->value->data.string.value);
            }else{
                snprintf(buff, 64, "%s %d %d \"%s\"","properties_changed",operation->siid, operation->piid, operation->value->data.string.value);
            }              
        break;
        default :
        break;
    }

    if (data){
        ret = virserial_properties_changed(data, strlen(data));
        qm_free(data);
        data = NULL;
    }else{
        ret = virserial_properties_changed(buff, strlen(buff));
    }

_exit:
    
    qm_free(operation->value);
    return ret;
}
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
static int _miio_adapter_event_report(event_operation_t  *operation)
{

    if (operation == NULL){
        return -QM_EINVAL;
    }

    char out[512] = {0};
	char piid_buf[8] = {0};

	memset(out, 0, 512);
	str_n_cat(out, 2, "event_occured", " ");

    do{
        char siid_buf[8] = {0};
        char eiid_buf[8] = {0};

        snprintf(siid_buf, 8, "%d", operation->siid);
        snprintf(eiid_buf, 8, "%d", operation->eiid);

        str_n_cat(out, 4, siid_buf, " ", eiid_buf, " ");      
    }while (0);

    if(operation->arguments == NULL){
        goto _exit;
    }

	for(int i = 0;i < operation->arguments->size;i++) {
		snprintf(piid_buf, 8, "%d", operation->arguments->arguments[i].piid);
		str_n_cat(out, 2, piid_buf, " ");
	
		switch(operation->arguments->arguments[i].value->format) {
				
			case PROPERTY_FORMAT_BOOLEAN : {
				if(operation->arguments->arguments[i].value->data.boolean.value == QM_FALSE) {
					str_n_cat(out, 2, "false", " ");
				} else {
					str_n_cat(out, 2, "true", " ");
				}
			}
			break;

			case PROPERTY_FORMAT_STRING : {
				str_n_cat(out, 2, operation->arguments->arguments[i].value->data.string.value, " ");
			}
			break;

			case PROPERTY_FORMAT_NUMBER : {
				if(operation->arguments->arguments[i].value->data.number.type == DATA_NUMBER_INTEGER) {
					char integer_buf[10] = {0};
					snprintf(integer_buf, 10, "%ld", operation->arguments->arguments[i].value->data.number.value.integerValue);
					str_n_cat(out, 2, integer_buf, " ");
				}else if(operation->arguments->arguments[i].value->data.number.type == DATA_NUMBER_UINTEGER) {
					char integer_buf[10] = {0};
					snprintf(integer_buf, 10, "%lu", operation->arguments->arguments[i].value->data.number.value.integerValue);
					str_n_cat(out, 2, integer_buf, " ");
				}else if (operation->arguments->arguments[i].value->data.number.type == DATA_NUMBER_FLOAT) {
					char float_buf[10] = {0};
					snprintf(float_buf, 10, "%f", operation->arguments->arguments[i].value->data.number.value.floatValue);
					str_n_cat(out, 2, float_buf, " ");
				}
			}
			break;
			
			default:
                return -QM_EINVAL;
			break;
		}

        qm_free(operation->arguments->arguments[i].value);
        operation->arguments->arguments[i].value = NULL;
	}

_exit:
    if(operation->arguments){
        qm_free(operation->arguments);
        operation->arguments = NULL;
    }
    virserial_properties_changed(out, strlen(out));
    return QM_EOK;
}
#endif


static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation)
{
    int len = 0;
    qm_err_t ret = QM_EOK;
    
    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    iot_map_mi_t id_map_data = {0};
    property_value_t *prop_value = NULL;

    id_map_data.attr_id = tlv_block->tag;

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
            len = 6;
            qm_tlv_unpack_bool(tlv_block, &bool_value);
            prop_value = property_value_new_boolean(bool_value);
        }break;
        case QM_DTTV_TYPE_INT8:{
            int8_t int8_value;
            qm_tlv_unpack_int8(tlv_block, &int8_value);
            len = digits_judge(int8_value);
            prop_value = property_value_new_integer(int8_value);
        }
        break;
        case QM_DTTV_TYPE_UINT8:{
            uint8_t uint8_value;
            qm_tlv_unpack_uint8(tlv_block, &uint8_value);
            len = digits_judge(uint8_value);
            prop_value = property_value_new_uinteger(uint8_value);

        }break;
        case QM_DTTV_TYPE_INT16:
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            len = digits_judge(int16_value);
            prop_value = property_value_new_integer(int16_value);

        }
        break;
        case QM_DTTV_TYPE_UINT16:{
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            len = digits_judge(uint16_value);
            prop_value = property_value_new_uinteger(uint16_value);

        }break;
        case QM_DTTV_TYPE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            len = digits_judge(int32_value);
            prop_value = property_value_new_integer(int32_value);
                
        }
        break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            len = digits_judge(uint32_value);
            prop_value = property_value_new_uinteger(uint32_value);
    
        }
        break;

        case QM_DTTV_TYPE_STRING:{
            int temp_len = 0;
            char buff[64] = {0};
            uint8_t *pdata   = NULL;

            qm_tlv_unpack_bytes(tlv_block, NULL, &temp_len);
            if (temp_len >= 64){
              pdata = (uint8_t *)qm_malloc(temp_len + 1);
              if(pdata == NULL){
                return -QM_ENOMEM;
              }
              qm_tlv_unpack_bytes(tlv_block, pdata, &temp_len);
              prop_value = property_value_new_string((char *)pdata);
              qm_free(pdata);
              pdata = NULL;
            }else{
                qm_tlv_unpack_bytes(tlv_block, (uint8_t *)buff, &temp_len);
                prop_value = property_value_new_string(buff);
            }
            len = temp_len + 5;
        }break;
        //note:修复字符串数组解析内存资源问题
        case QM_DTTV_TYPE_STRING_ARRAY:{
            int temp_len = 0;
            uint8_t *pdata   = NULL;
            uint8_t *p_strdata   = NULL;
            qm_tlv_unpack_bytes_direct(tlv_block, &pdata, &temp_len);
            p_strdata = (uint8_t *)qm_malloc(temp_len * 2 + 1);
            if(p_strdata == NULL){
                return -QM_ENOMEM;
            }
            memset(p_strdata, 0, temp_len * 2 + 1);
            hex_to_strs(pdata, temp_len, (char *)p_strdata);  
            prop_value = property_value_new_string((char *)p_strdata);

            qm_free(p_strdata);
            p_strdata = NULL;     
            len = (temp_len * 2) + 5;
        }break;
        case QM_DTTV_TYPE_FLOAT32:{
            float32_t float_value;
            qm_tlv_unpack_float32(tlv_block, &float_value);
            len = digits_judge(float_value) + 20;
            prop_value = property_value_new_float(float_value);
        }break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT16:					
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            double temp = int16_value * 0.1; 
            len = digits_judge(int16_value) + 20;

            prop_value = property_value_new_float(temp);
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            double temp = uint16_value * 0.1; 
            len = digits_judge(uint16_value) + 20;

            prop_value = property_value_new_float(temp);       
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_TWO_INT16:						
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            double temp = int16_value * 0.01; 
            len = digits_judge(int16_value) + 20;
            
            prop_value = property_value_new_float(temp);
            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            double temp = uint16_value * 0.01; 
            len = digits_judge(uint16_value) + 20;
            
            prop_value = property_value_new_float(temp);        
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            double temp = int32_value  * 0.1; 
            len = digits_judge(int32_value) + 20;
            
            prop_value = property_value_new_float(temp);            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            double temp = uint32_value  * 0.1; 
            len = digits_judge(uint32_value) + 20;
            
            prop_value = property_value_new_float(temp);         
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            double temp = int32_value * 0.01; 
            len = digits_judge(int32_value) + 20;

            prop_value = property_value_new_float(temp);
                
        }
        break;	
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            double temp = uint32_value * 0.01; 
            len = digits_judge(uint32_value) + 20;
            prop_value = property_value_new_float(temp);  
        }
        break;
        default:
        break;

    }

    operation->siid  = id_map_data.siid;
    operation->piid  = id_map_data.xiid;
    operation->value = prop_value;

    return len;
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
        case QM_DTTV_TYPE_STRING_ARRAY:{
            uint16_t bytes_len = 0;
            uint8_t *pdata = NULL;
            uint8_t bytes[64] = {0};
            bytes_len = operation->value->data.string.length / 2 + operation->value->data.string.length % 2;
            if (bytes_len > 64){
                pdata = (uint8_t *)qm_malloc(bytes_len);
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
            qm_tlv_pack_int32(tlv_block, temp);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT16:{
            int16_t temp = operation->value->data.number.value.floatValue * 10;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:{
            uint32_t temp = operation->value->data.number.value.floatValue * 10;
            qm_tlv_pack_uint32(tlv_block, temp);
        }
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:{
            uint16_t temp = operation->value->data.number.value.floatValue * 10;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;

        case QM_DTTV_TYPE_FLOAT_TWO_INT16:{
            int16_t temp = operation->value->data.number.value.floatValue * 100;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:{
            uint16_t temp = operation->value->data.number.value.floatValue * 100;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:{
            int32_t temp = operation->value->data.number.value.floatValue * 100;
            qm_tlv_pack_int32(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:{
            uint32_t temp = operation->value->data.number.value.floatValue * 100;
            qm_tlv_pack_uint32(tlv_block, temp);
        }break;	   
    default:
        break;
    }
    return QM_EOK;
}



static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret < 0){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
            continue;
        }

        ret = _miio_adapter_prop_report(&operation);
        if (ret != QM_EOK){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
            continue;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
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
        return ret;
    }

    qm_tlv_block_add(tlv_ctx, tlv_block); 
    
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
        operation->code = -4001;
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
    if(tlv_block == NULL){
        operation->code = -4001;
        return -QM_EINVAL;
    }

    ret = _xj_to_miio_transformat(map_handle, tlv_block, operation);

    if(ret > 0){
        *len += ret;
    }
    
    return ret;
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
int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
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
        ret = _miio_adapter_event_report(&operation);
        return ret; 
    }

    arguments = operation.arguments;

    while(tlv_block && tlv_ctx->count){
        uint16_t count = arguments->size;
        property_operation_t operation_tmp = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation_tmp);
        if (ret < 0){
            break;
        }
        
        arguments->arguments[count].piid  = operation_tmp.piid;
        arguments->arguments[count].value = operation_tmp.value;
        
        if(arguments->size++ >= MAX_ARGUMENTS){
            ret = _miio_adapter_event_report(&operation);
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

    return _miio_adapter_event_report(&operation);
}
#endif

static int iot_reset(void *arg)
{
    virserial_send_net_config();
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
    return QM_EOK;
}


static iot_generic_hal_t  generic_hal_mi_mesh = {

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
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, &generic_hal_mi_mesh);
}
