#include "qm.h"
#include "qm_tlv.h"

#include "id_map.h"
#include "iot_generic.h"

#include "qm_work.h"

#define LOG_TAG "APP"

#define CONFIG_MIIO_CONFIRM_BUTTON  (1)
#define CONFIG_MIIO_RESET_TEST      (1)



static void iot_report_timer(void *arg);
static void reset_timer_callback(void *arg);
static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx);
static void iot_hook_event_notify(iot_generic_event_t event, void *ctx);

static void *iot_handle = NULL;
static qm_tlv_ctx_t report_ctx = {0};
static qm_work_t rest_timer_handle  = {0};
static qm_work_t report_timer_handle  = {0};

#if CONFIG_MIIO_CONFIRM_BUTTON
static qm_work_t confirm_timer_handle  = {0};
#endif


static iot_generic_hooks_t mi_hook = {
    .set = iot_hook_set,
    .get = iot_hook_get,
    .event_notify = iot_hook_event_notify,
    .ctx = NULL,
};

static void reset_timer_callback(void *arg)
{
    QM_LOGD(LOG_TAG, "QM RESTART !!!");

    iot_generic_reset(iot_handle);
}

static void iot_report_timer(void *arg)
{
    int block_len = 0;
    void *report_handle = NULL;
    uint8_t *block_buff = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *tlv_block_tmp = NULL;

    report_handle = iot_generic_multi_report_start();
    if(report_handle == NULL){
        return;
    }

    tlv_block = qm_tlv_block_next(&report_ctx , NULL);
    while (tlv_block)
    {
        qm_tlv_unpack_bytes_direct(tlv_block, &block_buff, &block_len);

        QM_LOGD(LOG_TAG, "[iot_hook_report] XJ Attr_id %d Type %d", tlv_block->tag, tlv_block->type);
        QM_HEX_LOGD(LOG_TAG, "[iot_hook_report] PACK HEX DATA ", block_buff, block_len);

        tlv_block_tmp = qm_tlv_block_creat();
        if(tlv_block_tmp == NULL){
            break;
        }
        qm_tlv_block_copy(tlv_block_tmp, tlv_block);
        iot_generic_multi_report(report_handle, tlv_block_tmp);

        tlv_block = qm_tlv_block_next(&report_ctx , tlv_block);
    }

    iot_generic_multi_report_end(iot_handle, report_handle);

    qm_post_delayed_action(&report_timer_handle, iot_report_timer, NULL, 10 * 1000);
}

static void iot_hook_event_notify(iot_generic_event_t event, void *ctx)
{
    switch (event)
    {
        case IOT_GENERIC_EVENT_BLE_CONFIG:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_BLE_CONFIG");       
        break;
        case IOT_GENERIC_EVENT_CONNECTING_ROUTER:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_ROUTER");
            qm_cancel_delayed_action(&report_timer_handle);
        break;
        case IOT_GENERIC_EVENT_CONNECTED_ROUTER:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTED_ROUTER");
        break;
        case IOT_GENERIC_EVENT_CONNECTED_CLOUD:

            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONNECTING_CLOUD");

            qm_post_delayed_action(&report_timer_handle, iot_report_timer, NULL, 10 * 1000);
        break; 
        case IOT_GENERIC_EVENT_CONFIG_TIMEOUT:
            QM_LOGD(LOG_TAG, "IOT_GENERIC_EVENT_CONFIG_TIMEOUT");
        break; 
        default:
        break;
    }
}

