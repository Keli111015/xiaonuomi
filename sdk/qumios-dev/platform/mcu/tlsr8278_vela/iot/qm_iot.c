#include "iot_generic_hal.h"

#include "qm.h"

#include "id_map.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_iot.h"

#include "qm_log.h"
#include "qm_utils_string.h"

#include "miio_user_api.h"
#include "mible_mesh_api.h"
#include "mible_api.h"
#include <sys/printk.h>

#include "qm_utils_timer.h"
#include "syslog.h"


#define LOG_TAG "iot_hal"



#define MAX_ARGUMENTS           8


typedef struct{
    void* conn_timer;
    bool is_connected;
    bool mi_pair_complete;
}qm_iot_hal_handle_t;

static qm_iot_hal_handle_t iot_handle = {
    .conn_timer = NULL,
    .is_connected = false,
    .mi_pair_complete = false,
};

static void on_action_invoke(action_operation_t* o);
static void on_property_set(property_operation_t* o);
static void on_property_get(property_operation_t* o);

static int _xj_to_miio_transformat(void* map_handle, qm_tlv_block_t* tlv_block, property_operation_t* operation);
static int _miio_to_xj_transformat(void* map_handle, qm_tlv_block_t* tlv_block, property_operation_t* operation);

// static int _miio_adapter_prop_report(property_operation_t *operation);
// static int _miio_adapter_event_report(event_operation_t  *operation);

static int iot_init(void* arg);
static int iot_reset(void* arg);
static int iot_report(void* map_handle, qm_tlv_ctx_t* tlv_ctx);
static int iot_set(char* data, int len, void* map_handle, qm_tlv_ctx_t* tlv_ctx);
static int iot_get_req(char* data, int len, void* map_handle, qm_tlv_ctx_t* tlv_ctx);
static int iot_get_rsp(void* map_handle, qm_tlv_ctx_t* tlv_ctx, char* data, int* len);

static int qm_iot_hal_adapter_prop_report(property_operation_t* operation, uint8_t num);
static int qm_iot_hal_adapter_event_report(event_operation_t* operation);

bool mi_get_pair_complete_status(void)
{
    return iot_handle.mi_pair_complete;
}

bool mi_get_connected_status(void)
{
    return iot_handle.is_connected;
}

static void on_property_set(property_operation_t* o)
{
    // printk("[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
// #ifdef LOG_TAG
//     QM_LOGD(LOG_TAG, "[on_property_set] siid %d, piid %d type %d", o->siid, o->piid, o->value->format);
// #endif
    //检查是否是网关校时命令的返回
    if ((o->siid == 128) && (o->piid == 3)){
        //校时返回
        uint32_t ts = o->value->data.number.ulongValue;
        if (localtime_set_basets(ts)){
            localtime_start(false);
            localtime_start(true);
        }
    }
    else{
        iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char*)o, sizeof(property_operation_t));
    }
    o->code = 0;
}

static void on_property_get(property_operation_t* o)
{
    int out_len = 0;
    iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char*)o, sizeof(property_operation_t), (char*)o, &out_len);

    o->code = 0;
}

static void on_action_invoke(action_operation_t* o)
{
// #ifdef LOG_TAG
//     QM_LOGD(LOG_TAG, "[on_action_invoke] siid %d, Aiid %d ", o->siid, o->aiid);
// #endif
    int out_len = 0;
    int ret = QM_EOK;
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    ret = iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, (char*)o, sizeof(property_operation_t), (char*)o, &out_len);
#endif
    if (ret != QM_EOK){
        o->code = OPERATION_ERROR_METHOD;
    }
    else{
        o->code = OPERATION_OK;
    }
}

