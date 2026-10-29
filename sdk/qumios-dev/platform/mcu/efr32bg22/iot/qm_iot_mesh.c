#include "iot_generic_hal.h"
#include "iot_generic.h"

#include "id_map.h"
#include "qm.h"
#include "qm_tlv.h"
#include "qm_utils_timer.h"
#include "miio_user_api.h"
#include "mible_mesh_api.h"

#include "event_id.h"
#include "mible_log.h"
#include "app_main.h"

#define MAX_PUBLISH_ITEMS     (8)
#define _MIIO_PROP_INTERVAL    200

typedef struct 
{
    int is_use;
    uint8_t siid;
    uint8_t type;
    uint16_t piid;
}qm_iot_pub_list_t;

void *qm_iot_handle = NULL;
extern uint8_t ota_start;// = 0xffff;
extern uint16_t mi_connect_handle;// = 0xffff;
unsigned char mi_pair_complete = QM_FALSE;
void * rtc_timer = NULL;
static int rtc_count = 0;
static void * iot_timer = NULL;
static void * _g_map_handle = NULL;
static qm_tlv_ctx_t iot_snapshot_ctx = {0};
static qm_iot_pub_list_t pub_list[MAX_PUBLISH_ITEMS] = {0};


static int iot_init(void *arg);
static void on_action_invoke(action_operation_t  *o);
static void on_property_set(property_operation_t *o);
static void on_property_get(property_operation_t *o);

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);


static void *_miio_prop_snapshot_init(void);
static int _miio_prop_snapshot_set(void *handle, qm_tlv_block_t *tlv_block);
static int _miio_prop_snapshot_get(void *handle, qm_tlv_block_t *tlv_block);


static int _miio_adapter_prop_report(property_operation_t *operation, qm_tlv_block_t *tlv_block);
static int _miio_adapter_event_report(event_operation_t  *operation);

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);

extern void qm_rtc_update_utc_timestamp(uint32_t utc);