static int iot_hook_set(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    int block_len = 0;
    uint8_t *block_buff = NULL;

    tlv_block = qm_tlv_block_next(tlv_ctx , NULL);
    while (tlv_block)
    {     
        qm_tlv_unpack_bytes_direct(tlv_block, &block_buff, &block_len);

        QM_LOGD(LOG_TAG, "[iot_hook_set] XJ Attr_id %d Type %d", tlv_block->tag, tlv_block->type);
        if(tlv_block->type == QM_DTTV_TYPE_STRING){
            QM_LOGD(LOG_TAG, "[iot_hook_set] UNPACK string %*.s ", block_len, block_buff);
        }else{
            QM_HEX_LOGD(LOG_TAG, "[iot_hook_set] UNPACK HEX DATA ", block_buff, block_len);
        }
        tlv_block = qm_tlv_block_next(tlv_ctx , tlv_block);
    }
    
    return QM_EOK;
}

static int iot_hook_get(qm_tlv_ctx_t *tlv_ctx, void *ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    int block_len = 0;
    uint8_t block_buff_array[4] = {0x01,0x02,0x03,0x04};
    uint8_t block_buff_string[] = "hello user xjiang!!";
    
    tlv_block = qm_tlv_block_next(tlv_ctx , NULL);
    while (tlv_block)
    {
        QM_LOGD(LOG_TAG, "[iot_hook_get] XJ Attr_id %d Type %d", tlv_block->tag, tlv_block->type);

        switch (tlv_block->type)
        {
            case QM_DTTV_TYPE_BOOL:
            case QM_DTTV_TYPE_INT8:
            case QM_DTTV_TYPE_UINT8:
                block_len = sizeof(uint8_t);
            break;

            case QM_DTTV_TYPE_INT16:
            case QM_DTTV_TYPE_UINT16:
            case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
            case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
            case QM_DTTV_TYPE_FLOAT_ONE_INT16:
            case QM_DTTV_TYPE_FLOAT_TWO_INT16:
                block_len = sizeof(uint16_t);
            break;

            case QM_DTTV_TYPE_INT32:
            case QM_DTTV_TYPE_UINT32:
            
            case QM_DTTV_TYPE_FLOAT32:
            case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
            case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
            case QM_DTTV_TYPE_FLOAT_ONE_INT32:
            case QM_DTTV_TYPE_FLOAT_TWO_INT32:
                block_len = sizeof(uint32_t);
            break;

            case QM_DTTV_TYPE_STRING:
                block_len = strlen(block_buff_string);
            break;

            default:
            break;
        }

        if(tlv_block->type == QM_DTTV_TYPE_STRING){
            QM_LOGD(LOG_TAG, "[iot_hook_get] PACK string %*.s ", block_len, block_buff_string);
            qm_tlv_pack_string(tlv_block, block_buff_string, block_len);
        }else{
            QM_HEX_LOGD(LOG_TAG, "[iot_hook_get] PACK HEX DATA ", block_buff_array, block_len);
            qm_tlv_pack_bytes(tlv_block, block_buff_array, block_len);
        }

        tlv_block = qm_tlv_block_next(tlv_ctx , tlv_block);
    }

    qm_tlv_merge(&report_ctx, tlv_ctx);

    return QM_EOK;
}

static int miio_map_parse_set(iot_map_ctx_t *map_ctx, uint8_t attr_id, uint8_t type_id, 
                                uint8_t siid, uint8_t server_type, uint16_t xiid)

{
    iot_map_mi_t *map_mi = NULL;
    if(map_ctx == NULL){
        return -QM_EINVAL;
    }

    map_mi = (iot_map_mi_t *)iot_generic_map_get((void *)map_ctx);
    if(map_mi == NULL){
        return -QM_ENOMEM;
    }    

    map_mi->type_id = type_id;
    map_mi->attr_id = attr_id;
    map_mi->siid    = siid;
    map_mi->type = server_type;
    map_mi->xiid = xiid;

    return QM_EOK;
}

static void confirm_timer_callback(void *arg)
{
    QM_LOGD(LOG_TAG, "QM CONFIRM ADV START !!!");

    uint8_t confirm_adv = CONFIG_MIIO_CONFIRM_BUTTON;
    iot_generic_option_set(iot_handle, IOT_OPTION_MIIO_BLE_CONFIRM_PAIR_ADV, &confirm_adv, sizeof(uint8_t)); //开启确认配网广播，默认30s超时关闭
}

