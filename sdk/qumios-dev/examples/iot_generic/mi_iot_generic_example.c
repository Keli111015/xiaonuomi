#include "qm.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "id_map.h"
#include "iot_generic.h"

#define LOG_TAG "iot_log"

static  void *qm_iot_handle = NULL;

#if 0
static uint8_t xj_mi_list[] =  //对应小匠串口协议小米映射关系 : type_id + attr_id + siid + server_type + xiid(2 bytes)
{
0x00 ,0x00 ,0x02 ,0x00 ,0x00 ,0x01 ,
0x02 ,0x01 ,0x02 ,0x00 ,0x00 ,0x02 ,
0x04 ,0x02 ,0x02 ,0x00 ,0x00 ,0x03 ,
0x06 ,0x03 ,0x02 ,0x00 ,0x00 ,0x04 ,
0x02 ,0x04 ,0x02 ,0x00 ,0x00 ,0x05 ,
0x0E ,0x05 ,0x02 ,0x01 ,0x00 ,0x01 ,
0x02 ,0x06 ,0x03 ,0x00 ,0x00 ,0x01 ,
0x02 ,0x07 ,0x03 ,0x00 ,0x00 ,0x02 ,
0x02 ,0x08 ,0x03 ,0x00 ,0x00 ,0x03 ,
0x00 ,0x09 ,0x04 ,0x00 ,0x00 ,0x01 ,
0x02 ,0x0A ,0x04 ,0x00 ,0x00 ,0x02 ,
0x0E ,0x0B ,0x04 ,0x01 ,0x00 ,0x02 ,
0x0E ,0x0C ,0x04 ,0x01 ,0x00 ,0x03 ,
0x0E ,0x0D ,0x04 ,0x01 ,0x00 ,0x04 ,
0x0E ,0x0E ,0x04 ,0x01 ,0x00 ,0x05 ,
0x0E ,0x0F ,0x04 ,0x01 ,0x00 ,0x06 ,
0x0E ,0x10 ,0x04 ,0x01 ,0x00 ,0x07 ,
0x0E ,0x11 ,0x04 ,0x01 ,0x00 ,0x08 ,
0x0E ,0x12 ,0x04 ,0x01 ,0x00 ,0x09 ,
0x0E ,0x13 ,0x04 ,0x01 ,0x00 ,0x0A ,
0x0E ,0x14 ,0x04 ,0x01 ,0x00 ,0x0B ,
0x0E ,0xFE ,0x00 ,0x08 ,0x02 ,0x00 ,
0x00 ,0x01 ,0x02 ,0x00 ,0x00 ,0x02 ,
};
#endif

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
                tlv_block_report  = qm_tlv_block_creat();
                if(tlv_block_report == NULL){
                    return -QM_ENOMEM;
                }
                qm_tlv_block_copy(tlv_block_report, tlv_block);
                iot_generic_report(qm_iot_handle, tlv_block_report); // 内部会自行释放 tlv_block_report
            }break;
#if CONFIG_QM_DTTV_BYTES_SUPPORT
            case QM_DTTV_TYPE_STRING:{
                int len = 0;
                char *value = NULL;
                qm_tlv_unpack_string(tlv_block, NULL,&len);
                value = qm_malloc(len);
                qm_tlv_unpack_string(tlv_block, value, len);
            #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_set] XJIANG attr_id %d type %d value %s", tlv_block->tag , tlv_block->type , value);
            #endif        
                tlv_block_report  = qm_tlv_block_creat();
                if(tlv_block_report == NULL){
                    return -QM_ENOMEM;
                }
                qm_tlv_block_copy(tlv_block_report, tlv_block);
                iot_generic_report(qm_iot_handle, tlv_block_report); // 内部会自行释放 tlv_block_report
            }break;
#endif
#if CONFIG_QM_DTTV_FLOAT32_SUPPORT

            case QM_DTTV_TYPE_FLOAT32:{
                float32_t value;
                qm_tlv_pack_float32(tlv_block, value);
            #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_set] XJIANG attr_id %d type %d value %d", tlv_block->tag , tlv_block->type , value);
            #endif   
                tlv_block_report  = qm_tlv_block_creat();
                if(tlv_block_report == NULL){
                    return -QM_ENOMEM;
                }
                qm_tlv_block_copy(tlv_block_report, tlv_block);
                iot_generic_report(qm_iot_handle, tlv_block_report); // 内部会自行释放 tlv_block_report
                     
            }break;
#endif
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
#if CONFIG_QM_DTTV_BYTES_SUPPORT

            case QM_DTTV_TYPE_STRING:{
                qm_tlv_pack_string(tlv_block, "hello word !!",strlen("hello word !!"));
            }break;
            case QM_DTTV_TYPE_STRING_ARRAY:{ // 暂未实现
                
            }break;
            case QM_DTTV_TYPE_INT8 : {
                qm_tlv_pack_int8(tlv_block, -10);   
            }break;
#endif
#if CONFIG_QM_DTTV_FLOAT32_SUPPORT
            case QM_DTTV_TYPE_FLOAT32:{
                float32_t temp = -1.6 * 10;
                qm_tlv_pack_float32(tlv_block, temp);   
            }break;  
#endif
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
            case QM_DTTV_TYPE_BOOL:{
                bool_t value;
                qm_tlv_unpack_bool(tlv_block_in, &value);
        #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_action_IN] attr_id %d type %d value %d", tlv_block_in->tag , tlv_block_in->type , value);
        #endif 
            }break;