static void mible_conn_timeout_handler(void* arg)
{
    mible_status_t status;

    mible_timer_delete(iot_handle.conn_timer);
    iot_handle.conn_timer = NULL;

    if (get_mi_authorization() != UNAUTHORIZATION){
        return;
    }

#ifdef LOG_TAG
    QM_LOGW(LOG_TAG, "mible conn timeout");
#endif

    status = mible_gap_disconnect(0);
    if (status){
#ifdef LOG_TAG
        QM_LOGE(LOG_TAG, "Unable disconnect %d", status);
#endif
    }
}
static int user_state_process(uint8_t event, void* data)
{
    mible_status_t status;
    switch (event){
        case MIBLE_USER_PROV_CB_TYPE_UNPROV: //设备未配网
#ifdef LOG_TAG
            QM_LOGD(QM_LOG_TAG, "-------CB_TYPE_UNPROV");
#endif
            iot_handle.mi_pair_complete = false;
            miio_system_set_adv_timeout(30 * 60 * 1000);  //stop adv after 30min
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG);

            break;
        case MIBLE_USER_PROV_CB_TYPE_PROVED: //设备已配网
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------CB_TYPE_PROVED");
#endif
            iot_handle.mi_pair_complete = true;

            if (!iot_handle.is_connected){
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            }
            else{
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_CLOUD);
            }
            break;
        case MIBLE_USER_PROV_CB_TYPE_START: // 配网开始
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_START\n");
#endif
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_APP_CONFIG);
            break;
        case MIBLE_USER_PROV_CB_TYPE_COMPLETE:// 配网完成
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_COMPLETE\n");
#endif
            iot_handle.mi_pair_complete = true;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
            break;
        case MIBLE_USER_PROV_CB_TYPE_FAIL: //配网失败
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------MIBLE_USER_PROV_CB_TYPE_FAIL\n");
#endif
            iot_handle.mi_pair_complete = false;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_BLE_CONFIG_FAIL);
        case MIBLE_USER_PROV_CB_TYPE_RESET: //配网解除
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------CB_TYPE_RESET");
#endif
            iot_handle.mi_pair_complete = false;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
            break;
        case MIBLE_USER_SCENE_CB_ADD:
            {  // 登录开始
                vendor_mesh_scene_store_t* scene = (vendor_mesh_scene_store_t*)data;
#ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "Scene Add: id %d, siid %d, piid %d\n", scene->scene_id, scene->siid, scene->piid);
#endif
            } break;
        case MIBLE_USER_SCENE_CB_DELETE:
            {
                vendor_mesh_scene_delete_t* scene = (vendor_mesh_scene_delete_t*)data;
#ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "Scene Delete: id %d, siid %d, piid %d\n", scene->scene_id, scene->siid, scene->piid);
#endif
            }break;
        case MIBLE_USER_SCENE_CB_RECALL:
            {
                vendor_mesh_scene_recall_t* scene = (vendor_mesh_scene_recall_t*)data;
#ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "Scene Recall: id %d\n", scene->scene_id);
#endif
            }break;
        case MIBLE_USER_GAP_CB_CONNECTED:
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------MIBLE_USER_GAP_CB_CONNECTED\n");
#endif
            if (iot_handle.conn_timer == NULL){
                status = mible_timer_create(&iot_handle.conn_timer,
                    mible_conn_timeout_handler,
                    MIBLE_TIMER_SINGLE_SHOT);
                if (status){
#ifdef LOG_TAG
                    QM_LOGE(LOG_TAG, "Unable create conn timeout timer");
#endif
                }                
                else{
                    mible_timer_start(iot_handle.conn_timer, 20000, NULL);
                }
            }
            iot_handle.is_connected = true;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
            break;
        case MIBLE_USER_GAP_CB_DISCONNECTED:
#ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "-------MIBLE_USER_GAP_CB_DISCONNECTED\n");
#endif
            iot_handle.is_connected = false;
            if (iot_handle.conn_timer != NULL){
                mible_timer_stop(iot_handle.conn_timer);
                mible_timer_delete(iot_handle.conn_timer);
                iot_handle.conn_timer = NULL;
            }
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
            break;
        default:
            break;
    }
    return 0;
}

static int qm_iot_hal_adapter_prop_report(property_operation_t* operation, uint8_t num)
{
    if (operation == NULL || num == 0){
        return -QM_EINVAL;
    }

    if (iot_handle.is_connected == true){
        miio_gatt_property_changed(operation->siid, operation->piid, operation->value);
    }
    else{
        miio_mesh_properties_changed(operation->siid, operation->piid, operation->value);
    }

    return QM_EOK;
}

static int qm_iot_hal_adapter_event_report(event_operation_t* operation)
{
    if (operation == NULL){
        return -QM_EINVAL;
    }
    if (iot_handle.is_connected == true){
        gatt_send_event_occurred(operation->siid, operation->eiid, operation->arguments);
    }
    else{
        miio_mesh_event_occurred(operation->siid, operation->eiid, operation->arguments);
    }



    return QM_EOK;
}

static int iot_init(void* arg)
{
    miio_gatt_spec_init(on_property_set, on_property_get, on_action_invoke, 64, 16);
    miio_mesh_user_callback_register(on_property_set, on_property_get, on_action_invoke, user_state_process);
    return QM_EOK;
}

