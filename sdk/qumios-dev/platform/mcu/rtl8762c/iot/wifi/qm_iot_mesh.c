#include "iot_generic_hal.h"

#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"


#include "qm_log.h"
#include "app_task.h"
#include "app_main.h"
#include "miio_user_api.h"
#include "mible_mesh_api.h"

#include "qm_utils_timer.h"

#define QM_LOG_TAG "iot_hal"

#define MAX_PUBLISH_ITEMS     (8)
#define _MIIO_PROP_TIMEOUT  1000

void *qm_iot_mesh_hal_handle = NULL;

qm_timer_t pair_tiemr = {0};
qm_timer_t alive_tiemr = {0};
uint16_t mi_connect_handle = 0xffff;
void *_miio_prop_snapshot_handle = NULL;

typedef struct
{
    void *handle; 
    qm_mutex_t lock;
    qm_tlv_ctx_t  *ctx;
}_miio_prop_snapshot_t;

typedef struct 
{
    int is_use;
    uint8_t siid;
    uint8_t type;
    uint16_t piid;
}qm_iot_pub_list_t;

typedef struct
{
    void (*pair_notify_cb)(void);
}miio_hal_notify_t;

static miio_hal_notify_t miio_hal_notify = {0};
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

#if CONFIG_QM_RTC_SUPPORT
extern void qm_rtc_update_utc_timestamp(uint32_t utc);
#endif

static void on_property_set(property_operation_t *o)
{
#ifdef QM_LOG_TAG
//    QM_LOGD(QM_LOG_TAG, "[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
#endif
#if CONFIG_QM_RTC_SUPPORT
    if ((o->siid == 128) && (o->piid == SERVER_UTC_TIME)) {   //检查是否是网关校时命令的返回
        qm_rtc_update_utc_timestamp(o->value->data.number.ulongValue); //校时返回
    }else{
#endif
        iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t));
#if CONFIG_QM_RTC_SUPPORT
    }
#endif
    o->code = 0;   
}

static void on_property_get(property_operation_t *o)
{
#ifdef QM_LOG_TAG
//    QM_LOGD(QM_LOG_TAG, "[on_property_get] siid %d, piid %d ", o->siid, o->piid);
#endif
    int out_len = 0;
    iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    o->code = 0;
}
static void on_action_invoke(action_operation_t *o)
{
#ifdef QM_LOG_TAG
//    QM_LOGD(QM_LOG_TAG,"[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
#endif
    int out_len = 0;
    int ret = QM_EOK;
    ret = iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_METHOD; 
    }else{
        o->code = OPERATION_OK; 
    }
}


bool_t mi_pair_complete = false;

static void pair_timer_callback( qm_timer_t *timer, void *arg)
{
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
}

static void alive_timer_callback( qm_timer_t *timer, void *arg)
{
    static int count = 0;
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t operation = {0};
    _miio_prop_snapshot_t *snapshot_handle = (_miio_prop_snapshot_t *)_miio_prop_snapshot_handle;
   
    if(count++ >= 4){
        count = 0;
        qm_timer_stop(&alive_tiemr);
        return; 
    }

    qm_mutex_lock(&snapshot_handle->lock, QM_WAIT_FOREVER);
    tlv_block = qm_tlv_block_next(snapshot_handle->ctx, NULL);
    qm_mutex_unlock(&snapshot_handle->lock);
    
    if(tlv_block == NULL){
        return;
    }
        
    _xj_to_miio_transformat(snapshot_handle->handle, tlv_block, &operation);
    miio_mesh_properties_changed(operation.siid, operation.piid, operation.value);

}

uint8_t miio_enter_pair_notify_register(void (*notify)(void))
{
    miio_hal_notify.pair_notify_cb = notify;
}

