#include "iot_generic_hal.h"
#include "id_map.h"

#include "qm_log.h"
#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_utils_string.h"
#include "qm_iot_operation.h"
#include "serial_protocol.h"
#include "iot_generic.h"
#include "generic_serial.h"
#include "qm_kv.h"
#include "generic_serial_factor.h"

#include <stdbool.h>
#include <stdint.h>
#include <magiclink.h>
#include <magiclink_netcfg.h>

#define TEST_LOG  1
#define CONFIG_QM_LOG_PRINT_BL602 1
#define REPORT_LIST_LEN_MAX 30

typedef struct {
    uint8_t idx;
    uint8_t pid_count;
    int idx_list[10];
    char sid[25];
} report_sid_pid_t;

typedef struct report_queue_s
{
    uint8_t list_len;
    uint8_t report_flag;
    unsigned int data;
    uint8_t sid[30];
}report_queue_t;

static qm_timer_t reboot_timer;
static uint8_t g_reboot_flag = 0;
// qm_task_new(&recv_loop_task, "serial recv loop", serial_recv_thread, NULL, 4*1024*4, 0) == 0
qm_queue_t g_repott_data_queue = {0};
qm_task_t honor_report_task = {0};
// int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms);


static int _honor_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation, int len);
static int _xj_to_honor_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation);


static int iot_init(void *arg)
{
   return QM_EOK;
}

static void qm_timer_reboot_cb( qm_timer_t *timer, void *arg)
{
    qm_reboot();
}

void qm_reboot_timer_start(void)
{
    if(g_reboot_flag == 0)
    {
        qm_timer_new(&reboot_timer, qm_timer_reboot_cb, NULL, 500, 0);
        qm_timer_start(&reboot_timer);
        g_reboot_flag = 1;
    }
}

extern uint8_t get_online_state(void);
extern void device_name_clear(void);
static int iot_reset(void *arg)
{
   int module_status = 0;
    unsigned int timeout = 10;


    /* 已注册到设备云需要先从云端解绑设备 */
    if (MagicLinkReset() != 0) {
        return QM_ERROR;
    }
    /* 初始化SDK并设置配网超时时间 */
    if (MagicLinkSetAttr(MAGICLINK_ATTR_NETCFG_TIMEOUT, &timeout, sizeof(timeout)) != 0) {
        return QM_ERROR;
    }


    if (MagicLinkStartNetCfg() != 0) {
        return QM_ERROR;
    }
    device_name_clear();

    qm_kv_set(MODULE_STATUS_KEY, &module_status, 4, 1);

    qm_reboot_timer_start();
    return QM_EOK; 
}

static int iot_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)   
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }
    
    pid_operation_t *p_operation = (pid_operation_t *)data;
    qm_tlv_block_t *tlv_block = qm_tlv_block_creat();

    if (tlv_block == NULL) {
        return -QM_ENOMEM;
    }
    
    int ret = _honor_to_xj_transformat(map_handle, tlv_block, p_operation, len);    
    if (ret != QM_EOK){
        qm_tlv_block_delete(tlv_block);
        return ret;
    }
#if CONFIG_QM_LOG_PRINT_BL602
    QM_LOGD(LOG_TAG,"[%s|%d]", __func__, __LINE__);
#endif
    qm_tlv_block_add(tlv_ctx, tlv_block); 

    return QM_EOK;
}

static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
#if CONFIG_QM_LOG_PRINT_BL602
    qm_printf("---------------------iot_get_req enter--------------------------------\r\n");
#endif
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }

    qm_tlv_block_t *tlv_block = qm_tlv_block_creat();
    if (tlv_block == NULL) {
        return -QM_ENOMEM;
    }

    pid_operation_t *p_operation = (pid_operation_t *)data;
    iot_map_honor_t honor_map = {0};

    map_honor_unit_t map_honor_unit = {0};
    char *sid = NULL;

    idmap_serial_copy(&honor_map, p_operation->svcId, &sid);
    idmap_prop_copy(&map_honor_unit, p_operation->propId);

    honor_map.map_honor_unit = &map_honor_unit;
  
    int ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&honor_map, sizeof(iot_map_honor_t));    
    if (ret != QM_EOK) {
        qm_tlv_block_delete(tlv_block);
        return ret;
    }

    tlv_block->tag  = honor_map.map_honor_unit->attr_id;
    tlv_block->type = honor_map.map_honor_unit->type;
    // tlv_block->type_id = honor_map.map_honor_unit->type_id;