static int iot_report(void* map_handle, qm_tlv_ctx_t* tlv_ctx)
{
    qm_tlv_block_t* tlv_block = NULL;
    if (map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while (tlv_block){
        property_operation_t operation = { 0 };
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


static int iot_set(char* data, int len, void* map_handle, qm_tlv_ctx_t* tlv_ctx)
{

    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    qm_tlv_block_t* tlv_block = NULL;
    property_operation_t* operation = NULL;
    QM_LOGD(LOG_TAG, "-------iot_set-----------");
    operation = (property_operation_t*)data;

    tlv_block = qm_tlv_block_creat();
    if (tlv_block == NULL){
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


static int iot_get_req(char* data, int len, void* map_handle, qm_tlv_ctx_t* tlv_ctx)
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;
    qm_tlv_block_t* tlv_block = NULL;
    tlv_block = qm_tlv_block_creat();
    if (tlv_block == NULL){
        return -QM_ENOMEM;
    }
    property_operation_t* operation = NULL;
    operation = (property_operation_t*)data;

    iot_map_mi_t id_map_data = { 0 };
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return ret;
    }

    tlv_block->tag = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;

    qm_tlv_block_add(tlv_ctx, tlv_block);

    return QM_EOK;

}
static int iot_get_rsp(void* map_handle, qm_tlv_ctx_t* tlv_ctx, char* data, int* len)
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;
    qm_tlv_block_t* tlv_block = NULL;

    property_operation_t* operation = NULL;
    operation = (property_operation_t*)data;

    iot_map_mi_t id_map_data = { 0 };
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        return ret;
    }

    tlv_block = qm_tlv_block_find(tlv_ctx, id_map_data.attr_id);
    return _xj_to_miio_transformat(map_handle, tlv_block, operation);
}

#if CONFIG_IOT_GENERIC_ACTION_SUPPORT

static int iot_action_req(char* data, int len, void* map_handle, uint8_t* aid, qm_tlv_ctx_t* tlv_ctx)
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    int in_len = 0;
    action_operation_t* operation = NULL;
    operation = (action_operation_t*)data;
    iot_map_mi_t id_map_data = { 0 };

    id_map_data.type = 1;
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->aiid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        return ret;
    }

    *aid = id_map_data.attr_id;

    while (in_len < operation->in->size){

        qm_tlv_block_t* tlv_block = NULL;
        property_operation_t  prop_operation = { 0 };

        tlv_block = qm_tlv_block_creat();
        if (tlv_block == NULL){
            return -QM_ENOMEM;
        }

        prop_operation.siid = operation->siid;
        prop_operation.piid = operation->in->arguments[in_len].piid;
        prop_operation.value = operation->in->arguments[in_len].value;

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

static int iot_action_rsp(void* map_handle, uint8_t aid, qm_tlv_ctx_t* tlv_ctx, char* data, int* len)
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int out_len = 0;
    int ret = QM_EOK;
    qm_tlv_block_t* tlv_block = NULL;
    action_operation_t* out_operation = NULL;
    out_operation = (action_operation_t*)data;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while (tlv_block && tlv_ctx->count){
        property_operation_t operation = { 0 };
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation);
        if (ret < 0){
            break;
        }
        out_operation->out->size++;
        out_operation->out->arguments[out_len].piid = operation.piid;
        out_operation->out->arguments[out_len].value = operation.value;
        out_len++;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;

    return QM_EOK;
}
#endif

