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
#include "report_list.h"

#include "honor_ble_init.h"

#include <stdbool.h>
#include <stdint.h>

#define TEST_LOG  1

#define LOG_TAG_TEST

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
    uint8_t *data;
    uint8_t data_len;
    uint8_t siid;
    uint8_t prop_idx;
}report_queue_t;


/* 将事件下内容一同上报 */
node_t* g_report_head = NULL;

static int _honor_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation, int len);
static int _xj_to_honor_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation);

static int iot_init(void *arg)
{
   return QM_EOK;
}

static int iot_reset(void *arg)
{
    extern void RemoveBondDevices(void);
    RemoveBondDevices();
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

    QM_LOGD(LOG_TAG,"[%s|%d]", __func__, __LINE__);

    qm_tlv_block_add(tlv_ctx, tlv_block); 

    return QM_EOK;
}

static int iot_get_req(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    if (data == NULL || map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }

    qm_tlv_block_t *tlv_block = qm_tlv_block_creat();
    if (tlv_block == NULL) {
        return -QM_ENOMEM;
    }

    pid_operation_t *p_operation = (pid_operation_t *)data;
    iot_map_honor_ble_t honor_map = {0};

    honor_map.siid = p_operation->siid;
    honor_map.xiid = p_operation->prop_idx;
    honor_map.type = 0;
  
    int ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&honor_map, sizeof(iot_map_honor_ble_t));    
    if (ret != QM_EOK) {
        qm_tlv_block_delete(tlv_block);
        return ret;
    }

    tlv_block->tag  = honor_map.attr_id;
    tlv_block->type = honor_map.type_id;

    qm_tlv_block_add(tlv_ctx, tlv_block);


    QM_LOGD("get_req", "[%s|%d] exit", __func__, __LINE__);
    return 0;
}

static int iot_get_rsp(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len)
{

    // qm_printf("---------------------iot_get_rsp enter--------------------------------\r\n");

    if (data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    pid_operation_t *p_operation = (pid_operation_t *)data;
    iot_map_honor_ble_t honor_map = {0};

    honor_map.siid = p_operation->siid;
    honor_map.xiid = p_operation->prop_idx;
    honor_map.type = 0;

    int ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&honor_map, sizeof(iot_map_honor_ble_t));
    if (ret != QM_EOK) {
        return ret;
    }
    qm_tlv_block_t *tlv_block = qm_tlv_block_find(tlv_ctx, honor_map.attr_id);
    if(tlv_block == NULL)
    {
        return (-QM_ERROR);
    }

    QM_LOGD(LOG_TAG,"[%s|%d] tag:%d type:%d", __func__, __LINE__, tlv_block->tag, tlv_block->type);


    if((ret = _xj_to_honor_transformat(map_handle, tlv_block, p_operation)) != QM_EOK)
    {
        qm_printf("_xj_to_honor_transformat error! ret = %d\r\n", ret);
        return ret;

    }
    // printf("---------------------iot_get_rsp end--------------------------------\r\n");
    return 0;
}

static int iot_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{

    if (map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }
    
    qm_tlv_block_t *tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    pid_operation_t p_operation = {0};
    int ret = QM_EOK;
    while (tlv_block) 
    {
        memset(&p_operation, 0, sizeof(pid_operation_t));
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

        qm_printf("[LHSMD] ---------report-> sid = 0x%02X index = 0x%02X---------------\r\n", p_operation.siid, p_operation.prop_idx);

        for(int i = 0; i < p_operation.value_len; i++)
        {
            qm_printf("[LHSMD] ------value[%d] = %02X---------\r\n", i, p_operation.value[i]);
        }

        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);

        if(node_handle_flag_get() != IDLE_STATUS)
        {
            goto exit;
        }
        data_t *data = (uint8_t *)qm_malloc(sizeof(data_t));
        if(data == NULL) 
        {
            goto exit;
        } 
        memset(data, 0, sizeof(data_t));


        data->sid = p_operation.siid;
        data->prop_idx = p_operation.prop_idx;
        data->data = p_operation.value;
        data->data_len = p_operation.value_len;
        data->prop_type = BLE_DEVICE_REPORT_PROPERTY;
        if(insert_head_node(&g_report_head, data) != 0)
        {
            goto exit;
        }
    }
    if(node_len_get() == 0)
    {
        node_handle_flag_set(IDLE_STATUS);
    }
    else if(node_len_get() == 1)
    {
        print_list(g_report_head);
        prop_report_value(g_report_head->data->sid, g_report_head->data->prop_idx, g_report_head->data->data, g_report_head->data->data_len);
        delete_node(&g_report_head, g_report_head->data->sid, g_report_head->data->prop_idx);
        node_handle_flag_set(IDLE_STATUS);
    }
    else  //大于1个
    {
        print_list(g_report_head);
        prop_report_value(g_report_head->data->sid, g_report_head->data->prop_idx, g_report_head->data->data, g_report_head->data->data_len);
        delete_node(&g_report_head, g_report_head->data->sid, g_report_head->data->prop_idx);
        node_handle_flag_set(INSERT_DATA_STATUS);
    }
    return QM_EOK;
