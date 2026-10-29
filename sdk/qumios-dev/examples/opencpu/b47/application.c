#include "prodtst_client.h"
#include "prodtst_client_common.h"

#include "id_map.h"
#include "iot_generic.h"

#include "miio_user_api.h"

#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"

#define LOG_TAG "EXAMPLE"

static void *iot_handle = NULL;
static void *prodtst_handle = NULL;

static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static int iot_hook_action(uint8_t aid, qm_tlv_ctx_t *action, qm_tlv_ctx_t *action_rsp, void *ctx);
static int miio_map_parse_set(iot_map_ctx_t *map_ctx, uint8_t attr_id, uint8_t type_id, 
                                uint8_t siid, uint8_t server_type, uint16_t xiid);

int hal_fwname_verify(char *data, uint16_t len) //用于产测盒子向设备进行版本号校验
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    
    if(!memcmp(data, "WB60.M000.3007.14", strlen("WB60.M000.3007.14"))){
        // 版本号不一致 设备执行相应功能
    }else{
        // 版本号一致 设备执行相应功能
    }

    //返回结果默认为 QM_EOK,当返回结果不为QM_EOK时,组件包会将实际数据完整返回给平台
    return QM_EOK;
}

int hal_get_fwname(char *data, uint16_t len) //用于产测盒子向设备获取固件版本
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    memcpy(data, "WB60.M000.3007.14", strlen("WB60.M000.3007.14"));
    return QM_EOK;
}

static int mi_pid_setup(uint32_t product_id, char model[65])  //米家PID MODEL的写入与校验功能
{
    //先从设备内部中获取米家pid和model以及是否被绑定，若设备未被绑定并且pid和model与写入的不相同，则进行pid model更新

    return QM_AT_RESULT_OK;
}

static int mi_is_use_get(void)
{
    return 0;
}

static int mi_pid_get(void)
{
    return 18243;
}

static int mi_model_get(char model[65])
{
    memcpy(model, "qjiang.switch.b12sw4", strlen("qjiang.switch.b12sw4"));
    return QM_EOK;
}

static prodtst_client_generic_cmd_t generic_hal =
{
    .fwname_get = hal_get_fwname,
    .fwname_verify = hal_fwname_verify, 
#if CONFIG_PRODTST_CLIENT_AT_BLE
    .ble_mac_get = NULL,
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    .mi_pid_model_set = mi_pid_setup,
    .mi_pid_get = mi_pid_get,
    .mi_model_get = mi_model_get,
    .mi_is_use_get = mi_is_use_get,
#endif
};

static  qm_at_result_t led_setup(void *channel, const char *args)
{
    return QM_AT_RESULT_OK;
}


static qm_at_cmd_t prodtst_client_func_cmd[] = {  
    {"AT+LED", NULL, NULL, NULL, led_setup, NULL},
};

void prodtst_client_event_callback(prodtst_client_event_t event)
{
    switch (event)
    {
        case PRODTST_CLIENT_ENTER_PRODTST_SUCCESS:
            QM_LOGD(QM_LOG_TAG, "PRODTST_CLIENT_ENTER_PRODTST_SUCCESS!!!"); 
            miio_system_set_adv_timeout(0); //进入产测后用于关闭米家的mesh广播
        break;
        case PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT:
            QM_LOGD(QM_LOG_TAG, "PRODTST_CLIENT_ENTER_PRODTST_TIMEOUT!!!");
            prodtst_client_deinit(prodtst_handle); //产测超时后反初始化设备
        break;  
        case PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT:
            QM_LOGD(QM_LOG_TAG, "PRODTST_CLINET_PRODTST_TEST_RECV_TIMEOUT!!!");
        break;  

        case PRODTST_CLIENT_EXIT_PRODTST_SUCCESS:
            QM_LOGD(QM_LOG_TAG, "PRODTST_CLIENT_EXIT_PRODTST_SUCCESS!!!");
        break; 

        default:
        break;
    }
}

static void iot_hook_event_notify(iot_generic_event_t event, void *ctx)
{

    switch (event)
    {
    case IOT_GENERIC_EVENT_WIFI_CONFIG:
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_WIFI_CONFIG");
#endif        
    break;
    case IOT_GENERIC_EVENT_CONNECTING_ROUTER:
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_ROUTER");
#endif 
    break;
    case IOT_GENERIC_EVENT_CONNECTED_ROUTER:
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_ROUTER");
#endif 
    break;
    case IOT_GENERIC_EVENT_CONNECTED_CLOUD:
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_CLOUD");
#endif 
    break; 
    case IOT_GENERIC_EVENT_CONFIG_TIMEOUT:
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONFIG_TIMEOUT");
#endif 
    break; 
    default:
    break;
    }
}

