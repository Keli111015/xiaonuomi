#include "iot_generic_hal.h"

#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#include "qm_log.h"
#include "app_main.h"
#include "miio_user_api.h"


#include "serial_common.h"
#include "serial_platform.h"
#include "generic_serial_iot.h"

#include "standard_auth/mible_standard_auth.h"

// #define QM_LOG_TAG "iot_hal"

#define MIBEACON_ADV_TIMEOUT            (30*60*1000)
#define MIBEACON_ADV_ON                 (0xFFFFFFFF)

extern void *qm_iot_handle;
qm_timer_t pair_tiemr = {0};
uint16_t mi_connect_handle = 0xffff;

static int iot_init(void *arg);
static void on_action_invoke(action_operation_t  *o);
static void on_property_set(property_operation_t *o);
static void on_property_get(property_operation_t *o);

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);

static int _miio_adapter_prop_report(property_operation_t *operation);
static int _miio_adapter_event_report(event_operation_t  *operation);

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);

static void on_property_set(property_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
#endif
    iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, (char *)o , sizeof(property_operation_t));
    o->code = 0;
        
}
static void on_property_get(property_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_property_get] siid %d, piid %d ", o->siid, o->piid);
#endif
    int out_len = 0;
    iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    o->code = 0;
}
static void on_action_invoke(action_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
#endif
    int out_len = 0;
    int ret = QM_EOK;
    ret = iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_METHOD; 
    }else{
        o->code = OPERATION_OK; 
    }
}


bool_t mi_pair_complete = true;

static void pair_timer_callback( qm_timer_t *timer, void *arg)
{
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
    qm_timer_free(&pair_tiemr);
}

void mi_schd_event_handler(schd_evt_t *p_event)
{
    MI_LOG_INFO("USER CUSTOM CALLBACK RECV EVT ID %d\n", p_event->id);
    switch (p_event->id)
    {
        case SCHD_EVT_OOB_REQUEST:
            MI_LOG_INFO("App selected IO cap is 0x%04X\n", p_event->data.IO_capability);
            switch (p_event->data.IO_capability)
            {
                case 0x0080:
//                    mi_schd_oob_rsp(qr_code, 16);
                    MI_LOG_INFO(MI_LOG_COLOR_GREEN "Please scan device QR code.\n");
                    break;

                default:
                    MI_LOG_ERROR("Selected IO cap is not supported.\n");
                    mible_gap_disconnect(0);
                }
        break;

        case SCHD_EVT_KEY_DEL_SUCC:
            // device has been reset, restart adv mibeacon contains IO cap.
            mi_pair_complete   = false;
            miio_ble_user_adv_init(0);
            miio_ble_user_adv_start(200);
            miio_ble_set_adv_timeout(MIBEACON_ADV_TIMEOUT);
        break;

    case SCHD_EVT_REG_SUCCESS:
        // device has been reg.
        MI_LOG_INFO("App selected IO cap is 0x%04X\n", p_event->data.IO_capability);

        mi_pair_complete   = true;
        miio_ble_user_adv_init(0);
        miio_ble_user_adv_start(1000);
        miio_ble_set_adv_timeout(MIBEACON_ADV_ON);
        
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_CONNECTING_ROUTER);

        break;

    case SCHD_EVT_KEY_FOUND:
        MI_LOG_INFO("-----------------------SCHD_EVT_KEY_FOUND--------------------------\n");
        miio_ble_user_adv_init(0);
        miio_ble_user_adv_start(1000);
        miio_ble_set_adv_timeout(MIBEACON_ADV_ON);

        mi_pair_complete   = true;

        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_CONNECTING_ROUTER);

        break;

    case SCHD_EVT_KEY_NOT_FOUND:
        MI_LOG_INFO("-----------------------SCHD_EVT_KEY_NOT_FOUND--------------------------\n");

        mi_pair_complete   = false;
        miio_ble_user_adv_init(0);
        miio_ble_user_adv_start(200);
        miio_ble_set_adv_timeout(MIBEACON_ADV_TIMEOUT);
        qm_timer_new(&pair_tiemr, pair_timer_callback, NULL, 30 * 60 * 1000, 0);
        qm_timer_start(&pair_tiemr);

        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_BLE_CONFIG);
        break;
    default:
        break;
    }
}