static void on_property_set(property_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
#endif
 
    if ((o->siid == 128) && (o->piid == SERVER_UTC_TIME)) {   //检查是否是网关校时命令的返回
        if(rtc_timer){
            miio_timer_stop(rtc_timer);
            miio_timer_delete(rtc_timer);
            rtc_timer = NULL;
        }
        qm_rtc_update_utc_timestamp(o->value->data.number.ulongValue); //校时返回
    }else{
        iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t));
    }
    o->code = 0;
        
}
static void on_property_get(property_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_property_get] siid %d, piid %d ", o->siid, o->piid);
#endif
    int out_len = 0;
    iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    o->code = 0;
}
static void on_action_invoke(action_operation_t *o)
{
#ifdef QM_LOG_TAG
    SERIAL_LOG("[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
#endif
    int out_len = 0;
    iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    o->code = OPERATION_OK; 
}

// tmp fix memory low , need delete
typedef struct {
    iot_generic_platform_type_t platform_type;
    iot_generic_network_type_t network_type;
    iot_generic_hooks_t *hooks;
    iot_generic_hal_t *generic_hal;
    qm_tlv_ctx_t *option;
    void *map_handle;
    void *ctx;
}iot_generic_ctx_tmp_t;
//

static void add_period_publish_attr(void)
{
    int len = 0;
    uint8_t buff[MAX_PUBLISH_ITEMS * 4] = {0};

    iot_generic_option_get(qm_iot_handle, IOT_OPTION_PUBLISH_LIST, buff, &len);
    for (int i = 0; i < len;  i += 4) {
        if (buff[i] != 0) {
            pub_list[i / 4].is_use = QM_TRUE;
            pub_list[i / 4].siid = buff[i];
            pub_list[i / 4].type = buff[i+1];
            pub_list[i / 4].piid = buff[i+2]<<8 | buff[i+3];
            miio_mesh_properties_period_publish_init(pub_list[i / 4].siid, pub_list[i / 4].piid, 0);
        }
    }
    // tmp fix memory low , need delete
    iot_generic_ctx_tmp_t *tmp = (iot_generic_ctx_tmp_t *)qm_iot_handle;
    qm_tlv_ctx_delete(tmp->option);
    tmp->option = NULL;
    //
}

extern void gpio_wakup(uint8_t pin);
extern void xj_thread_timer_start(bool start);
static void _miio_iot_thread(void *arg)
{
    miio_timer_delete(iot_timer);
    iot_timer = NULL;
    gpio_wakup(0);
    miio_system_set_adv_timeout(0);  //stop adv
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
}

static void _miio_update_rtc(void *arg)
{
    miio_mesh_request_property(SERVER_UTC_TIME); //向网关请求校时
    rtc_count++;
    miio_timer_start(rtc_timer, rtc_count * 1000, NULL);
}


static int user_state_process(uint8_t event, void *data)
{

    switch(event){
        case MIBLE_USER_PROV_CB_TYPE_UNPROV:
            // start timer for blinking LEDs to indicate which node is unprovisioned.
            mi_pair_complete = QM_FALSE;
            miio_system_set_adv_timeout(30 * 60 * 1000);  //stop adv after 2min
            miio_timer_create(&iot_timer, _miio_iot_thread, MIBLE_TIMER_SINGLE_SHOT);
            miio_timer_start(iot_timer, 30 * 60 * 1000, NULL);
            // tmp fix memory low , need delete
            iot_generic_ctx_tmp_t *tmp = (iot_generic_ctx_tmp_t *)qm_iot_handle;
            qm_tlv_ctx_delete(tmp->option);
            tmp->option = NULL;
            //
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);       
            break;
        case MIBLE_USER_PROV_CB_TYPE_PROVED:
            mi_pair_complete = QM_TRUE;
            if(mi_connect_handle == 0xffff){
                add_period_publish_attr();
                miio_mesh_request_property(SERVER_UTC_TIME); //向网关请求校时
                miio_timer_create(&rtc_timer, _miio_update_rtc, MIBLE_TIMER_SINGLE_SHOT);
                miio_timer_start(rtc_timer, 1 * 1000, NULL);
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            }else{
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_CLOUD);
            }
            break;
        case MIBLE_USER_PROV_CB_TYPE_START:
            break;
        case MIBLE_USER_PROV_CB_TYPE_COMPLETE:
            mi_pair_complete = QM_TRUE;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
            break;
        case MIBLE_USER_PROV_CB_TYPE_FAIL:
            mi_pair_complete = QM_FALSE;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
        case MIBLE_USER_PROV_CB_TYPE_RESET:
            /* clear user data and restore */
            mi_pair_complete = QM_FALSE;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);
            break;
        case MIBLE_USER_SCENE_CB_ADD:{

        } break;
        case MIBLE_USER_SCENE_CB_DELETE:{

        }break;
        case MIBLE_USER_SCENE_CB_RECALL:{

        }break;
        case MIBLE_USER_GAP_CB_CONNECTED:

        if(mi_pair_complete){
            if(rtc_timer){
                miio_timer_stop(rtc_timer);
                miio_timer_delete(rtc_timer);
                rtc_timer = NULL;
            }
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
        }    
        break;
        case MIBLE_USER_GAP_CB_DISCONNECTED:
        if(mi_pair_complete){
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            miio_mesh_request_property(SERVER_UTC_TIME);
            miio_timer_create(&rtc_timer, _miio_update_rtc, MIBLE_TIMER_REPEATED);
            miio_timer_start(rtc_timer, 30 * 1000, NULL);
        }
        break;
        default:
            break;
    }
    return 0;
}

static int iot_init(void *arg)
{
	qm_iot_handle = arg;
    miio_gatt_spec_init(on_property_set, on_property_get, on_action_invoke, 256);
    miio_mesh_user_callback_register(on_property_set, on_property_get, on_action_invoke, user_state_process);
    return QM_EOK;
}