#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
static int iot_event(void* map_handle, uint8_t eid, qm_tlv_ctx_t* tlv_ctx)
{
    qm_tlv_block_t* tlv_block = NULL;
    if (map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    arguments_t* arguments = NULL;
    event_operation_t operation = { 0 };

    iot_map_mi_t id_map_data = { 0 };

    id_map_data.attr_id = eid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        return ret;
    }

    operation.eiid = id_map_data.xiid;
    operation.siid = id_map_data.siid;

    operation.arguments = arguments_new();
    if (operation.arguments == NULL){
        return -QM_ENOMEM;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    if (tlv_block == NULL || tlv_ctx->count == 0){
        ret = qm_iot_hal_adapter_event_report(&operation);
        return ret;
    }

    arguments = operation.arguments;

    while (tlv_block && tlv_ctx->count){
        uint16_t count = arguments->size;
        property_operation_t operation_tmp = { 0 };
        ret = _xj_to_miio_transformat(map_handle, tlv_block, &operation_tmp);
        if (ret != QM_EOK){
            break;
        }

        arguments->arguments[count].piid = operation_tmp.piid;
        arguments->arguments[count].value = operation_tmp.value;

#ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_event] piid %d", arguments->arguments[count].piid);
#endif
        if (arguments->size++ >= MAX_ARGUMENTS){
            ret = qm_iot_hal_adapter_event_report(&operation);
            if (ret != QM_EOK){
                return ret;
            }
            operation.arguments = arguments_new();
            if (operation.arguments == NULL){
                return -QM_ENOMEM;
            }
            arguments = operation.arguments;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    return qm_iot_hal_adapter_event_report(&operation);
}
#endif

static int iot_reset(void* arg)
{
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
    miio_system_restore();

    return QM_EOK;
}

 int iot_deinit(void *arg)
 {
    return QM_EOK;
 }


static iot_generic_hal_t  generic_hal_mi_mesh = {

    .init = iot_init,
    .report = iot_report,
    .set = iot_set,
    .get_req = iot_get_req,
    .get_rsp = iot_get_rsp,
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
    .event = iot_event,
#endif
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    .action_req = iot_action_req,
    .action_rsp = iot_action_rsp,
#endif
#if CONFIG_IOT_GENERIC_RAWDATA_SUPPORT
    .up_rawdata = NULL,
#endif
    .reset = iot_reset,
    .deinit = iot_deinit,

};

int iot_generic_hal_mi_init(void)
{

    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH, &generic_hal_mi_mesh);
}


static int _xj_to_miio_transformat(void* map_handle, qm_tlv_block_t* tlv_block, property_operation_t* operation)
{
    int len = 0;
    qm_err_t ret = QM_EOK;
    iot_map_mi_t id_map_data = { 0 };
    property_value_t* prop_value = NULL;

    if (tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    id_map_data.attr_id = tlv_block->tag;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        return -QM_EINVAL;
    }

    qm_tlv_block_data_type_set(tlv_block, id_map_data.type_id);
    switch (id_map_data.type_id){
        case QM_DTTV_TYPE_BOOL:
            {
                bool_t bool_value;
                qm_tlv_unpack_bool(tlv_block, &bool_value);
                prop_value = property_value_new_boolean(bool_value);

            }break;
        case QM_DTTV_TYPE_INT8:
            {
                int8_t int8_value;
                qm_tlv_unpack_int8(tlv_block, &int8_value);
                prop_value = property_value_new_char(int8_value);
            }
            break;
        case QM_DTTV_TYPE_UINT8:
            {
                uint8_t uint8_value;
                qm_tlv_unpack_uint8(tlv_block, &uint8_value);
                prop_value = property_value_new_uchar(uint8_value);
                // printk("\r\n---------uint8_value-------------- = %d\r\n", uint8_value);
            }break;
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
        case QM_DTTV_TYPE_INT16:
            {
                int16_t int16_value;
                qm_tlv_unpack_int16(tlv_block, &int16_value);
                prop_value = property_value_new_short(int16_value);

            }
            break;
        case QM_DTTV_TYPE_UINT16:
            {
                uint16_t uint16_value;
                qm_tlv_unpack_uint16(tlv_block, &uint16_value);
                prop_value = property_value_new_ushort(uint16_value);

            }break;
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
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
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
        case QM_DTTV_TYPE_STRING:
            {
                int temp_len = 0;
                char buff[64] = { 0 };
                char* pdata = NULL;

                qm_tlv_unpack_string(tlv_block, NULL, &temp_len);
                if (temp_len > 64){
                    pdata = (char*)qm_malloc(temp_len + 1);
                    if (pdata == NULL){
                        return -QM_ENOMEM;
                    }
                    memset(pdata, 0, temp_len + 1);
                    qm_tlv_unpack_string(tlv_block, pdata, &temp_len);
                    prop_value = property_value_new_string(pdata);
                    qm_free(pdata);
                    pdata = NULL;
                }
                else{
                    qm_tlv_unpack_string(tlv_block, buff, &temp_len);
                    prop_value = property_value_new_string(buff);
                }
            }break;
        case QM_DTTV_TYPE_ARRAY:
            {
                int temp_len = 0;
                char buff[8] = { 0 };
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
        case QM_DTTV_TYPE_STRING_ARRAY:            
{
                int temp_len = 0;
                char buff[64] = { 0 };
                char* pdata = NULL;
                char* p_strdata = NULL;
                qm_tlv_unpack_bytes(tlv_block, NULL, &temp_len);
                p_strdata = (char*)qm_malloc(temp_len * 2 + 1);
                if (p_strdata == NULL){
                    return -QM_ENOMEM;
                }
                memset(p_strdata, 0, temp_len * 2 + 1);
                if (temp_len > 64){
                    pdata = (char*)qm_malloc(temp_len);
                    if (pdata == NULL){
                        qm_free(p_strdata);
                        p_strdata = NULL;
                        return -QM_ENOMEM;
                    }
                    qm_tlv_unpack_bytes(tlv_block, pdata, &temp_len);
                    hex_to_strs(pdata, temp_len, p_strdata);
                    prop_value = property_value_new_string(p_strdata);
                    qm_free(pdata);
                    pdata = NULL;
                }                
else{
                    qm_tlv_unpack_bytes(tlv_block, buff, &temp_len);
                    hex_to_strs(buff, temp_len, p_strdata);
                    prop_value = property_value_new_string(p_strdata);
                }

                qm_free(p_strdata);
                p_strdata = NULL;
            }break;
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
        case QM_DTTV_TYPE_FLOAT32:
            {
                float32_t float_value;
                qm_tlv_unpack_float32(tlv_block, &float_value);
                prop_value = property_value_new_float(float_value);
            }break;
#endif
        default:
            break;

    }

    operation->siid = id_map_data.siid;
    operation->piid = id_map_data.xiid;
    operation->value = prop_value;

    return QM_EOK;
}

static int _miio_to_xj_transformat(void* map_handle, qm_tlv_block_t* tlv_block, property_operation_t* operation)
{
    if (tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    iot_map_mi_t id_map_data = { 0 };

    id_map_data.type = 0;
    id_map_data.siid = operation->siid;
    id_map_data.xiid = operation->piid;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t*)&id_map_data, sizeof(iot_map_mi_t));
    if (ret != QM_EOK){
        return -QM_EINVAL;
    }

    tlv_block->tag = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;

    switch (id_map_data.type_id){
        case QM_DTTV_TYPE_BOOL:
            qm_tlv_pack_bool(tlv_block, operation->value->data.boolean);
            break;
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
        case QM_DTTV_TYPE_STRING:
            {
                qm_tlv_pack_string(tlv_block, operation->value->data.string.value, operation->value->data.string.length);
            }break;
        case QM_DTTV_TYPE_ARRAY:
            {
                uint16_t bytes_len = 0;
                uint8_t bytes[10] = { 0 };
                uint64_t bytes_tmp = operation->value->data.number.integerValue;
                memcpy(bytes, &bytes_tmp, sizeof(uint64_t));
                bytes_len = strlen(bytes);
                bytes_tmp = cpu_to_be64(bytes_tmp) >> ((sizeof(uint64_t) - bytes_len) * 8);
                memcpy(bytes, &bytes_tmp, sizeof(uint64_t));
                qm_tlv_pack_bytes(tlv_block, bytes, bytes_len);
            }break;
        case QM_DTTV_TYPE_STRING_ARRAY:
            {
                uint16_t bytes_len = 0;
                uint8_t* pdata = NULL;
                uint8_t bytes[64] = { 0 };
                bytes_len = operation->value->data.string.length / 2 + operation->value->data.string.length % 2;
                if (bytes_len > 64){
                    pdata = (char*)qm_malloc(bytes_len);
                    if (pdata == NULL){
                        return -QM_ENOMEM;
                    }
                    hex_str_to_nums(operation->value->data.string.value,
                        operation->value->data.string.length,
                        pdata);
                    qm_tlv_pack_bytes(tlv_block, pdata, bytes_len);
                    qm_free(pdata);
                    pdata = NULL;
                }
                else{
                    hex_str_to_nums(operation->value->data.string.value,
                        operation->value->data.string.length,
                        bytes);
                    qm_tlv_pack_bytes(tlv_block, bytes, bytes_len);
                }
            }break;
#endif
        case QM_DTTV_TYPE_INT8:
            {
                qm_tlv_pack_int8(tlv_block, operation->value->data.number.charValue);
            }break;
        case QM_DTTV_TYPE_UINT8:
            {
                qm_tlv_pack_uint8(tlv_block, operation->value->data.number.ucharValue);
            }break;
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
        case QM_DTTV_TYPE_INT16:
            {
                qm_tlv_pack_int16(tlv_block, operation->value->data.number.shortValue);
            }break;
        case QM_DTTV_TYPE_UINT16:
            {
                qm_tlv_pack_uint16(tlv_block, operation->value->data.number.ushortValue);
            }break;
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
        case QM_DTTV_TYPE_INT32:
            {
                qm_tlv_pack_int32(tlv_block, operation->value->data.number.longValue);
            }break;
        case QM_DTTV_TYPE_UINT32:
            {
                qm_tlv_pack_uint32(tlv_block, operation->value->data.number.ulongValue);
            }break;
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
        case QM_DTTV_TYPE_FLOAT32:
            {
                qm_tlv_pack_float32(tlv_block, operation->value->data.number.floatValue);
            }break;
#endif   
        default:
            break;
    }
    return QM_EOK;
}