#if CONFIG_QM_LOG_PRINT_BL602
    QM_LOGD("get_req", "[%s|%d] tag:%d type:%d", __func__, __LINE__, tlv_block->tag, tlv_block->type);
#endif
    qm_tlv_block_add(tlv_ctx, tlv_block);

    if (sid != NULL) {
        qm_free(sid);
        sid = NULL;
    }

    #if 0
    if (map_honor_unit.pid_len > HONOR_ID_MAX_LEN && map_honor_unit->p.pid != NULL) {
        qm_free(map_honor_unit->p.pid);
        map_honor_unit->p.pid = NULL;
    }
    #endif
    QM_LOGD("get_req", "[%s|%d] exit", __func__, __LINE__);
    return ret;
}

static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{
#if CONFIG_QM_LOG_PRINT_BL602
    qm_printf("---------------------iot_get_rsp enter--------------------------------\r\n");
#endif
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    #if 1
    pid_operation_t *p_operation = (pid_operation_t *)data;
#if CONFIG_QM_LOG_PRINT_BL602
    qm_printf("---------------------sid = %s  pid = %s--------------------------------\r\n", p_operation->svcId, p_operation->propId);
#endif
    iot_map_honor_t honor_map = {0};
    map_honor_unit_t map_honor_unit = {0};
    char *sid = NULL;

    idmap_serial_copy(&honor_map, p_operation, &sid);
    idmap_prop_copy(&map_honor_unit, p_operation->propId);

    honor_map.map_honor_unit = &map_honor_unit;

    int ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&honor_map, sizeof(iot_map_honor_t));
    if (ret != QM_EOK) {
        return ret;
    }
    qm_tlv_block_t *tlv_block = qm_tlv_block_find(tlv_ctx, honor_map.map_honor_unit->attr_id);

#if CONFIG_QM_LOG_PRINT_BL602
    QM_LOGD(LOG_TAG,"[%s|%d] tag:%d type:%d", __func__, __LINE__, tlv_block->tag, tlv_block->type);
#endif

    if((ret = _xj_to_honor_transformat(map_handle, tlv_block, p_operation)) != QM_EOK)
    {
#if CONFIG_QM_LOG_PRINT_BL602
        qm_printf("_xj_to_honor_transformat error! ret = %d\r\n", ret);
#endif
    }

    if (sid != NULL) {
        qm_free(sid);
        sid = NULL;
    }

    if (map_honor_unit.pid_len > HONOR_ID_MAX_LEN && map_honor_unit.p.pid != NULL) {
        qm_free(map_honor_unit.p.pid);
        map_honor_unit.p.pid = NULL;
    }
    #endif
#if CONFIG_QM_LOG_PRINT_BL602    
     printf("---------------------iot_get_rsp end--------------------------------\r\n");
#endif
    return ret;
}
// report_queue_t g_report_data = {0};
#define LOG_TAG_TEST
static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
#if CONFIG_QM_LOG_PRINT_BL602
    QM_LOGD(LOG_TAG,"[%s|%d]", __func__, __LINE__);
#endif
    if (map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }

    qm_tlv_block_t *tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    pid_operation_t p_operation = {0};
    report_sid_pid_t report_list[tlv_ctx->count];
    unsigned int arr[5] = {0};
    report_queue_t report_queue[REPORT_LIST_LEN_MAX] = {0};
    int sid_count = 0; 
    int ret = QM_EOK;
#if CONFIG_QM_LOG_PRINT_BL602
    QM_LOGD(LOG_TAG,"[%s|%d]", __func__, __LINE__);
#endif
    sid_count = tlv_ctx->count;
    uint8_t num = 0;
    report_queue[num++].list_len = sid_count;
    while (tlv_block) {
        ret = _xj_to_honor_transformat(map_handle, tlv_block, &p_operation);
        if (ret != QM_EOK){
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);

             if(p_operation.value != NULL)
            {
                qm_free(p_operation.value);
                p_operation.value = NULL;
            }
            continue;
        }
#if CONFIG_QM_LOG_PRINT_BL602
        // qm_printf("[LHSMD] ----------%s %s %d---------------\r\n", p_operation.svcId, p_operation.propId, *(uint8_t *)p_operation.value);
        // QM_HEX_LOGD(LOG_TAG_TEST, "report ", p_operation.value, p_operation.value_len);
#endif
        if(p_operation.value != NULL)
        {
            qm_free(p_operation.value);
            p_operation.value = NULL;
        }