static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *tlv_block_report = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx , NULL);
    while (tlv_block)
    {
        switch(tlv_block->type)
        {
            case QM_DTTV_TYPE_BOOL:{
                bool_t value;
                qm_tlv_unpack_bool(tlv_block, &value);
            #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_set] XJIANG attr_id %d type %d value %d", tlv_block->tag , tlv_block->type , value);
            #endif        
            }break;

            case QM_DTTV_TYPE_UINT8:{
                uint8_t value;
                qm_tlv_unpack_uint8(tlv_block, &value);
            #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_set] XJIANG attr_id %d type %d value %d", tlv_block->tag , tlv_block->type , value);
            #endif        
            }break;
            default:
            break;
        }       
        tlv_block = qm_tlv_block_next(tlv_ctx , tlv_block);
    }
    
    return QM_EOK;
}

static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx , NULL);
    while (tlv_block)
    {
    #ifdef LOG_TAG
        QM_LOGD(LOG_TAG, "[iot_hook_get] XJIANG attr_id %d ", tlv_block->tag);
    #endif  
        switch(tlv_block->type)
        {
            case QM_DTTV_TYPE_BOOL:
                qm_tlv_pack_bool(tlv_block, QM_TRUE);
            break;

            case QM_DTTV_TYPE_INT8 : 
                qm_tlv_pack_int8(tlv_block, -10);   
            break;

            case QM_DTTV_TYPE_UINT8 : 
                qm_tlv_pack_uint8(tlv_block, tlv_block->tag % 2);   
            break;
            default:
            break;
        }      
        tlv_block = qm_tlv_block_next(tlv_ctx , tlv_block);
    }

    return QM_EOK;
}

static int iot_hook_action(uint8_t aid, qm_tlv_ctx_t *action, qm_tlv_ctx_t *action_rsp, void *ctx)
{
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "[iot_hook_action] aid : %d", aid);
#endif   
    int out_index = 0;
    qm_tlv_block_t *tlv_block_in = NULL;
    qm_tlv_block_t *tlv_block_out = NULL;
    tlv_block_in = qm_tlv_block_next(action , NULL);
    while (tlv_block_in)
    {
        switch(tlv_block_in->type)
        {
            case QM_DTTV_TYPE_BOOL:
            {
                bool_t value;
                qm_tlv_unpack_bool(tlv_block_in, &value);
        #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_action_IN] attr_id %d type %d value %d", tlv_block_in->tag , tlv_block_in->type , value);
        #endif 
            }break;

            default:
            break;
        }  
        tlv_block_in = qm_tlv_block_next(action , tlv_block_in);
    }
    return QM_EOK;
}

static iot_generic_hooks_t mi_hook = {
    .set = iot_hook_set,
    .get = iot_hook_get,
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    .action = iot_hook_action,
#endif
    .event_notify = iot_hook_event_notify,
    .ctx = NULL,
};

static int miio_map_parse_set(iot_map_ctx_t *map_ctx, uint8_t attr_id, uint8_t type_id, 
                                uint8_t siid, uint8_t server_type, uint16_t xiid)

{
    iot_map_mi_t *map_mi = NULL;
    if(map_ctx == NULL){
        return -QM_EINVAL;
    }

    map_mi = (iot_map_mi_t *)iot_generic_map_get((void *)map_ctx);
    if(map_mi == NULL){
        return;
    }    

    map_mi->type_id = type_id;
    map_mi->attr_id = attr_id;
    map_mi->siid    = siid;
    map_mi->type = server_type;
    map_mi->xiid = xiid;

    return QM_EOK;
}

void qm_application_start(void)
{   
    uint32_t pid = 18243;

    iot_map_ctx_t *map_ctx = NULL;
    QM_LOGD("TEST", "qm_application_start !!");

    iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH);
    iot_generic_hooks_register(iot_handle, &mi_hook);

    iot_generic_option_set(iot_handle, IOT_OPTION_MODEL, "qjiang.switch.b12sw4", strlen("qjiang.switch.b12sw4"));
    iot_generic_option_set(iot_handle, IOT_OPTION_PRODUCT_ID, &pid, sizeof(uint32_t));

    map_ctx = (iot_map_ctx_t *)iot_generic_map_init(iot_handle, 4);
    if(map_ctx == NULL){
        return;
    }

    miio_map_parse_set(map_ctx, 0, QM_DTTV_TYPE_BOOL, 2, 0, 1);
    miio_map_parse_set(map_ctx, 1, QM_DTTV_TYPE_UINT8, 2, 0, 2);
    miio_map_parse_set(map_ctx, 2, QM_DTTV_TYPE_UINT8, 3, 0, 2);
    miio_map_parse_set(map_ctx, 3, QM_DTTV_TYPE_UINT8, 4, 0, 2);


    prodtst_client_generic_cmd_register(&generic_hal);
    prodtst_client_event_notify_register(prodtst_client_event_callback);

    //照明行业的产测类型为 PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH
    prodtst_handle = prodtst_client_init(PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH, prodtst_client_func_cmd, QM_ARRAY_SIZE(prodtst_client_func_cmd));
}