static void add_period_publish_attr(void)
{
    int len = 0;
    uint8_t buff[MAX_PUBLISH_ITEMS * 4] = {0};

    iot_generic_option_get(qm_iot_mesh_hal_handle, IOT_OPTION_PUBLISH_LIST, buff, &len);
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

static int user_state_process(uint8_t event, void *data)
{

    switch(event){
    case MIBLE_USER_PROV_CB_TYPE_UNPROV:
        // start timer for blinking LEDs to indicate which node is unprovisioned.
    #ifdef QM_LOG_TAG
//		QM_LOGD(QM_LOG_TAG, "-------CB_TYPE_UNPROV");
    #endif
        mi_pair_complete = false;
        if(miio_hal_notify.pair_notify_cb){
            miio_hal_notify.pair_notify_cb();
        }
        qm_timer_new(&pair_tiemr, pair_timer_callback, NULL, 30 * 60 * 1000, 0);
        qm_timer_start(&pair_tiemr);
        miio_system_set_adv_timeout(30 * 60 * 1000);  //stop adv after 2min
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);
        break;
    case MIBLE_USER_PROV_CB_TYPE_PROVED:
    #ifdef QM_LOG_TAG
		QM_LOGD(QM_LOG_TAG, "-------CB_TYPE_PROVED");
    #endif
        mi_pair_complete = true;
        if(mi_connect_handle == 0xffff && mi_pair_complete){
            qm_timer_stop(&pair_tiemr);
            add_period_publish_attr();
            miio_mesh_request_property(SERVER_UTC_TIME); //向网关请求校时
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
        }else{
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_CLOUD);
        }
        
        break;
    case MIBLE_USER_PROV_CB_TYPE_START:
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_START\n");
    #endif
        break;
    case MIBLE_USER_PROV_CB_TYPE_COMPLETE:
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_COMPLETE\n");
    #endif
        mi_pair_complete = true;
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
        break;
    case MIBLE_USER_PROV_CB_TYPE_FAIL:
        
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_FAIL\n");
    #endif
        mi_pair_complete = false;
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
    case MIBLE_USER_PROV_CB_TYPE_RESET:
        /* clear user data and restore */
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------CB_TYPE_RESET");
    #endif
        mi_pair_complete = false;
        iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);
        break;
    case MIBLE_USER_SCENE_CB_ADD:{
        vendor_mesh_scene_store_t *scene = (vendor_mesh_scene_store_t *)data;
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "Scene Add: id %d, siid %d, piid %d\n", scene->scene_id, scene->siid, scene->piid);
    #endif
    } break;
    case MIBLE_USER_SCENE_CB_DELETE:{
        vendor_mesh_scene_delete_t *scene = (vendor_mesh_scene_delete_t *)data;
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "Scene Delete: id %d, siid %d, piid %d\n", scene->scene_id, scene->siid, scene->piid);
    #endif
    }break;
    case MIBLE_USER_SCENE_CB_RECALL:{
        vendor_mesh_scene_recall_t *scene = (vendor_mesh_scene_recall_t *)data;
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "Scene Recall: id %d\n", scene->scene_id);
    #endif
    }break;
    case MIBLE_USER_GAP_CB_CONNECTED:
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------MIBLE_USER_GAP_CB_CONNECTED\n");
    #endif

        mi_connect_handle = 1;
        if(mi_pair_complete){
            qm_timer_new(&alive_tiemr, alive_timer_callback, NULL, 30 * 1000, 1);
            qm_timer_start(&alive_tiemr);
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
        }
        break;
    case MIBLE_USER_GAP_CB_DISCONNECTED:
    #ifdef QM_LOG_TAG
//        QM_LOGD(QM_LOG_TAG, "-------MIBLE_USER_GAP_CB_DISCONNECTED\n");
    #endif

        mi_connect_handle = 0xFFFF;
        if(mi_pair_complete){
            qm_timer_stop(&alive_tiemr);
            qm_timer_free(&alive_tiemr);
            miio_mesh_request_property(SERVER_UTC_TIME);
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
        }
        break;
        default:
        break;
    }
    return 0;
}

