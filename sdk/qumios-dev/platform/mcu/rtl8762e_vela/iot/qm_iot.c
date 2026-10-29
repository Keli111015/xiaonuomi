#include "qm.h"
#include "iot_generic_hal.h"
#include "qm_utils_timer.h"

#include "id_map.h"
#include "qm_tlv.h"

#include "qm_log.h"
#include "qm_utils_string.h"

#include "miio_user_api.h"
#include "mible.h"

#define LOG_TAG "iot_hal"

#define MAX_ARGUMENTS           8
#define MAX_PUBLISH_ITEMS       (10)

typedef struct 
{
    bool is_connected;
    bool is_logined;
    bool is_provisioned;
    void* conn_timer;
    void* handle;
}qm_iot_hal_handle_t;

typedef struct 
{
    uint8_t is_use;
    uint8_t siid;
    uint8_t type;
    uint16_t piid;
}qm_iot_pub_list_t;

static qm_iot_hal_handle_t iot_handle = {
    .is_connected = false,
    .is_logined = false,
    .is_provisioned = false,
    .conn_timer = NULL
};

void *qm_iot_handle = NULL;
static qm_iot_pub_list_t pub_list[MAX_PUBLISH_ITEMS] = {0};

static int iot_init(void *arg);
static int iot_reset(void *arg);
static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  mible_spec_property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  mible_spec_property_operation_t *operation);

extern void user_event_cb(mible_user_event_type_t event, void *user_data);
extern void do_spec_get(mible_spec_bearer_t bearer, mible_spec_property_operation_t *o);
extern void do_spec_set(mible_spec_bearer_t bearer, mible_spec_property_operation_t *o);
extern void do_spec_invoke(mible_spec_bearer_t bearer, mible_spec_action_operation_t *o);

static int iot_init(void *arg)
{
    qm_iot_handle = arg;
    iot_handle.handle = arg;

    return QM_EOK;
}

static int iot_reset(void *arg)
{
    mible_mesh_restore(0);
    return QM_EOK;
}


static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    mible_spec_property_operation_t *operation = (mible_spec_property_operation_t *)data;   

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        return -QM_ENOMEM;
    }

    ret = _miio_to_xj_transformat(map_handle, tlv_block, operation);
    if (ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
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
    mible_spec_property_operation_t *operation = NULL;
    operation = (mible_spec_property_operation_t *)data;

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

    mible_spec_property_operation_t *operation = NULL;
    operation = (mible_spec_property_operation_t *)data;

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

static int qm_iot_hal_adapter_prop_report(mible_spec_property_operation_t *operation, uint8_t num)
{
    char *string_value;
    int string_len;

    mible_spec_property_t property = {0};

    if (operation == NULL || num == 0){
        return -QM_EINVAL;
    }

    if(iot_handle.is_connected == true){

        property.siid = operation->siid;
        property.piid = operation->piid;

        memcpy(&property.value, &operation->value, sizeof(mible_spec_property_value_t));

        mible_gatt_spec_send_properties_changed(num, &property);
    }

    mible_mesh_send_property_changed(operation->siid, operation->piid, &operation->value);

    if(operation->value.format == MIBLE_SPEC_PROPERTY_FORMAT_STRING && operation->value.data.string.str){
        qm_free(operation->value.data.string.str);
        operation->value.data.string.str = NULL;
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
        mible_spec_property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret != QM_EOK){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
            continue;
        }

        ret = qm_iot_hal_adapter_prop_report(&operation, 1);
        if (ret != QM_EOK){
             break;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;

}

#if CONFIG_IOT_GENERIC_EVENT_SUPPORT

static int qm_iot_hal_adapter_event_report(uint16_t siid, uint16_t eiid, mible_spec_arguments_t *newArgs)
{
    if(newArgs == NULL){
        return -QM_EINVAL;
    }

    if(iot_handle.is_connected == true){
        miio_gatt_event_occurred(siid, eiid, newArgs);
    }else{
        miio_mesh_event_occurred(siid, eiid, newArgs);
    }

    return QM_EOK;
}

static int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};

    mible_spec_arguments_t operation = {0};
    mible_spec_argument_t arguments[MAX_ARGUMENTS] = {0};
    
    id_map_data.type = 2;
    id_map_data.attr_id = eid;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return ret;
    }

    operation.arguments = arguments;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    if(tlv_block == NULL ||  tlv_ctx->count == 0){
        ret = qm_iot_hal_adapter_event_report(id_map_data.siid, id_map_data.xiid, &operation);
        return ret; 
    }

    while(tlv_block && tlv_ctx->count){
        uint16_t count = operation.size;
        mible_spec_property_operation_t operation_tmp = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation_tmp);
        if (ret != QM_EOK){
            break;
        }
        
        operation.arguments[count].piid  = operation_tmp.piid;
        operation.arguments[count].value = operation_tmp.value;
        
    #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_event] piid %d", operation.arguments[count].piid);
    #endif
        if(operation.size++ >= MAX_ARGUMENTS){
            ret = qm_iot_hal_adapter_event_report(id_map_data.siid, id_map_data.xiid, &operation);
            if (ret != QM_EOK){
                return ret;
            }
            operation.size = 0;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    } 

    return qm_iot_hal_adapter_event_report(id_map_data.siid, id_map_data.xiid, &operation);
}
#endif