static void gap_event_handler(mible_gap_evt_t evt, mible_gap_evt_param_t* param)
{
    switch (evt) {
    case MIBLE_GAP_EVT_CONNECTED:
        MI_LOG_INFO("---------------------MIBLE_GAP_EVT_CONNECTED------%d-------------------\n", get_mi_authorization());
        mi_connect_handle = param->conn_handle;
        
        if(mi_pair_complete == true){
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
        }
        
        break;
    case MIBLE_GAP_EVT_DISCONNECT:
        MI_LOG_INFO("-----------------------MIBLE_GAP_EVT_DISCONNECT-------%d----------------\n", get_mi_authorization());
        mi_connect_handle = 0xFFFF;
        if(mi_pair_complete == true){
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
        }
        break;
    default:
        break;
    }
}

static int iot_init(void *arg)
{
    mible_gap_register(gap_event_handler);
    miio_gatt_spec_init(on_property_set, on_property_get, on_action_invoke, 256);
    return QM_EOK;
}


static int _miio_adapter_prop_report(property_operation_t *operation)
{
    if (operation == NULL || !mi_pair_complete){
        return -QM_EINVAL;
    }
#ifdef QM_LOG_TAG
    QM_LOGD(QM_LOG_TAG, "[_miio_adapter_prop_report] siid %d, piid %d  mi_connect_handle %04X", 
                operation->siid, operation->piid, mi_connect_handle);
#endif
    if(mi_connect_handle != 0xFFFF){
        miio_gatt_properties_changed(operation->siid, operation->piid, operation->value);
    }else{
        miio_ble_property_changed(operation->siid, operation->piid, operation->value, 0);
    }
    return QM_EOK;
}

static int _miio_adapter_event_report(event_operation_t  *operation)
{
    if (operation == NULL || !mi_pair_complete){
        return -QM_EINVAL;
    }

    if(mi_connect_handle != 0xFFFF){
        gatt_send_event_occurred(operation->siid, operation->eiid, operation->arguments);
    }else{
        miio_ble_event_occurred(operation->siid, operation->eiid, operation->arguments, 0);
    }

    return QM_EOK;
}
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
#ifdef SERIAL_LOG_TAG
    QM_LOGD(QM_LOG_TAG,"[_xj_to_miio_transformat] siid %d, piid %d type %d ", id_map_data.siid, id_map_data.xiid, id_map_data.type_id);