static int iot_init(void *arg)
{
    _miio_prop_snapshot_handle = _miio_prop_snapshot_init();
    if(_miio_prop_snapshot_handle == NULL){
        return -QM_EINVAL;
    }

    qm_iot_mesh_hal_handle = arg;
    miio_gatt_spec_init(on_property_set, on_property_get, on_action_invoke, 256);
    miio_mesh_user_callback_register(on_property_set, on_property_get, on_action_invoke, user_state_process);
    return QM_EOK;
}

static void *_miio_prop_snapshot_init(void)
{
    int ret = QM_EOK;
    _miio_prop_snapshot_t *handle = NULL;

    handle = (_miio_prop_snapshot_t *)qm_malloc(sizeof(_miio_prop_snapshot_t));
    if(handle == NULL){
        return NULL;
    }

    memset(handle, 0, sizeof(_miio_prop_snapshot_t));
    ret = qm_mutex_new(&handle->lock);
    if (ret != QM_EOK){
        goto _error;
    }
    
    handle->ctx = qm_tlv_ctx_creat();
    if(handle->ctx == NULL){
        goto _error;
    }


    return (void *)handle;

_error:
    
    if(qm_mutex_is_valid(&handle->lock)){
        qm_mutex_free(&handle->lock);
    }

    if(handle->ctx){
        qm_tlv_ctx_delete(handle->ctx);
        handle->ctx = NULL;
    }

    qm_free(handle);

    return NULL;
}

static int _miio_prop_snapshot_create(void *handle, qm_tlv_block_t *tlv_block)
{

    qm_utils_time_t *timeout = NULL;
    qm_tlv_block_t *tmp_tlv_block = NULL;
    _miio_prop_snapshot_t *snapshot_handle = (_miio_prop_snapshot_t *)handle;

    if (handle == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    tmp_tlv_block = qm_tlv_block_creat();
    if(tmp_tlv_block == NULL){
        return -QM_ENOMEM;
    }

    timeout = (qm_utils_time_t *)qm_malloc(sizeof(qm_utils_time_t));
    if(timeout == NULL){
        qm_tlv_ctx_delete(tmp_tlv_block);
        return -QM_ENOMEM;
    }
    memset(timeout, 0, sizeof(qm_utils_time_t));


    qm_tlv_block_copy(tmp_tlv_block, tlv_block);
    tmp_tlv_block->arg = (void *)timeout;

    qm_mutex_lock(&snapshot_handle->lock, QM_WAIT_FOREVER);
    qm_tlv_block_add(snapshot_handle->ctx, tmp_tlv_block);
    qm_mutex_unlock(&snapshot_handle->lock);

    qm_utils_time_countdown_ms(timeout, _MIIO_PROP_TIMEOUT);

    return QM_EOK;
}

static int _miio_prop_snapshot_set(void *handle, qm_tlv_block_t *tlv_block)
{
    qm_utils_time_t *timeout = NULL;
    qm_tlv_block_t *tmp_tlv_block = NULL;
    _miio_prop_snapshot_t *snapshot_handle = (_miio_prop_snapshot_t *)handle;

    if (handle == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&snapshot_handle->lock, QM_WAIT_FOREVER);
    tmp_tlv_block = qm_tlv_block_find(snapshot_handle->ctx, tlv_block->tag);
    qm_mutex_unlock(&snapshot_handle->lock);
    if(tmp_tlv_block == NULL){
        return _miio_prop_snapshot_create(handle, tlv_block);
    }
    
    timeout = (qm_utils_time_t *)tmp_tlv_block->arg;
    tlv_block->arg = (void *)timeout;

    qm_mutex_lock(&snapshot_handle->lock, QM_WAIT_FOREVER);
    qm_tlv_block_copy(tmp_tlv_block, tlv_block);
    qm_mutex_unlock(&snapshot_handle->lock);

    qm_utils_time_countdown_ms(timeout, _MIIO_PROP_TIMEOUT);

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
    qm_utils_time_t *timeout = NULL;
    qm_tlv_block_t *tmp_tlv_block = NULL;

    _miio_prop_snapshot_t *snapshot_handle = (_miio_prop_snapshot_t *)handle;

    if (handle == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&snapshot_handle->lock, QM_WAIT_FOREVER);
    tmp_tlv_block = qm_tlv_block_find(snapshot_handle->ctx, tlv_block->tag);
    qm_mutex_unlock(&snapshot_handle->lock);
    if(tmp_tlv_block == NULL){
        return _miio_prop_snapshot_create(handle, tlv_block);
    }

    timeout = (qm_utils_time_t *)tmp_tlv_block->arg;
    tlv_block->arg = (void *)timeout;

    if(qm_utils_time_is_expired(timeout)){
        goto _exit;
    }

    ret =  _miio_prop_snapshot_value_judge(tmp_tlv_block, tlv_block);

_exit:
    qm_tlv_block_copy(tmp_tlv_block, tlv_block);
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
        prop_value = (property_value_t *)qm_malloc(sizeof(property_value_t));
        if(prop_value == NULL){
            return -QM_EINVAL;
        }
        
        memset(prop_value, 0, sizeof(property_value_t));
        memcpy(prop_value,  operation->value, sizeof(property_value_t));
        
        miio_gatt_properties_changed(operation->siid, operation->piid, prop_value);//gen_serial_notify_post(EVENT_XJIANG_PRO_GATT_REPORT, operation);

        operation->value = prop_value;
    }

    ret = _miio_prop_snapshot_get(_miio_prop_snapshot_handle, tlv_block);
    if(ret != QM_EOK){
        property_value_delete(operation->value);
        return ret;
    }

    if(!_miio_pub_list_first_use(operation->siid, operation->piid)){
        property_value_delete(operation->value);
        return ret;
    }

    miio_mesh_properties_changed(operation->siid, operation->piid, operation->value);//gen_serial_notify_post(EVENT_XJIANG_PRO_MESH_REPORT, operation);

    return QM_EOK;
}