#if CONFIG_QM_LOG_PRINT_BL602       
        qm_printf("[LHSMD] ----------p_operation.propidx = %d---------------\r\n", p_operation.propidx);
#endif
        
        report_queue[num].data = p_operation.propidx;
        memcpy(report_queue[num].sid, p_operation.svcId, strlen(p_operation.svcId));

        num++;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    qm_printf("[LHSMD] ------------------------------------------------------\r\n");
    for(int i = 0; i < num; i++)
    {
        qm_printf("[LHSMD] ----------report_queue[%d].data = %d report_queue[%d].sid = %s---------------\r\n", i, report_queue[i].data, i ,report_queue[i].sid);
    }
    qm_printf("[LHSMD] ------------------------------------------------------\r\n");
    qm_queue_send(&g_repott_data_queue, report_queue, sizeof(report_queue_t) * REPORT_LIST_LEN_MAX);
    return QM_EOK;
 }
uint8_t buf[4] = {12, 0};
/* 将事件下内容一同上报 */
    
static int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = -1;
    qm_printf("---------------------iot_event--------------------------------\r\n");
    if (map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }

    qm_tlv_block_t *tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    if (tlv_block == NULL ||  tlv_ctx->count == 0) {
        return 0;
    }
    qm_printf("---------------------iot_event--------------------------------2\r\n");
    uint16_t count = 0;
    while (tlv_block) 
    {
        pid_operation_t operation_tmp = {0};
        ret = _xj_to_honor_transformat(map_handle, tlv_block, &operation_tmp);
        if (ret != QM_EOK){
            break;
        }
        struct MagicLinkDataVal dataArray = {0};
        qm_printf("---------------------iot_event--------------------------------\r\n");
        qm_printf("operation_tmp.svcId = %s\r\n", operation_tmp.svcId);
        qm_printf("operation_tmp.propId = %s\r\n", operation_tmp.propId);
        QM_HEX_LOGD(LOG_TAG_TEST, "value ", (uint8_t *)operation_tmp.value, operation_tmp.value_len);
        qm_printf("operation_tmp.value_len = %d\r\n", operation_tmp.value_len);
        qm_printf("operation_tmp.eid_list[%d] = %d\r\n", count, operation_tmp.eid_list[count]);
        qm_printf("---------------------iot_event--------------------------------\r\n");
        

        if(operation_tmp.eid_list[count] != 0xFF && count < EVENT_NUM_MAX)
        {
            dataArray.dataIdx = operation_tmp.eid_list[count];
            dataArray.dataLen = operation_tmp.value_len;
            dataArray.data = operation_tmp.value;
            if (MagicLinkReportServiceEvent(operation_tmp.svcId, operation_tmp.propId, 1, &dataArray) != 0) {
                qm_printf("MagicLinkReportServiceEvent error!\r\n");
                return QM_ERROR;
            }

           
        }
        else if(count < EVENT_NUM_MAX)
        {
            if (MagicLinkReportServiceEvent(operation_tmp.svcId, operation_tmp.propId, 0, NULL) != 0) {
                return QM_ERROR;
            }

        }
        if (operation_tmp.value != NULL) {
            qm_free(operation_tmp.value);
            operation_tmp.value = NULL;
        }

        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
        count++;

        if(count >= 5) break;
        
    }
    return QM_EOK;
}

static int iot_action_req(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx)
{
    if (map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }
    #if 0
    int ret = QM_EOK;
    int in_len = 0;
    aid_eid_operation_t *operation = (aid_eid_operation_t *)data;
    iot_map_honor_t id_map_data = {0};

    id_map_data.type_id = 1;
    id_map_data.type = QM_DTTV_TYPE_STRING;
    strcpy(id_map_data.svcId, operation->svcId);
    strcpy(id_map_data.propId, operation->eventId);
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_honor_t));    
    if (ret != QM_EOK) {
        return ret;
    }

    *aid = id_map_data.attr_id;

#if 1
    while (in_len < operation->dataNum) {
        pid_operation_t prop_operation = {0};
        qm_tlv_block_t *tlv_block = qm_tlv_block_creat();
        if (tlv_block == NULL) {
            return -QM_ENOMEM;
        }  

        strcpy(prop_operation.svcId, operation->svcId);
        strcpy(prop_operation.propId, operation->eventId);
        strcpy(prop_operation.value.string, (char *)operation->dataArray[in_len].data);

        in_len++;

        ret = _honor_to_xj_transformat(map_handle, tlv_block, &prop_operation, strlen(prop_operation.value.string));
        if (ret != QM_EOK){
            qm_tlv_block_delete(tlv_block);
            break;
        }
        
        qm_tlv_block_add(tlv_ctx, tlv_block); 
    }