#endif
    
    qm_tlv_block_data_type_set(tlv_block, id_map_data.type_id);

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
            prop_value = property_value_new_char(int8_value);
        }
        break;
        case QM_DTTV_TYPE_UINT8:{
            uint8_t uint8_value;
            qm_tlv_unpack_uint8(tlv_block, &uint8_value);
            prop_value = property_value_new_uchar(uint8_value);

        }break;
        case QM_DTTV_TYPE_INT16:
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            prop_value = property_value_new_short(int16_value);

        }
        break;
        case QM_DTTV_TYPE_UINT16:{
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            prop_value = property_value_new_ushort(uint16_value);

        }break;
        case QM_DTTV_TYPE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            prop_value = property_value_new_long(int32_value);
                
        }
        break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            prop_value = property_value_new_ulong(uint32_value);
        }
        break;

        case QM_DTTV_TYPE_STRING:
        case QM_DTTV_TYPE_ARRAY:{
            int len          = 0;
            char buff[64] = {0};
            char *pdata   = NULL;

            qm_tlv_unpack_string(tlv_block, NULL, &len);
            if (len > 64){
              pdata = (char *)qm_malloc(len + 1);
              if(pdata == NULL){
                return -QM_ENOMEM;
              }
              qm_tlv_unpack_string(tlv_block, pdata, &len);
              prop_value = property_value_new_string(pdata);
              qm_free(pdata);
              pdata = NULL;
            }else{
                qm_tlv_unpack_string(tlv_block, buff, &len);
                prop_value = property_value_new_string(buff);
            }
        }break;
        case QM_DTTV_TYPE_STRING_ARRAY:{

        }break;

        case  QM_DTTV_TYPE_FLOAT_ONE_INT16:					
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            float temp = int16_value * 0.1; 
            prop_value = property_value_new_float(temp);

        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float temp = uint16_value * 0.1; 
            prop_value = property_value_new_float(temp);       
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_TWO_INT16:						
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            float temp = int16_value * 0.01; 
            prop_value = property_value_new_float(temp);
            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float temp = uint16_value * 0.01; 
            prop_value = property_value_new_float(temp);        
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float temp = int32_value  * 0.1; 
            prop_value = property_value_new_float(temp);            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float temp = uint32_value  * 0.1; 
            prop_value = property_value_new_float(temp);         
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float temp = int32_value * 0.01; 
            prop_value = property_value_new_float(temp);
                
        }
        break;	
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float temp = uint32_value * 0.01; 
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
            qm_tlv_pack_bool(tlv_block,  operation->value->data.boolean);
        break;
        case QM_DTTV_TYPE_STRING:{
            qm_tlv_pack_string(tlv_block, operation->value->data.string.value,operation->value->data.string.length);
        }break;
        case QM_DTTV_TYPE_STRING_ARRAY:{
            
        }break;
        case QM_DTTV_TYPE_INT8 : {
            qm_tlv_pack_int8(tlv_block,operation->value->data.number.charValue);   
        }break;
        case QM_DTTV_TYPE_UINT8 : {
            qm_tlv_pack_uint8(tlv_block, operation->value->data.number.ucharValue);   
        }break;
        case QM_DTTV_TYPE_INT16 : {
            qm_tlv_pack_int16(tlv_block, operation->value->data.number.shortValue);   
        }break;
        case QM_DTTV_TYPE_UINT16 : {
            qm_tlv_pack_uint16(tlv_block, operation->value->data.number.ushortValue);     
        }break;
        case QM_DTTV_TYPE_INT32 : {
            qm_tlv_pack_int32(tlv_block, operation->value->data.number.longValue);   
        }break;
        case QM_DTTV_TYPE_UINT32 : {
            qm_tlv_pack_uint32(tlv_block, operation->value->data.number.ulongValue);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT32:{
            int32_t temp = operation->value->data.number.floatValue * 10;
            qm_tlv_pack_int32(tlv_block, temp);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT16:{
            int16_t temp = operation->value->data.number.floatValue * 10;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:{
            uint32_t temp = operation->value->data.number.floatValue * 10;
            qm_tlv_pack_uint32(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:{
            uint16_t temp = operation->value->data.number.floatValue * 10;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;

        case QM_DTTV_TYPE_FLOAT_TWO_INT16:{
            int16_t temp = operation->value->data.number.floatValue * 100;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:{
            uint16_t temp = operation->value->data.number.floatValue * 100;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:{
            int32_t temp = operation->value->data.number.floatValue * 100;
            qm_tlv_pack_int32(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:{
            uint32_t temp = operation->value->data.number.floatValue * 100;
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
    #ifdef QM_LOG_TAG
        QM_LOGD(QM_LOG_TAG, "tlv_block tag %d", tlv_block->tag);
    #endif
        property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret != QM_EOK){
            break;
        }

        ret = _miio_adapter_prop_report(&operation);
        if (ret != QM_EOK){
             break;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;
}

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
        ret = _miio_adapter_event_report(&operation);
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
        
    #ifdef QM_LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_event] piid %d", arguments->arguments[count].piid);
    #endif
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
    iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    
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
    
    iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));

    tlv_block = qm_tlv_block_find(tlv_ctx, id_map_data.attr_id);

    ret = _xj_to_miio_transformat(map_handle, tlv_block, operation);

    if (ret != QM_EOK){
        return ret;
    }


    return QM_EOK;
}

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
    iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));

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


    return QM_EOK;
}


static int iot_reset(void *arg)
{
    SERIAL_LOG("iot_reset");
    mible_record_clear();
    user_record_clear();
    miio_system_restore();
    return QM_EOK;
}


static iot_generic_hal_t  generic_hal_mi_ble = {

    .init = iot_init,
    .report = iot_report,
    .set = iot_set,
    .get_req = iot_get_req,
    .get_rsp = iot_get_rsp,
    .event   = iot_event,
    .action_req = iot_action_req,
    .action_rsp = iot_action_rsp,
    .reset = iot_reset,
    .deinit = NULL,

};

int iot_generic_hal_mi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE, &generic_hal_mi_ble);
}