static int _miio_adapter_event_report(event_operation_t  *operation)
{
    if (operation == NULL){
        return -QM_EINVAL;
    }

    if(mi_connect_handle != 0xFFFF){
        gatt_send_event_occurred(operation->siid, operation->eiid, operation->arguments);
    }else{
        miio_mesh_event_occurred(operation->siid, operation->eiid, operation->arguments);
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
    QM_LOGD(QM_LOG_TAG, "[_xj_to_miio_transformat] siid %d, piid %d type %d ", id_map_data.siid, id_map_data.xiid, id_map_data.type_id);
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
    int ret = QM_EOK;
    static uint8_t init = 1;
    qm_tlv_block_t *tlv_block = NULL;
     _miio_prop_snapshot_t *snapshot_handle = (_miio_prop_snapshot_t *)_miio_prop_snapshot_handle;

    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    if(init){
        init = 0;
        snapshot_handle->handle = map_handle;
    }
    
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){

        property_operation_t operation = {0};
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret != QM_EOK){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
            continue;
        }

        _miio_adapter_prop_report(&operation, tlv_block);

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
        qm_tlv_block_delete(tlv_block);

        return ret;
    }

    ret = _miio_prop_snapshot_set(_miio_prop_snapshot_handle, tlv_block);
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

#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
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

    #ifdef QM_LOG_TAG
        QM_LOGD(QM_LOG_TAG, "[iot_event] SIID %d EIID %d",  operation.eiid,  operation.siid);
    #endif


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
        QM_LOGD(QM_LOG_TAG, "[iot_event] piid %d", arguments->arguments[count].piid);
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
#endif

static int iot_reset(void *arg)
{
    mible_record_clear();
    miio_system_restore();
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
    .reset = iot_reset,
    .deinit = NULL,

};

int iot_generic_hal_mi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, &generic_hal_mi_mesh);
}