exit:
    if(p_operation.value != NULL)
    {
        qm_free(p_operation.value);
        p_operation.value = NULL;
    }
    //还需全部free g_report_head
    free_list(&g_report_head);
    return (-1);
 }

static int iot_event(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = -1;
    qm_printf("---------------------iot_event--------------------------------\r\n");
    if (map_handle == NULL || tlv_ctx == NULL) {
        return -QM_EINVAL;
    }
    event_operation_t p_operation = {0};
    iot_map_honor_ble_t id_map_data = {0};

    id_map_data.attr_id = eid;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_honor_ble_t));
    if(ret != QM_EOK){
        return ret;
    }

    p_operation.eid = id_map_data.xiid;
    p_operation.siid = id_map_data.siid;

    qm_tlv_block_t *tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    if (tlv_block == NULL ||  tlv_ctx->count == 0) {
        qm_printf("[LHSMD] ---------event report-> sid = 0x%02X index = 0x%02X---------------\r\n", p_operation.siid, p_operation.eid);
        prop_event_report_value(p_operation.siid, p_operation.eid, NULL, 0);
        return 0;
    }
    uint16_t count = 0;
    while (tlv_block) 
    {
        memset(&p_operation, 0, sizeof(pid_operation_t));
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
      
     
        qm_printf("[LHSMD] ---------event report-> sid = 0x%02X index = 0x%02X---------------\r\n", p_operation.siid, p_operation.eid);

        for(int i = 0; i < p_operation.value_len; i++)
        {
            qm_printf("[LHSMD] ------event value[%d] = %02X---------\r\n", i, p_operation.value[i]);
        }

        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);

        if(node_handle_flag_get() != IDLE_STATUS)
        {
            goto exit;
        }
        data_t *data = (uint8_t *)qm_malloc(sizeof(data_t));
        if(data == NULL) 
        {
            goto exit;
        } 
        memset(data, 0, sizeof(data_t));


        data->sid = p_operation.siid;
        data->prop_idx = p_operation.eid;
        data->data = p_operation.value;
        data->data_len = p_operation.value_len;
        data->prop_type = BLE_DEVICE_REPORT_EVENT; //事件
        if(insert_head_node(&g_report_head, data) != 0)
        {
            goto exit;
        }
    }
    if(node_len_get() == 0)
    {
        node_handle_flag_set(IDLE_STATUS);
    }
    else if(node_len_get() == 1)
    {
        print_list(g_report_head);
        prop_event_report_value(g_report_head->data->sid, g_report_head->data->prop_idx, g_report_head->data->data, g_report_head->data->data_len);
        delete_node(&g_report_head, g_report_head->data->sid, g_report_head->data->prop_idx);
        node_handle_flag_set(IDLE_STATUS);
    }
    else  //大于1个
    {
        print_list(g_report_head);
        prop_event_report_value(g_report_head->data->sid, g_report_head->data->prop_idx, g_report_head->data->data, g_report_head->data->data_len);
        delete_node(&g_report_head, g_report_head->data->sid, g_report_head->data->prop_idx);
        node_handle_flag_set(INSERT_DATA_STATUS);
    }
    return QM_EOK;
exit:
    if(p_operation.value != NULL)
    {
        qm_free(p_operation.value);
        p_operation.value = NULL;
    }
    //还需全部free g_report_head
    free_list(&g_report_head);
    return (-1);
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

