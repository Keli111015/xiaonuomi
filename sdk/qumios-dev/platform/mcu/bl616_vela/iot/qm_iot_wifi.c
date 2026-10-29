#include "iot_generic_hal.h"

#include "qm.h"
#include "qm_work.h"

#include "id_map.h"
#include "qm_tlv.h"

#include "qm_utils_string.h"

#include "operation_code.h"
#include "operation_executor.h"
#include "../typedef/action_operation.h"

#include "xj_operation_typedef.h"
#include "xj_operation_executor.h"

#include "qm_utils_timer.h"

#include "miio_net.h"
#include "miio_api.h"

#include "qm.h"
#include "syslog.h"

#define LOG_TAG "log"

#define MAX_ARGUMENTS               6
#define MIIO_REPORT_MAX_NUM         8
#define REPORT_DELAY_TIMER_MS      (100)
#define REPORT_MERGE_TIMER_MS      (500)

typedef struct 
{   
    qm_work_t report_work;
    qm_mutex_t report_mutex;
    qm_tlv_ctx_t    report_tlv_ctx;
}qm_hal_iot_handle_t;

typedef struct 
{   
    int count;
    qm_utils_time_t timeout;
}qm_hal_report_handle_t;

// uint32_t xj_hal_miio_pid = CONFIG_MIBLE_BT_PRODUCT_ID;
// char xj_hal_miio_model[65] = {CONFIG_MIIO_INSTANCE_MODEL};

static qm_hal_iot_handle_t  g_iot_handle_info = {0};

static int _xj_to_miio_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation);
static int _xj_to_miio_transformat_report(void *map_handle, qm_tlv_block_t *tlv_block,  xj_property_operation_t *operation);

static int iot_init(void *arg);
static int iot_reset(void *arg);
static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);

#if CONFIG_MIIO_SPEC_USER_HANDLE
extern miio_handle_t g_miio_instance_handle;
#else
#error "not config MIIO_SPEC_USER_HANDLE"
#endif


#ifndef CONFIG_QM_WORK_SUPPORT
#error "not config QM_WORK_SUPPORT"
#endif


static int iot_init(void *arg)
{   
    qm_mutex_new(&g_iot_handle_info.report_mutex);
    return QM_EOK;
}

static int iot_reset(void *arg)
{
    iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
    miio_restore(g_miio_instance_handle, "user.restore");
    miio_reboot(g_miio_instance_handle, "user.restore", 0);
    return QM_EOK;
}


//