#if CONFIG_IOT_GENERIC_ACTION_SUPPORT

static int iot_action_req(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx)
{

    int ret = QM_EOK;
    int in_len = 0;
    iot_map_mi_t id_map_data = {0};
    mible_spec_action_operation_t *operation = (mible_spec_action_operation_t *)data;

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    id_map_data.type = 1;
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->aiid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return ret;
    }
    
    *aid = id_map_data.attr_id;

    while(in_len < operation->vals.size){

        qm_tlv_block_t *tlv_block = NULL;
        mible_spec_property_operation_t  prop_operation = {0};

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            return -QM_ENOMEM;
        }  

        prop_operation.siid  =  operation->siid;
        prop_operation.piid  =  operation->vals.arguments[in_len].piid;
        memcpy(&prop_operation.value, &operation->vals.arguments[in_len].value, sizeof(mible_spec_property_value_t));

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
    int out_len = 0;
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    mible_spec_action_operation_t *out_operation = (mible_spec_action_operation_t *)data;

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    out_operation->vals.size = 0;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block && tlv_ctx->count){
        mible_spec_property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret < 0){
            break;
        }

        out_operation->vals.arguments[out_len].piid  = operation.piid;
        memcpy(&out_operation->vals.arguments[out_len].value, &operation.value, sizeof(mible_spec_property_value_t));

        out_operation->vals.size = ++out_len;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;

    return QM_EOK;
}
#endif


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
   
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, &generic_hal_mi_mesh);
}


#if CONFIG_QM_RTC_SUPPORT
extern void qm_rtc_update_utc_timestamp(uint32_t utc);
#define SERVER_UTC_TIME  (3)
#endif

void do_spec_set(mible_spec_bearer_t bearer, mible_spec_property_operation_t *o)
{
    o->code = MIBLE_SPEC_OPERATION_OK;
#if CONFIG_QM_RTC_SUPPORT
    if ((o->siid == 128) && (o->piid == SERVER_UTC_TIME)) 
    {   //检查是否是网关校时命令的返回
        qm_rtc_update_utc_timestamp(o->value.data.ulongValue); //校时返回
    }
    else
#endif
    {
        iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(mible_spec_property_operation_t));
    }
}

void do_spec_get(mible_spec_bearer_t bearer, mible_spec_property_operation_t *o)
{
    int out_len = 0;
    o->code = MIBLE_SPEC_OPERATION_OK;
    iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(mible_spec_property_operation_t), (char *)o , &out_len);
}

void do_spec_invoke(mible_spec_bearer_t bearer, mible_spec_action_operation_t *o)
{
    int out_len = 0;
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(mible_spec_action_operation_t), (char *)o , &out_len);
#endif
    o->code = MIBLE_SPEC_OPERATION_OK; 
}   

static void mible_conn_timeout_handler(void* arg)
{
    mible_status_t status;

    miio_timer_delete(iot_handle.conn_timer);
    iot_handle.conn_timer = NULL;

    status = mible_gap_disconnect(0);
    if (status){

    }
}

void add_period_publish_attr(void)
{
    int len = 0;
    uint8_t buff[MAX_PUBLISH_ITEMS * 4] = {0};

    iot_generic_option_get(iot_handle.handle, IOT_OPTION_PUBLISH_LIST, buff, &len);

    for (int i = 0; i < len;  i += 4) {
        if (buff[i] != 0) {
            pub_list[i / 4].is_use = QM_TRUE;
            pub_list[i / 4].siid = buff[i];
            pub_list[i / 4].type = buff[i+1];
            pub_list[i / 4].piid = buff[i+2]<<8 | buff[i+3];
            miio_mesh_properties_period_publish_init(pub_list[i / 4].siid, pub_list[i / 4].piid, 0);
        }
    }
}