static iot_generic_hal_t  generic_hal_honor_ble = {
    .init       = iot_init,
    .report     = iot_report,
    .set        = iot_set,
    .get_req    = iot_get_req,
    .get_rsp    = iot_get_rsp,
    .event      = iot_event,
#if CONFIG_QM_SERIAL_ACTION_SUPPORT
    .action_req = NULL,
    .action_rsp = NULL,
#endif
    .reset      = iot_reset,
    .deinit     = iot_deinit,
};

int iot_generic_hal_honor_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_HONOR, IOT_GENERIC_NETWORK_TYPE_BLE, &generic_hal_honor_ble);
}

static int _honor_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block, pid_operation_t *p_operation, int len)
{
    if (tlv_block == NULL || map_handle == NULL || p_operation == NULL) {
        return -QM_EINVAL;
    }

    int ret = QM_EOK;

    iot_map_honor_ble_t id_map_data = {0};
    
    id_map_data.type = 0;
    id_map_data.siid = p_operation->siid;
    id_map_data.xiid = p_operation->prop_idx;

    qm_printf("p_operation->value_len = %d\r\n", p_operation->value_len);
    for(uint8_t i = 0; i < p_operation->value_len; i++)
    {
        qm_printf("0x%02X ", p_operation->value[i]);
    }
    qm_printf("\r\n");
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_honor_ble_t));
    if (ret != QM_EOK) {

        return -QM_EINVAL;
    }

    tlv_block->tag  = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;
    p_operation->attr_id = id_map_data.attr_id;
    p_operation->type_id = id_map_data.type_id;
    qm_printf("id_map_data.type_id = %d\r\n", id_map_data.type_id);
    qm_printf("id_map_data.type = %d\r\n", id_map_data.type);

    switch(id_map_data.type_id)
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
        {
            int16_t value = 0;
            for(uint8_t i = 0; i < p_operation->value_len; i++)
            {
                value |= (p_operation->value[p_operation->value_len - i - 1] << (i * 8));
            }
            qm_tlv_pack_int16(tlv_block, value);
            qm_printf("p_operation->value = %d\r\n", value);
            break;
        }
        case QM_DTTV_TYPE_UINT16:
        {
            uint16_t value = 0;
            for(uint8_t i = 0; i < p_operation->value_len; i++)
            {
                value |= (p_operation->value[p_operation->value_len - i - 1] << (i * 8));
            }
            qm_tlv_pack_uint16(tlv_block, value);
            qm_printf("p_operation->value = %d\r\n", value);
            break;
        }
        case QM_DTTV_TYPE_INT32:
        {
            int32_t value = 0;
            for(uint8_t i = 0; i < p_operation->value_len; i++)
            {
                value |= (p_operation->value[p_operation->value_len - i - 1] << (i * 8));
            }
            qm_tlv_pack_int32(tlv_block, value);
            qm_printf("p_operation->value = %d\r\n", value);
            break;
        }
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t value = 0;
            for(uint8_t i = 0; i < p_operation->value_len; i++)
            {
                value |= (p_operation->value[p_operation->value_len - i - 1] << (i * 8));
            }
            qm_tlv_pack_uint32(tlv_block, value);
            qm_printf("p_operation->value = %d\r\n", value);
            break;
        }
        case QM_DTTV_TYPE_STRING:
            qm_tlv_pack_string(tlv_block, (char *)p_operation->value, p_operation->value_len);
            qm_printf("p_operation->value = %s   len = %d\r\n",  (char *)p_operation->value, p_operation->value_len);
            break;
        default:
            break;
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
    
    iot_map_honor_ble_t id_map_data = {0};
    
    id_map_data.type = 0;
    id_map_data.attr_id = tlv_block->tag;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_honor_ble_t));
    if (ret != QM_EOK) {

        return -QM_EINVAL;
    }

    qm_printf("id_map_data.xiid = %X\r\n", id_map_data.xiid);

    p_operation->siid = id_map_data.siid;
    p_operation->prop_idx = id_map_data.xiid;

    p_operation->type_id = id_map_data.type_id;
    p_operation->value_len = 1;
    p_operation->value = NULL;
    switch (id_map_data.type_id) 
    {
#if 0
        case QM_DTTV_TYPE_BOOL:
        {
            bool value;
            qm_tlv_unpack_bool(tlv_block, &value);
            P_MALLOC(p_operation->value, uint8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);

        }  break;
        case QM_DTTV_TYPE_INT8:
        {
            int8_t value;
            qm_tlv_unpack_int8(tlv_block, &value);
            P_MALLOC(p_operation->value, int8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);

        }  break;
        case QM_DTTV_TYPE_UINT8:
        {
            uint8_t value;
            qm_tlv_unpack_uint8(tlv_block, &value);
            P_MALLOC(p_operation->value, uint8_t, p_operation->value_len);
            memcpy(p_operation->value, &value, p_operation->value_len);

        } 
            break;

        case QM_DTTV_TYPE_INT16:
         {
            int16_t value;
            p_operation->value_len = 2;
            qm_tlv_unpack_int16(tlv_block, &value);
            P_MALLOC(p_operation->value, int16_t, p_operation->value_len);
            value = SWAP_2BYTE(int16_t, value);

            memcpy(p_operation->value, &value, p_operation->value_len);


        } 
        break;
        case QM_DTTV_TYPE_UINT16:
         {
            uint16_t value;
            p_operation->value_len = 2;
            qm_tlv_unpack_uint16(tlv_block, &value);
            P_MALLOC(p_operation->value, uint16_t, p_operation->value_len);

            value = SWAP_2BYTE(uint16_t, value);
            memcpy(p_operation->value, &value, p_operation->value_len);


        } 
        break;

        case QM_DTTV_TYPE_INT32:
         {
            int32_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_int32(tlv_block, &value);     
            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            value = SWAP_4BYTE(int32_t, value);
            memcpy(p_operation->value, &value, p_operation->value_len);


        } break;
        case QM_DTTV_TYPE_UINT32:
        {
            uint32_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_uint32(tlv_block, &value);
            qm_printf("uint32 value = %u\r\n", value);
            P_MALLOC(p_operation->value, uint32_t, p_operation->value_len);
            value = SWAP_4BYTE(uint32_t, value);
            memcpy(p_operation->value, &value, p_operation->value_len);


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
#elif 1
        case QM_DTTV_TYPE_BOOL:
        {
            bool_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_bool(tlv_block, &value);     

            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            int32_t num  = SWAP_4BYTE(int32_t, (int32_t)value);
            memcpy(p_operation->value, &num, p_operation->value_len);
        }
        break;

        case QM_DTTV_TYPE_INT8:
        {
            int8_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_int8(tlv_block, &value);     

            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            int32_t num = SWAP_4BYTE(int32_t, (int32_t)value);
            memcpy(p_operation->value, &num, p_operation->value_len);
        }
        break;
        case QM_DTTV_TYPE_UINT8:
        {
            uint8_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_uint8(tlv_block, &value);     

            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            int32_t num = SWAP_4BYTE(int32_t, (int32_t)value);
            memcpy(p_operation->value, &num, p_operation->value_len);
        }
        break;
        case QM_DTTV_TYPE_INT16:
        {
            int16_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_int16(tlv_block, &value);     
            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            int32_t num = SWAP_4BYTE(int32_t, (int32_t)value);
            memcpy(p_operation->value, &num, p_operation->value_len);

        }
        break;
        case QM_DTTV_TYPE_UINT16:
        {
            uint16_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_uint16(tlv_block, &value);     
            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            int32_t num = SWAP_4BYTE(int32_t, (int32_t)value);
            memcpy(p_operation->value, &num, p_operation->value_len);
        }
        break;
        case QM_DTTV_TYPE_INT32:
        case QM_DTTV_TYPE_UINT32:
        {
            int32_t value;
            p_operation->value_len = 4;
            qm_tlv_unpack_int32(tlv_block, &value);     
            P_MALLOC(p_operation->value, int32_t, p_operation->value_len);
            value = SWAP_4BYTE(int32_t, value);
            memcpy(p_operation->value, &value, p_operation->value_len);


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
            qm_tlv_unpack_string(tlv_block, buff, &temp_len);
            memcpy(p_operation->value, buff, p_operation->value_len);
        } break;


#endif
        default:
            break;
    }
    // p_operation->type = honor_map.map_honor_unit->type;

    return QM_EOK;
}