#endif
    #endif
    return QM_EOK;
}

static int iot_action_rsp(void *map_handle, uint8_t aid, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{
    
    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

#if 0 /* 1.1.0.334 版本SDK暂没开放输出接口 */
    int out_len = 0;
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    aid_eid_operation_t *out_operation = (aid_eid_operation_t *)data;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block && tlv_ctx->count){
        property_operation_t operation = {0};
        ret = _xj_to_honor_transformat(map_handle, tlv_block, &operation);
        if (ret < 0){
            break;
        }

        out_operation->dataNum++:
        strcpy(prop_operation.svcId, operation->svcId);
        strcpy(prop_operation.propId, operation->eventId);
        strcpy(prop_operation.value.string, (char *)operation->dataArray[out_len].data);   
        out_len++;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
#endif

    return QM_EOK;
}

typedef struct {
    iot_generic_platform_type_t platform_type;
    iot_generic_network_type_t network_type;
    iot_generic_hooks_t *hooks;
    iot_generic_hal_t *generic_hal;
    qm_tlv_ctx_t *option;
    void *map_handle;
    void *ctx;
}iot_generic_ctx_t;

extern void *generic_serial_iot_handle_get(void);
static int iot_deinit(void *arg)
{
    // void *qm_iot_handle = NULL;
    iot_generic_ctx_t *qm_iot_handle = (iot_generic_ctx_t *)generic_serial_iot_handle_get();
    if(qm_iot_handle == NULL)
    {
        return (-1);
    }
    iot_map_deinit(qm_iot_handle->map_handle);
    return QM_EOK;
}



static iot_generic_hal_t  generic_hal_honor_wifi = {
    .init       = iot_init,
    .report     = iot_report,
    .set        = iot_set,
    .get_req    = iot_get_req,
    .get_rsp    = iot_get_rsp,
    .event      = iot_event,
    .action_req = iot_action_req,
    .action_rsp = iot_action_rsp,
    .reset      = iot_reset,
    .deinit     = iot_deinit,
};

int iot_generic_hal_honor_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_HONOR, IOT_GENERIC_NETWORK_TYPE_WIFI, &generic_hal_honor_wifi);
}