void qm_application_start(void)
{   
    QM_LOGD(LOG_TAG, "QM APP START !!!");
		
	uint32_t pid = 20582;
	iot_map_ctx_t *map_ctx = NULL;
    uint8_t confirm_button = CONFIG_MIIO_CONFIRM_BUTTON;

    iot_handle = iot_generic_init(IOT_GENERIC_PLATFORM_TYPE_MI, IOT_GENERIC_NETWORK_TYPE_BLE);
    iot_generic_hooks_register(iot_handle, &mi_hook);

    iot_generic_option_set(iot_handle, IOT_OPTION_MODEL, "zjdh.magic_touch.001", strlen("zjdh.magic_touch.001"));
    iot_generic_option_set(iot_handle, IOT_OPTION_PRODUCT_ID, &pid, sizeof(uint32_t));

    iot_generic_option_set(iot_handle, IOT_OPTION_MIIO_BLE_CONFIRM_BUTTON, &confirm_button, sizeof(uint8_t)); //配置确认按键
    
#if CONFIG_MIIO_CONFIRM_BUTTON
    qm_post_delayed_action(&confirm_timer_handle, confirm_timer_callback, NULL, 100); 
#endif

    map_ctx = (iot_map_ctx_t *)iot_generic_map_init(iot_handle, 17);
    if(map_ctx == NULL){
        return;
    }

    miio_map_parse_set(map_ctx, 0, QM_DTTV_TYPE_INT8    , 4, 0, 1);
    miio_map_parse_set(map_ctx, 1, QM_DTTV_TYPE_UINT8   , 4, 0, 2);
    miio_map_parse_set(map_ctx, 2, QM_DTTV_TYPE_INT16   , 4, 0, 3);
    miio_map_parse_set(map_ctx, 3, QM_DTTV_TYPE_UINT16  , 4, 0, 4);
    miio_map_parse_set(map_ctx, 4, QM_DTTV_TYPE_INT32   , 4, 0, 5);
    miio_map_parse_set(map_ctx, 5, QM_DTTV_TYPE_UINT32  , 4, 0, 6);
    miio_map_parse_set(map_ctx, 6, QM_DTTV_TYPE_UINT8   , 4, 0, 7);
    miio_map_parse_set(map_ctx, 7, QM_DTTV_TYPE_STRING  , 4, 0, 8);
    miio_map_parse_set(map_ctx, 8, QM_DTTV_TYPE_BOOL    , 4, 0, 9);
    miio_map_parse_set(map_ctx, 9, QM_DTTV_TYPE_FLOAT_ONE_UINT16 , 4, 0, 10);
    miio_map_parse_set(map_ctx, 10, QM_DTTV_TYPE_FLOAT_TWO_UINT16 , 4, 0, 11);
    miio_map_parse_set(map_ctx, 11, QM_DTTV_TYPE_FLOAT_ONE_UINT32 , 4, 0, 12);
    miio_map_parse_set(map_ctx, 12, QM_DTTV_TYPE_FLOAT_TWO_UINT32 , 4, 0, 13);
    miio_map_parse_set(map_ctx, 13, QM_DTTV_TYPE_FLOAT_ONE_INT16 , 4, 0, 14);
    miio_map_parse_set(map_ctx, 14, QM_DTTV_TYPE_FLOAT_TWO_INT16 , 4, 0, 15);
    miio_map_parse_set(map_ctx, 15, QM_DTTV_TYPE_FLOAT_ONE_INT32 , 4, 0, 16);
    miio_map_parse_set(map_ctx, 16, QM_DTTV_TYPE_FLOAT_TWO_INT32 , 4, 0, 17);

#if CONFIG_MIIO_RESET_TEST
    qm_post_delayed_action(&rest_timer_handle, reset_timer_callback, NULL, 10 * 60 * 1000);
#endif
}