void user_event_cb(mible_user_event_type_t event, void *user_data)
{

    switch (event)
    {
        case MIBLE_USER_PROV_CB_TYPE_UNPROV: //设备未配网
            iot_handle.is_provisioned = false;
            miio_system_set_adv_timeout(30 * 60 * 1000);  //stop adv after 30min
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);
        break;
        case MIBLE_USER_PROV_CB_TYPE_FAIL:  //配网失败

            iot_handle.is_provisioned = false;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG_FAIL);
        break;
        case MIBLE_USER_PROV_CB_TYPE_START:// 配网开始
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_APP_CONFIG);
        break;
        case MIBLE_USER_PROV_CB_TYPE_COMPLETE:// 配网完成
            iot_handle.is_provisioned = true;
            mibeacon_set_adv_timeout(0xFFFFFFFF);
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
        break;
        case MIBLE_USER_PROV_CB_TYPE_PROVED:// 配网完成
            mibeacon_set_adv_timeout(0xFFFFFFFF);
            iot_handle.is_provisioned = true;
            if(!iot_handle.is_connected){
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            }else{
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_CLOUD);
            }
        break;
        case MIBLE_USER_PROV_CB_TYPE_RESET://配网解除
            iot_handle.is_provisioned = false;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
        break;
        case MIBLE_USER_SCENE_CB_ADD:// 登录开始
        {
            mible_mesh_scene_store_t *scene = user_data;

             //Confirmed subscribe 0xFEFE
            extern struct bt_mesh_elem elements[];
            struct bt_mesh_model *mod = &elements[0].vnd_models[0];

            uint16_t *entry = bt_mesh_model_find_group(&mod, MIBLE_MESH_DEVICE_GROUP);
            if(entry == NULL){
                entry = bt_mesh_model_find_group(&mod, BT_MESH_ADDR_UNASSIGNED);
                if(entry){
                    *entry = MIBLE_MESH_DEVICE_GROUP;
                    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
                        bt_mesh_model_sub_store(mod);
                    }
                    printk("success sub dev group addr %04x!!\n", MIBLE_MESH_DEVICE_GROUP);
                }
            }
        }
        break;
        case MIBLE_USER_SCENE_CB_RECALL:
        {
            mible_mesh_scene_delete_t *scene = user_data;
        }
            break;
        case MIBLE_USER_GAP_CB_CONNECTED:
        {
            int err;
            iot_handle.is_connected = true;
            iot_handle.is_logined = false;
            if(iot_handle.is_provisioned){
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
            }

            //未绑定状态下is_provisioned为true表示已经绑定
            if (iot_handle.conn_timer == NULL && iot_handle.is_provisioned == false)
            {
                int status = miio_timer_create(&iot_handle.conn_timer,mible_conn_timeout_handler, MIBLE_TIMER_SINGLE_SHOT);
                if (status){

                }else{
                    miio_timer_start(iot_handle.conn_timer, 20000, NULL);
                }
            }
        }
        break;

        case MIBLE_USER_GAP_CB_DISCONNECTED:
        {
            iot_handle.is_connected = false;
            if (iot_handle.conn_timer != NULL){
                miio_timer_stop(iot_handle.conn_timer);
                miio_timer_delete(iot_handle.conn_timer);
                iot_handle.conn_timer = NULL;
            }
            if(iot_handle.is_provisioned){
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            }
        }
        break;
        case MIBLE_USER_LOGIN_CB_TYPE_COMPLETE:
        {
            iot_handle.is_logined = true;
        }
        break;
        
        default:
        
        break;
    }
    return;
}

static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  mible_spec_property_operation_t *operation)
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
            qm_tlv_pack_bool(tlv_block,  operation->value.data.boolean);
        break;
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT

        case QM_DTTV_TYPE_STRING:
            qm_tlv_pack_string(tlv_block, (char *)operation->value.data.string.str,operation->value.data.string.length);
        break;

        case QM_DTTV_TYPE_STRING_ARRAY:
        {
            uint16_t bytes_len = 0;
            uint8_t *pdata = NULL;
            uint8_t bytes[64] = {0};
            bytes_len = operation->value.data.string.length / 2 + operation->value.data.string.length % 2;
            if (bytes_len > 64){
                pdata = (char *)qm_malloc(bytes_len);
                if(pdata == NULL){
                    return -QM_ENOMEM;
                }
                hex_str_to_nums((char *)operation->value.data.string.str, 
                                operation->value.data.string.length, 
                                pdata);
                qm_tlv_pack_bytes(tlv_block, pdata, bytes_len);               
                qm_free(pdata);
                pdata = NULL;
            }else{
                hex_str_to_nums((char *)operation->value.data.string.str, 
                                operation->value.data.string.length, 
                                bytes);
                qm_tlv_pack_bytes(tlv_block, bytes, bytes_len);                    
            }    
        }break;