static void report_delay_post_callback(void *arg)
{
    int snapshot_count = 0;
    int ret = QM_EOK;
    void *map_handle = arg;
    qm_tlv_ctx_t  *tlv_ctx = NULL;
    qm_tlv_block_t  *tlv_block = NULL;
    qm_tlv_block_t  *tlv_block_tmp = NULL;
    qm_tlv_block_t  *dest_tlv_block = NULL;
    qm_hal_report_handle_t *report_handle = NULL;
    xj_property_operation_t operation[MIIO_REPORT_MAX_NUM] = {0};

    if(map_handle == NULL){
        return;
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return;
    }

    qm_cancel_delayed_action(&g_iot_handle_info.report_work);

    qm_mutex_lock(&g_iot_handle_info.report_mutex, QM_WAIT_FOREVER);

    tlv_block = qm_tlv_block_next(&g_iot_handle_info.report_tlv_ctx, NULL);
    while(tlv_block){

        report_handle = (qm_hal_report_handle_t *)tlv_block->arg;

        if((report_handle->count > 1) && !qm_utils_time_is_expired(&report_handle->timeout)){
            continue;
        }

        tlv_block_tmp = qm_tlv_block_creat();
        if(tlv_block_tmp == NULL){
            break;
        }

        qm_tlv_block_copy(tlv_block_tmp, tlv_block);

        ret = _xj_to_miio_transformat_report(map_handle, tlv_block_tmp, (operation + tlv_ctx->count));
        if (ret != QM_EOK){
            continue;
        }

        qm_tlv_block_arg_set(tlv_block_tmp, (operation + tlv_ctx->count));
        qm_tlv_block_add(tlv_ctx, tlv_block_tmp);

    //节省资源删除缓存区
        dest_tlv_block = tlv_block;
        tlv_block = qm_tlv_block_next(&g_iot_handle_info.report_tlv_ctx, dest_tlv_block);
        qm_tlv_block_remove(&g_iot_handle_info.report_tlv_ctx, dest_tlv_block);
        qm_free(report_handle);
        report_handle = NULL;
    //
        snapshot_count = g_iot_handle_info.report_tlv_ctx.count;
        if(tlv_ctx->count >= MIIO_REPORT_MAX_NUM){
            break;
        }
    }
    qm_mutex_unlock(&g_iot_handle_info.report_mutex);

    xj_send_property_changed(g_miio_instance_handle, tlv_ctx);
    qm_tlv_ctx_delete(tlv_ctx);
    tlv_ctx = NULL;

    if(snapshot_count){
        qm_post_delayed_action(&g_iot_handle_info.report_work, report_delay_post_callback, map_handle, REPORT_MERGE_TIMER_MS);    
    }
}

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};
    qm_tlv_block_t  *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    qm_hal_report_handle_t *report_handle = NULL;
    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    qm_cancel_delayed_action(&g_iot_handle_info.report_work);
    
    qm_mutex_lock(&g_iot_handle_info.report_mutex, QM_WAIT_FOREVER);
    while (1)
    {
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
        if(tlv_block == NULL){
            break;
        }

        id_map_data.attr_id = tlv_block->tag;
        ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
        if(ret != QM_EOK){
            continue;
        }

        m_tlv_block = qm_tlv_block_find(&g_iot_handle_info.report_tlv_ctx, (uint16_t)tlv_block->tag);
        if(m_tlv_block){
            report_handle = (qm_hal_report_handle_t *)m_tlv_block->arg;
            qm_tlv_block_copy(m_tlv_block, tlv_block);
        }else{
            m_tlv_block = qm_tlv_block_creat();
            if(m_tlv_block == NULL){
                break;
            }
            report_handle = (qm_hal_report_handle_t *)qm_malloc(sizeof(qm_hal_report_handle_t));
            if(report_handle == NULL){
                qm_tlv_block_delete(m_tlv_block);
                m_tlv_block = NULL;
                break;
            }
            memset(report_handle, 0, sizeof(qm_hal_report_handle_t));
            qm_tlv_block_copy(m_tlv_block, tlv_block);
            qm_tlv_block_add(&g_iot_handle_info.report_tlv_ctx, m_tlv_block);
        }

        m_tlv_block->arg = report_handle;
        if((++report_handle->count) <= 1){
           qm_utils_time_countdown_ms(&report_handle->timeout, REPORT_MERGE_TIMER_MS); 
        }
    }
    qm_mutex_unlock(&g_iot_handle_info.report_mutex);

    qm_post_delayed_action(&g_iot_handle_info.report_work, report_delay_post_callback, map_handle, REPORT_DELAY_TIMER_MS);    

    return QM_EOK;
}