static int _miio_prop_snapshot_value_judge(qm_tlv_block_t *a, qm_tlv_block_t *b)
{
    int ret = QM_EOK;
    
    int a_len = 0;
    int b_len = 0;

    uint8_t *a_g_byte = NULL;
    uint8_t *b_g_byte = NULL;
    if(a == NULL || b == NULL){
        return QM_FALSE;
    }
    qm_tlv_unpack_bytes_direct(a, &a_g_byte, &a_len);
    qm_tlv_unpack_bytes_direct(b, &b_g_byte, &b_len);

    if(a_len != b_len){
        return QM_EOK;
    }

    ret = memcmp(a_g_byte, b_g_byte, a_len);
    return (ret != 0 ? QM_EOK : -QM_ERROR);
}

static int _miio_prop_snapshot_get(void *handle, qm_tlv_block_t *tlv_block)
{
    int ret = QM_FALSE;
    qm_tlv_block_t *tmp_tlv_block = NULL;
    qm_tlv_ctx_t *snapshot_ctx = (qm_tlv_ctx_t *)handle;

    if (handle == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    tmp_tlv_block = qm_tlv_block_find(snapshot_ctx, tlv_block->tag);
    if(tmp_tlv_block == NULL){
        return QM_EOK;
    }

    ret =  _miio_prop_snapshot_value_judge(tmp_tlv_block, tlv_block);
    qm_tlv_block_remove(snapshot_ctx, tmp_tlv_block);
    return ret;
}

static int _miio_prop_snapshot_create(void *handle, qm_tlv_block_t *tlv_block)
{

    qm_tlv_block_t *tmp_tlv_block = NULL;
    qm_tlv_ctx_t *snapshot_ctx = (qm_tlv_ctx_t *)handle;

    if (handle == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    tmp_tlv_block = qm_tlv_block_creat();
    if(tmp_tlv_block == NULL){
        return -QM_ENOMEM;
    }

    qm_tlv_block_copy(tmp_tlv_block, tlv_block);
    qm_tlv_block_add(snapshot_ctx, tmp_tlv_block);

    return QM_EOK;
}

static int _miio_prop_snapshot_set(void *handle, qm_tlv_block_t *tlv_block)
{
    qm_tlv_block_t *tmp_tlv_block = NULL;

    tmp_tlv_block = qm_tlv_block_find(handle, tlv_block->tag);
    if(tmp_tlv_block == NULL){
        _miio_prop_snapshot_create(handle, tlv_block);
        return QM_EOK;
    }
    qm_tlv_block_copy(tmp_tlv_block, tlv_block);

    return QM_EOK;
}

static int _miio_pub_list_first_use(uint8_t siid, uint16_t piid)
{
    int index = 0;
    int ret = -QM_EINVAL;
    for(index = 0; index < MAX_PUBLISH_ITEMS; index++){
        if(!pub_list[index].is_use){
            continue;
        }
        
        if(pub_list[index].siid == siid && pub_list[index].piid == piid){
                pub_list[index].is_use = QM_FALSE;
                ret = QM_EOK;
                break;
        }
    }
    return ret;
}

static int _miio_adapter_prop_report(property_operation_t *operation, qm_tlv_block_t *tlv_block)
{
    int ret = QM_EOK;
    property_value_t *prop_value = NULL;

    if (operation == NULL){
        return -QM_EINVAL;
    }

    if(mi_connect_handle != 0xFFFF){
        miio_gatt_properties_changed(operation->siid, operation->piid, operation->value);
    }else{
        ret = _miio_prop_snapshot_get(&iot_snapshot_ctx, tlv_block);
        if(ret != QM_EOK){
            property_value_delete(operation->value);
            return ret;
        }
        
        if(!_miio_pub_list_first_use(operation->siid, operation->piid)){
            property_value_delete(operation->value);
            return ret;
        }
        
        miio_mesh_properties_changed(operation->siid, operation->piid, operation->value);
    }

    return QM_EOK;
}


static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation)
{
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
#ifdef QM_LOG_TAG
    SERIAL_LOG("[_xj_to_miio_transformat] siid %d, piid %d type %d ", id_map_data.siid, id_map_data.xiid, id_map_data.type_id);
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
            float temp = (int16_value / 10.0);
            prop_value = property_value_new_float(temp);

        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float temp = (uint16_value / 10.0);
            prop_value = property_value_new_float(temp);       
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_TWO_INT16:						
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            float temp = (int16_value / 100.0); 
            prop_value = property_value_new_float(temp);
            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            float temp = (uint16_value / 100.0);
            prop_value = property_value_new_float(temp);        
        }
        break;
        case  QM_DTTV_TYPE_FLOAT_ONE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float temp = (int32_value  / 10.0);
            prop_value = property_value_new_float(temp);            
        }
        break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float temp = (uint32_value  / 10.0); 
            prop_value = property_value_new_float(temp);         
        }
        break;
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            float temp = (int32_value / 100.0); 
            prop_value = property_value_new_float(temp);
                
        }
        break;	
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            float temp = (uint32_value  / 100.0); 
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
            //TODO: 米家底层库的bug，字符串长度会额外增加6个字节
            if (operation->value->data.string.length > 6) {
                operation->value->data.string.length -= 6;
            }
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
            float temp = operation->value->data.number.floatValue * 10.0;
            temp += 0.5;
            qm_tlv_pack_int32(tlv_block, temp);   
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_INT16:{
            float temp = operation->value->data.number.floatValue * 10.0;
            temp += 0.5;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:{
            float temp = operation->value->data.number.floatValue * 10.0;
            temp += 0.5;
            qm_tlv_pack_uint32(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:{
            float temp = operation->value->data.number.floatValue * 10.0;
            temp += 0.5;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;

        case QM_DTTV_TYPE_FLOAT_TWO_INT16:{
            float temp = operation->value->data.number.floatValue * 100;
            temp += 0.5;
            qm_tlv_pack_int16(tlv_block, temp);
        }break;
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:{
            float temp = operation->value->data.number.floatValue * 100.0;
            temp += 0.5;
            qm_tlv_pack_uint16(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:{
            float temp = operation->value->data.number.floatValue  * 100.0;
            temp += 0.5;
            qm_tlv_pack_int32(tlv_block, temp);
        }break;  
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:{
            float temp = operation->value->data.number.floatValue * 100.0;
            temp += 0.5;
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
    property_operation_t operation = {0};
    int ret = QM_EOK;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while (tlv_block)
    {
        _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        _miio_adapter_prop_report(&operation, tlv_block);
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    return QM_EOK;
}

static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t *operation = (property_operation_t *)data;  

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){

        return -QM_ENOMEM;
    }

    ret = _miio_to_xj_transformat(map_handle, tlv_block, operation);
    if (ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return ret;
    }

    ret = _miio_prop_snapshot_set(&iot_snapshot_ctx, tlv_block);
    if(ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return -QM_ERROR;
    }

    qm_tlv_block_add(tlv_ctx, tlv_block); 
    
    return QM_EOK;
}
 

static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    iot_map_mi_t id_map_data = {0};
    qm_tlv_block_t *tlv_block = qm_tlv_block_creat();
    property_operation_t *operation = (property_operation_t *)data;

    if(tlv_block == NULL){
        return -QM_ENOMEM;
    }  
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

    iot_map_mi_t id_map_data = {0};
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t *operation = (property_operation_t *)data;

    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;
    
    iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));

    tlv_block = qm_tlv_block_find(tlv_ctx, id_map_data.attr_id);

    return _xj_to_miio_transformat(map_handle, tlv_block, operation);

}

static int iot_action_req(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx)
{
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    int in_len = 0;
    iot_map_mi_t id_map_data = {0};
    action_operation_t *operation = (action_operation_t *)data;

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
    return QM_EOK;
}

static int iot_reset(void *arg)
{
    miio_system_restore();
    return QM_EOK;
}


static iot_generic_hal_t  generic_hal_mi_mesh = {

    .init = iot_init,
    .report = iot_report,
    .set = iot_set,
    .get_req = iot_get_req,
    .get_rsp = iot_get_rsp,
//    .event   = NULL,
    .action_req = iot_action_req,
    .action_rsp = iot_action_rsp,
    // .up_rawdata = NULL,
    .reset = iot_reset,
    .deinit = NULL,

};

int iot_generic_hal_mi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, &generic_hal_mi_mesh);
}