#endif
        case QM_DTTV_TYPE_INT8 :
            qm_tlv_pack_int8(tlv_block,operation->value.data.charValue);   
        break;
        case QM_DTTV_TYPE_UINT8 :
            qm_tlv_pack_uint8(tlv_block, operation->value.data.ucharValue);   
        break;
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
        case QM_DTTV_TYPE_INT16 : 
            qm_tlv_pack_int16(tlv_block, operation->value.data.shortValue);   
        break;
        case QM_DTTV_TYPE_UINT16 : 
            qm_tlv_pack_uint16(tlv_block, operation->value.data.ushortValue);     
        break;
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
        case QM_DTTV_TYPE_INT32 : 
            qm_tlv_pack_int32(tlv_block, operation->value.data.longValue);   
        break;
        case QM_DTTV_TYPE_UINT32 :
            qm_tlv_pack_uint32(tlv_block, operation->value.data.ulongValue);   
        break;
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
        case QM_DTTV_TYPE_FLOAT32:
            qm_tlv_pack_float32(tlv_block, operation->value.data.floatValue);   
        break;	
#endif   
        default:
        break;
    }
    return QM_EOK;
}

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  mible_spec_property_operation_t *operation)
{
    qm_err_t ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};

    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    id_map_data.attr_id = tlv_block->tag;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    qm_tlv_block_data_type_set(tlv_block, id_map_data.type_id);
    switch(id_map_data.type_id)
    {
        case QM_DTTV_TYPE_BOOL:{
            bool_t bool_value;
            qm_tlv_unpack_bool(tlv_block, &bool_value);
            mible_spec_property_value_new_boolean(&operation->value, bool_value);

        }break;
        case QM_DTTV_TYPE_INT8:{
            int8_t int8_value;
            qm_tlv_unpack_int8(tlv_block, &int8_value);
            mible_spec_property_value_new_char(&operation->value, int8_value);
        }
        break;
        case QM_DTTV_TYPE_UINT8:{
            uint8_t uint8_value;
            qm_tlv_unpack_uint8(tlv_block, &uint8_value);
            mible_spec_property_value_new_uchar(&operation->value, uint8_value);

        }break;
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
        case QM_DTTV_TYPE_INT16:
        {
            int16_t int16_value;
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            mible_spec_property_value_new_short(&operation->value, int16_value);

        }
        break;
        case QM_DTTV_TYPE_UINT16:{
            uint16_t uint16_value;
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            mible_spec_property_value_new_ushort(&operation->value, uint16_value);

        }break;
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
        case QM_DTTV_TYPE_INT32:
        {
            int32_t int32_value;
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            mible_spec_property_value_new_long(&operation->value, int32_value);
                
        }
        break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t uint32_value;
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            mible_spec_property_value_new_ulong(&operation->value, uint32_value);
        }
        break;
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
        case QM_DTTV_TYPE_STRING:
        {
            int string_len = 0;
            char *string_value = NULL;
            char *p_strdata   = NULL;

            qm_tlv_unpack_string_direct(tlv_block, &string_value, &string_len);


            p_strdata = (char *)qm_malloc(string_len + 1);
            if(p_strdata == NULL){
                return -QM_ENOMEM;
            }
            memset(p_strdata, 0, string_len + 1);
            memcpy(p_strdata, string_value, string_len);

            mible_spec_property_value_new_nstring(&operation->value, (const uint8_t *)p_strdata, string_len);
        // [huanglei]todo: 在调用米家上报接口后释放
        #if 0
            qm_free(p_strdata);
            p_strdata = NULL;  
        #endif
        //
        }break;

        case QM_DTTV_TYPE_STRING_ARRAY:{
            int pdata_len = 0;
            uint8_t *pdata   = NULL;
            char *p_strdata   = NULL;

            qm_tlv_unpack_bytes_direct(tlv_block, &pdata, &pdata_len);
            if(pdata_len == NULL){
                break;
            }

            p_strdata = (char *)qm_malloc(pdata_len * 2 + 1);
            if(p_strdata == NULL){
                return -QM_ENOMEM;
            }
            memset(p_strdata, 0, pdata_len * 2 + 1);

            hex_to_strs(pdata, pdata_len, p_strdata);  

            mible_spec_property_value_new_nstring(&operation->value, p_strdata, pdata_len * 2 + 1);
        // [huanglei]todo: 在调用米家上报接口后释放
        #if 0
            qm_free(p_strdata);
            p_strdata = NULL;  
        #endif
        //
        }break;
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
        case QM_DTTV_TYPE_FLOAT32:{
            float32_t float_value;
            qm_tlv_unpack_float32(tlv_block, &float_value);
            mible_spec_property_value_new_float(&operation->value, float_value);
        }break;
#endif
        default:
        break;

    }

    operation->siid  = id_map_data.siid;
    operation->piid  = id_map_data.xiid;

    return QM_EOK;
}