#if CONFIG_QM_DTTV_BYTES_SUPPORT

            case QM_DTTV_TYPE_STRING:{
                int in_len = 0;
                char in_buff[32] = {0};
                qm_tlv_unpack_string(tlv_block_in, in_buff,&in_len);
        #ifdef LOG_TAG
            QM_LOGD(LOG_TAG, "[iot_hook_action_IN]  attr_id %d type %d value %s", tlv_block_in->tag , tlv_block_in->type , in_buff);
        #endif
            }break;
#endif
        default:
            break;
        }  
        tlv_block_in = qm_tlv_block_next(action , tlv_block_in);
    }
#if 0
    tlv_block_out = qm_tlv_block_creat();
    if(tlv_block_out == NULL){
        return -QM_ENOMEM;
    }

    while (out_index < sizeof(xj_mi_list))
    {
        tlv_block_out->type = xj_mi_list[out_index];
        tlv_block_out->tag  = xj_mi_list[out_index + 1];
        switch(tlv_block_out->type)
        {
            case QM_DTTV_TYPE_BOOL:{
                bool_t value = QM_FALSE;
                qm_tlv_pack_bool(tlv_block_out, value);
        #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_action_OUT]  attr_id %d type %d value %d", tlv_block_out->tag , tlv_block_out->type , value);
        #endif  
            }break;
            case QM_DTTV_TYPE_STRING:{
                char out_buff[] = "iot_hook_action_out";
                int in_len = strlen(out_buff);
                qm_tlv_pack_string(tlv_block_out, out_buff, in_len);
        #ifdef LOG_TAG
                QM_LOGD(LOG_TAG, "[iot_hook_action_OUT]  attr_id %d type %d value %s", tlv_block_out->tag , tlv_block_out->type , out_buff);
        #endif
            }break;
            case QM_DTTV_TYPE_STRING_ARRAY:{ // 暂未实现
                
            }break;
        default:
            break;
        }          
        out_index += 6;
        qm_tlv_block_add(action_rsp, tlv_block_out);
    }
#endif
    return QM_EOK;
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
static iot_generic_hooks_t mi_hook = {
    .set = iot_hook_set,
    .get = iot_hook_get,
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    .action = iot_hook_action,
#endif
    .event_notify = iot_hook_event_notify,
    .ctx = NULL,
};

static int miio_map_parse_set(  iot_map_ctx_t *map_ctx,
                                uint8_t attr_id,
                                uint8_t type_id, 
                                uint8_t siid,
                                uint8_t server_type, 
                                uint16_t xiid)

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
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "qm_application_start!!!!");
#endif   

    iot_map_ctx_t *map_ctx = NULL;
    qm_iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_MESH);
    iot_generic_hooks_register(qm_iot_handle, &mi_hook);

    map_ctx = (iot_map_ctx_t *)iot_generic_map_init(qm_iot_handle, 10);
    if(map_ctx == NULL){
        return;
    }

#if CONFIG_IOT_MAP_VERSION_V2
    miio_map_parse_set(map_ctx, 0, QM_DTTV_TYPE_BOOL, 2, 0, 1);
    miio_map_parse_set(map_ctx, 1, QM_DTTV_TYPE_INT8, 2, 0, 2);
    miio_map_parse_set(map_ctx, 2, QM_DTTV_TYPE_BOOL, 2, 0, 3);
    miio_map_parse_set(map_ctx, 3, QM_DTTV_TYPE_BOOL, 2, 0, 4);
    miio_map_parse_set(map_ctx, 4, QM_DTTV_TYPE_BOOL, 2, 0, 5);
    miio_map_parse_set(map_ctx, 5, QM_DTTV_TYPE_BOOL, 2, 0, 6);
    miio_map_parse_set(map_ctx, 6, QM_DTTV_TYPE_BOOL, 2, 0, 7);
    miio_map_parse_set(map_ctx, 7, QM_DTTV_TYPE_BOOL, 2, 0, 8); 
    miio_map_parse_set(map_ctx, 8, QM_DTTV_TYPE_BOOL, 2, 0, 9);
    miio_map_parse_set(map_ctx, 9, QM_DTTV_TYPE_FLOAT32, 3, 0, 1);
#else
    uint8_t xj_mi_list[] =  //对应小匠串口协议小米映射关系 : type_id + attr_id + siid + server_type + xiid(2 bytes)
    {
        0x00 ,0x00 ,0x02 ,0x00 ,0x00 ,0x01 ,
        0x02 ,0x01 ,0x02 ,0x00 ,0x00 ,0x02 ,
        0x04 ,0x02 ,0x02 ,0x00 ,0x00 ,0x03 ,
        0x06 ,0x03 ,0x02 ,0x00 ,0x00 ,0x04 ,
        0x02 ,0x04 ,0x02 ,0x00 ,0x00 ,0x05 ,
        0x0E ,0x05 ,0x02 ,0x01 ,0x00 ,0x01 ,
        0x02 ,0x06 ,0x03 ,0x00 ,0x00 ,0x01 ,
        0x02 ,0x07 ,0x03 ,0x00 ,0x00 ,0x02 ,
        0x02 ,0x08 ,0x03 ,0x00 ,0x00 ,0x03 ,
        0x00 ,0x09 ,0x04 ,0x00 ,0x00 ,0x01 ,
    };
    iot_generic_map_set(qm_iot_handle, xj_mi_list, sizeof(xj_mi_list));  //内部会自行分配内存因此只需局部变量导入即可
#endif
}