static int _honor_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation, int len)
{
    if (tlv_block == NULL || map_handle == NULL || p_operation == NULL) {
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    iot_map_honor_t honor_map = {0};
    map_honor_unit_t map_honor_unit = {0};
    char *sid = NULL;
          
    idmap_serial_copy(&honor_map, p_operation->svcId, &sid);
    idmap_prop_copy(&map_honor_unit, p_operation->propId);

    honor_map.map_honor_unit = &map_honor_unit;
#if CONFIG_QM_LOG_PRINT_BL602   
    QM_LOGD(LOG_TAG,"[%s|%d] -------------%d", __func__, __LINE__, HONOR_ID_MAX_LEN);
    if (honor_map.sid_len > HONOR_ID_MAX_LEN) {
        QM_LOGD(LOG_TAG,"[%s|%d] %s", __func__, __LINE__, honor_map.s.sid);
    } else {
        QM_LOGD(LOG_TAG,"[%s|%d] %s", __func__, __LINE__, honor_map.s.id);
    }
    if (honor_map.map_honor_unit->pid_len > HONOR_ID_MAX_LEN) {
        QM_LOGD(LOG_TAG,"[%s|%d] %s", __func__, __LINE__, honor_map.map_honor_unit->p.pid);
    } else {
        QM_LOGD(LOG_TAG,"[%s|%d] %s", __func__, __LINE__, honor_map.map_honor_unit->p.id);
    }
#endif 
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&honor_map, sizeof(iot_map_honor_t));
    if (ret != QM_EOK) {
#if CONFIG_QM_LOG_PRINT_BL602   
        QM_LOGD(LOG_TAG,"[%s|%d]", __func__, __LINE__);
#endif
        return -QM_EINVAL;
    }

    tlv_block->tag  = honor_map.map_honor_unit->attr_id;
    tlv_block->type = honor_map.map_honor_unit->type;
    // tlv_block->type_id = honor_map.map_honor_unit->type_id;
#if CONFIG_QM_LOG_PRINT_BL602   
    QM_LOGD("transformat", "%s %d  %d", __func__, __LINE__, honor_map.map_honor_unit->type_id);
#endif
    switch(honor_map.map_honor_unit->type)
    {   
        case QM_DTTV_TYPE_BOOL:
            qm_tlv_pack_bool(tlv_block, *(bool *)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(bool *)p_operation->value);
            break;
        case QM_DTTV_TYPE_INT8:
            qm_tlv_pack_int8(tlv_block, *(int8_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(int8_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_UINT8:
            qm_tlv_pack_uint8(tlv_block, *(uint8_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(uint8_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_INT16:
            qm_tlv_pack_int16(tlv_block, *(int16_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(int16_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_UINT16:
            qm_tlv_pack_uint16(tlv_block, *(uint16_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(uint16_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_INT32:
            qm_tlv_pack_int32(tlv_block, *(int32_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(int32_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_UINT32:
            qm_tlv_pack_uint32(tlv_block, *(uint32_t*)p_operation->value);
            qm_printf("p_operation->value = %d\r\n", *(uint32_t*)p_operation->value);
            break;
        case QM_DTTV_TYPE_STRING:
            qm_tlv_pack_string(tlv_block, (char *)p_operation->value, strlen((char *)p_operation->value));
            qm_printf("p_operation->value = %s   len = %d\r\n",  (char *)p_operation->value, strlen((char *)p_operation->value));
            break;
        default:
            break;
    }

    if (sid != NULL) {
        qm_free(sid);
        sid = NULL;
    }

    if (map_honor_unit.pid_len > HONOR_ID_MAX_LEN && map_honor_unit.p.pid != NULL) {
        qm_free(map_honor_unit.p.pid);
        map_honor_unit.p.pid = NULL;
    }

    return 0;
}

#define P_MALLOC(point, type, size) \
    do { \
        point = (type *)qm_malloc(size); \
        if (point == NULL) { \
            return QM_ERROR; \
        } \
        memset(point, 0, size); \
    } while(0);



static int _xj_to_honor_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation)
{
    if (tlv_block == NULL || map_handle == NULL || p_operation == NULL) {
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    
    iot_map_honor_t honor_map = {0};
    map_honor_unit_t map_honor_unit = {0};

    map_honor_unit.attr_id = (int)tlv_block->tag;
    map_honor_unit.type = tlv_block->type;
#if CONFIG_QM_LOG_PRINT_BL602   
    qm_printf("map_honor_unit.attr_id = %d\r\n", map_honor_unit.attr_id);
#endif 
    honor_map.map_honor_unit = &map_honor_unit;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&honor_map, sizeof(iot_map_honor_t));
    if (ret != QM_EOK) {
#if CONFIG_QM_LOG_PRINT_BL602   
        qm_printf("iot map match error!\r\n");
#endif
        return -QM_EINVAL;
    }
#if CONFIG_QM_LOG_PRINT_BL602   
    QM_LOGD("transformat", "[%s|%d] type_id:%d", __func__, __LINE__, honor_map.map_honor_unit->type_id);
    QM_LOGD("transformat", "[%s|%d] sid:%s pid:%s", __func__, __LINE__, honor_map.s.id, honor_map.map_honor_unit->p.id);
#endif

    memset(p_operation->svcId, 0, sizeof(p_operation->svcId));
    memcpy(p_operation->svcId, honor_map.s.id, strlen(honor_map.s.id));
    memset(p_operation->propId, 0, sizeof(p_operation->propId));
    memcpy(p_operation->propId, honor_map.map_honor_unit->p.id, strlen(honor_map.map_honor_unit->p.id));
    memcpy(p_operation->eid_list, honor_map.map_honor_unit->eiid_list, sizeof(p_operation->eid_list));
    p_operation->propidx = honor_map.map_honor_unit->propidx;
    p_operation->type = honor_map.map_honor_unit->type;
    // p_operation->value_len = 4;
    p_operation->value_len = 1;
    p_operation->value = NULL;
    switch (honor_map.map_honor_unit->type) {
        case QM_DTTV_TYPE_BOOL:
        {
            bool value;
            qm_tlv_unpack_bool(tlv_block, &value);
            P_MALLOC(p_operation->value, uint8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("p_operation->value = %d, value = %d\r\n", *(uint8_t *)p_operation->value, value);
#endif
        }  break;
        case QM_DTTV_TYPE_INT8:
         {
            int8_t value;
            qm_tlv_unpack_int8(tlv_block, &value);
            P_MALLOC(p_operation->value, int8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("p_operation->value = %d, value = %d\r\n",  *(uint8_t *)p_operation->value, value);
#endif
        }  break;
        case QM_DTTV_TYPE_UINT8:
        {
            uint8_t value;
            qm_tlv_unpack_uint8(tlv_block, &value);
            P_MALLOC(p_operation->value, uint8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("p_operation->value = %d, value = %d\r\n",  *(uint8_t *)p_operation->value, value);
#endif
        } 
            break;

        case QM_DTTV_TYPE_INT16:
         {
            int16_t value;
            p_operation->value_len = 2;
            qm_tlv_unpack_int16(tlv_block, &value);
            P_MALLOC(p_operation->value, int16_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
           qm_printf("p_operation->value = %d, value = %d\r\n", *(int16_t *)p_operation->value, value);
#endif

        } 
        break;
        case QM_DTTV_TYPE_UINT16:
         {
            uint16_t value;
            p_operation->value_len = 2;
            qm_tlv_unpack_uint16(tlv_block, &value);
            P_MALLOC(p_operation->value, uint16_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("value = %d\r\n", value);
#endif

        } 
        break;

        case QM_DTTV_TYPE_INT32:
         {
            int32_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_int32(tlv_block, &value);     
            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("int32 value = %ld\r\n", value);
#endif

        } break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_uint32(tlv_block, &value);
            qm_printf("uint32 value = %u\r\n", value);
            P_MALLOC(p_operation->value, uint32_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);
#if CONFIG_QM_LOG_PRINT_BL602   
            qm_printf("uint32 value = %u\r\n", value);
#endif

        } break;
        case QM_DTTV_TYPE_STRING: {
            int temp_len = 0;
            char buff[64] = {0};
            char *pdata  = NULL;
            qm_tlv_unpack_string(tlv_block, NULL, &temp_len);
            if(temp_len >= 64)  break;
            p_operation->value_len = temp_len;
            p_operation->value = (uint8_t *)qm_malloc(p_operation->value_len + 1);
            if (p_operation->value == NULL) {
                break;
            }
            memset(p_operation->value, 0, p_operation->value_len);
            
            // if (temp_len > 64) {
            //     if (pdata == NULL) {
            //         return -QM_ENOMEM;
            //     }
            //     memset(pdata, 0, temp_len + 1);
            //     qm_tlv_unpack_string(tlv_block, pdata, &temp_len);
            //     memcpy(p_operation->value, pdata, p_operation->value_len);
            //     qm_free(pdata);
            //     pdata = NULL;
            // } else {
            qm_tlv_unpack_string(tlv_block, buff, &temp_len);
            memcpy(p_operation->value, buff, p_operation->value_len);
            // }
        } break;
        default:
            break;
    }
    p_operation->type = honor_map.map_honor_unit->type;
#if CONFIG_QM_LOG_PRINT_BL602   
    QM_LOGD("transformat", "%s %d", __func__, __LINE__);
#endif

    return QM_EOK;
}

// qm_mutex_t g_report_mutex = {0};
void iot_report_task(void *arg)
{
    unsigned int size = sizeof(report_queue_t) * REPORT_LIST_LEN_MAX;
    report_queue_t report_queue[REPORT_LIST_LEN_MAX] = {0};
    if(qm_queue_new(&g_repott_data_queue, 1, sizeof(report_queue_t) * REPORT_LIST_LEN_MAX) != QM_EOK)
    {
        qm_printf("report_data_queue error!\r\n");
        qm_task_exit(&honor_report_task);
        return;
    }
    while(1)
    {
        if(qm_queue_recv(&g_repott_data_queue, report_queue, &size, QM_WAIT_FOREVER) == QM_EOK)
        {
            for(int i = 1; i <= report_queue[0].list_len; i++)
            {
                qm_printf("[LHSMD] -----%d. report_queue[%d].sid = [%s], report_queue[%d].data = [%d]------\r\n", i, i, report_queue[i].sid, i, report_queue[i].data);
                if (MagicLinkReportPropertyStatus((const char *)report_queue[i].sid, 1, (const unsigned int *)&report_queue[i].data) != 0) {
                    QM_LOGD("report", "[%s|%d] MagicLinkReportServiceStatus fail! ", __func__, __LINE__);
                }
            }
            
        }
        qm_msleep(10);

    }
}


void honor_report_task_init(void)
{
    extern app_task_t g_app_task;
    if(qm_task_new(&g_app_task.honor_report_task, "honor_report_task", iot_report_task, NULL, 1536, 0) != QM_EOK) //2k
    {
        qm_printf("honor_report_task_init error!\r\n");
    }
}