void xj_miio_net_indicator_set(miio_net_state_t state)
{
    static miio_net_state_t is_uprov = MIIO_NET_UNPROV; 

    switch (state)
    {
        case MIIO_NET_UNPROV:
            
            is_uprov = MIIO_NET_UNPROV;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONFIG_TIMEOUT);
        break;
        case MIIO_NET_UAP:
            if(is_uprov == MIIO_NET_CLOUD){
                is_uprov = MIIO_NET_UNPROV;
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_UNREGISTER);
            }else{
                is_uprov = MIIO_NET_UAP;
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_WIFI_CONFIG);
            }
            is_uprov = state;
        break;
        case MIIO_NET_DISCONNECTED:
            if(is_uprov != MIIO_NET_UAP){
                is_uprov = MIIO_NET_DISCONNECTED;
            }
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTING_ROUTER);
        break;
        case MIIO_NET_LOCAL:
            if(is_uprov != MIIO_NET_UAP){
                is_uprov = MIIO_NET_LOCAL;
            }
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTED_ROUTER);
        break;
        case MIIO_NET_CLOUD:
            if(is_uprov == MIIO_NET_UAP){
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_REGISTERED);
            }else{
                iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_CONNECTED_CLOUD);
            }
            is_uprov = MIIO_NET_CLOUD;
        break;
        case MIIO_NET_UPDATING:
            is_uprov = MIIO_NET_UPDATING;
            iot_generic_event_notify(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, IOT_GENERIC_EVENT_DEVICE_OTA_START);
        break;	
        default:
        break;
    }

}

void on_property_set(property_operation_t *o)
{
    int ret = QM_EOK;

    QM_LOGD(LOG_TAG, "on_property_set: did = [%s], siid = [%d], piid = [%d]\n", o->did, o->siid, o->piid);
    syslog(LOG_ERR, "on_property_set: did = [%s], siid = [%d], piid = [%d]\n", o->did, o->siid, o->piid);
    if(o->siid == 11 && o->piid == 3)
    {
        QM_LOGD(LOG_TAG, "on_property_set: value type = %d\n", o->value->data.number.type);
        QM_LOGD(LOG_TAG, "on_property_set: value = %d\n", o->value->data.number.value.integerValue);
    }
    ret = iot_generic_set(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t));
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_CANNOT_WRITE;
    }else{
        o->code = OPERATION_OK;
    }
}

static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)    
{
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t *operation = (property_operation_t *)data;  

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

    ret = qm_tlv_block_add(tlv_ctx, tlv_block); 
    
    return QM_EOK;
}

void on_property_get(property_operation_t *o)
{
    QM_LOGD(LOG_TAG, "on_property_get: did = [%s], siid = [%d], piid = [%d]\n", o->did, o->siid, o->piid);

    int out_len = 0;
    int ret = QM_EOK;
    ret = iot_generic_get(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(property_operation_t), (char *)o , &out_len);
    syslog(LOG_ERR, "on_property_get: did = [%s], siid = [%d], piid = [%d] ret:%d\n", o->did, o->siid, o->piid, ret);
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_CANNOT_READ;
    }else{
        o->code = OPERATION_OK;
    }

    if(o->value->format == PROPERTY_FORMAT_STRING){
        QM_LOGD(LOG_TAG, "get rsp: str value:%s ", o->value->data.string);
    }

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

void on_action_invoke(action_operation_t *o)
{
    o->code = 0;
    int out_len = 0;
    int ret = QM_EOK;
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    o->code = iot_generic_action(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, (char *)o , sizeof(action_operation_t), (char *)o , &out_len);
#endif
    if(ret != QM_EOK){
        o->code = OPERATION_ERROR_VALUE;
    }
}
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
static int iot_action_req(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    int in_len = 0;
    iot_map_mi_t id_map_data = {0};
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t  prop_operation = {0};
    action_operation_t *operation = (action_operation_t *)data;

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
    
    while(in_len < operation->in->size){
            
        memset(&prop_operation, 0, sizeof(property_operation_t));

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
    int out_len = 0;
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    property_operation_t operation = {0};
    action_operation_t *out_operation = (action_operation_t *)data;

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);

    while(tlv_block && tlv_ctx->count){

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

int qm_iot_hal_wifi_adapter_event_report(event_operation_t  *operation)
{
    int ret = QM_EOK;
    
    if (operation == NULL){
        return -QM_EINVAL;
    }

    arch_u64toa(arch_get_did_ex(), operation->did);
    ret = send_event_occurred(g_miio_instance_handle, operation);
    if(operation->arguments){
        arguments_delete(operation->arguments);
        operation->arguments = NULL;
    }
    return ret;
}

static int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    arguments_t *arguments = NULL;
    iot_map_mi_t id_map_data = {0};
    qm_tlv_block_t *tlv_block = NULL;
    event_operation_t operation = {0};

    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

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

    return ret;
}
#endif


static int miio_ble_config_change(uint16_t product_id, uint16_t version)
{
	int ret = -1;

    #define MIIO_BLE_VER_LEN			10
    #define MIIO_BLE_NAME_LEN			10

	struct {
		uint16_t product_id;
		uint8_t version[MIIO_BLE_VER_LEN];
		uint8_t name[MIIO_BLE_NAME_LEN];
	} tmp;
	tmp.product_id = product_id;

	ret = arch_psm_set_value("ble", "info", &tmp, sizeof(tmp));
	if (sizeof(tmp) != ret) {
        return -1;
    }
    
	ret = arch_psm_set_value("ble", "version", &version, sizeof(version));
	if (sizeof(version) != ret) {
        return -1;
    }
	return 0;
}

static int iot_option_set(iot_generic_option_t option, void *data, int len)
{
    //TODO: enable
#if 0
    uint16_t version = 1;
    
    
    switch (option)
    {
        case IOT_OPTION_PRODUCT_ID:
            if(len != sizeof(uint32_t)){
                break;
            }
            memcpy(&xj_hal_miio_pid, data, len);
            QM_LOGD("1","XJ MIIO PID %d", xj_hal_miio_pid);
            miio_ble_config_change(xj_hal_miio_pid, MIIO_APP_VERSION_NUMBER);
        break;
        case IOT_OPTION_MODEL:
            memset(xj_hal_miio_model, 0, sizeof(xj_hal_miio_model));
            memcpy(xj_hal_miio_model, data, len);
            QM_LOGD("1","XJ MIIO MODEL %s", xj_hal_miio_model);
        break;
        default:
        break;
    }
#endif

    return QM_EOK;
}

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
    .option_set = iot_option_set,
    .deinit = NULL,
};

int iot_generic_hal_mi_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_WIFI, &generic_hal_mi_wifi);
}


static int _miio_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  property_operation_t *operation)
{

    int ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};
    
    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

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
    
    int ret = QM_EOK;
    iot_map_mi_t id_map_data = {0};
    property_value_t *prop_value = NULL;

    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    id_map_data.attr_id = tlv_block->tag;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_mi_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

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
            prop_value = property_value_new_uinteger(uint8_value);

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
            prop_value = property_value_new_uinteger(uint16_value);

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
            prop_value = property_value_new_uinteger(uint32_value);
        }
        break;
        case QM_DTTV_TYPE_STRING:
        {
            int string_len = 0;
            char *string = NULL;
            qm_tlv_unpack_string_direct(tlv_block, &string, &string_len);
            prop_value = property_value_new_string(string);
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
            char *p_strbuf = NULL;
            uint8_t *p_strdata = NULL;
            qm_tlv_unpack_bytes_direct(tlv_block, &p_strdata, &temp_len);
            p_strbuf = (char *)qm_malloc(temp_len * 2 + 1);
            if(p_strbuf == NULL){
                return -QM_ENOMEM;
            }
            memset(p_strbuf, 0, temp_len * 2 + 1);

            hex_to_strs(p_strdata, temp_len, p_strbuf);  
            prop_value = property_value_new_string(p_strbuf);
            qm_free(p_strbuf);
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

static int _xj_to_miio_transformat_report(void *map_handle, qm_tlv_block_t *tlv_block,  xj_property_operation_t *operation)
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

    operation->siid  = id_map_data.siid;
    operation->piid  = id_map_data.xiid;
    qm_tlv_block_data_type_set(tlv_block, id_map_data.type_id);

    return QM_EOK;
}


