#include "iot_generic_hal.h"

#include "qm.h"
#include "qm_tlv.h"
#include "qm_log.h"
#include "id_map.h"
#include "iot_middleware.h"

#include "genie_event.h"

#define LOG_TAG "baidu_mesh_hal"


static int _baidu_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  baidu_mesh_data_t *operation);
static int _xj_to_baidu_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  baidu_mesh_data_t *operation);

static int iot_hal_init(void *arg)
{
    iot_middleware_init(arg);
    return QM_EOK;
}


static int iot_hal_report(void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    baidu_mesh_data_t operation = {0};

    if(map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        ret = _xj_to_baidu_transformat(map_handle, tlv_block, &operation);
        if (ret != QM_EOK){
            goto _next;
        }

        iot_middleware_report(&operation);

    _next: 
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return ret;
}


static int iot_hal_set(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx)
{
    int ret = QM_EOK;
    baidu_mesh_data_t *baidu_data = (baidu_mesh_data_t *)data;

    if(data == NULL || map_handle == NULL || tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        return -QM_ENOMEM;
    }

    ret = _baidu_to_xj_transformat(map_handle, tlv_block, baidu_data);
    if (ret != QM_EOK){
        return ret;
    }

    qm_tlv_block_add(tlv_ctx, tlv_block); 
    return QM_EOK;
}


static int iot_hal_reset(void *arg)
{
    genie_event(GENIE_EVT_HW_RESET_START, NULL);
    return QM_EOK;
}


static iot_generic_hal_t  generic_hal_baidu_mesh = {
    .init = iot_hal_init,
    .report = iot_hal_report,
    .set = iot_hal_set,
    .reset = iot_hal_reset,
    .deinit = NULL,
};

int iot_generic_hal_baidu_mesh_init(void)
{
    return iot_generic_hal_register(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, &generic_hal_baidu_mesh);
}


static int _xj_to_baidu_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  baidu_mesh_data_t *operation)
{
    int ret = QM_EOK;
    iot_map_baidu_mesh_t id_map_data = {0};

    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }

    id_map_data.attr_id = tlv_block->tag;
    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_PLATFORM_BY_ID, (uint8_t *)&id_map_data, sizeof(iot_map_baidu_mesh_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

   
    operation->payload.baidu_id = id_map_data.baidu_id;

    switch(id_map_data.type_id)
    {
        case QM_DTTV_TYPE_BOOL:
        {
            bool_t bool_value;
            operation->len = sizeof(bool_t);
            qm_tlv_unpack_bool(tlv_block, &bool_value);
            memcpy(&operation->payload.data_param.buff[0], &bool_value, sizeof(bool_t));
        }
        break;

        case QM_DTTV_TYPE_INT8:
        {
            int8_t int8_value;
            operation->len = sizeof(int8_t);
            qm_tlv_unpack_int8(tlv_block, &int8_value);
            memcpy(&operation->payload.data_param.buff[0], &int8_value, sizeof(int8_t));
        }
        break;
        
        case QM_DTTV_TYPE_UINT8:
        {
            uint8_t uint8_value;
            operation->len = sizeof(uint8_t);
            qm_tlv_unpack_uint8(tlv_block, &uint8_value);
            memcpy(&operation->payload.data_param.buff[0], &uint8_value, sizeof(uint8_t));
        }break;


        case QM_DTTV_TYPE_INT16:
        case  QM_DTTV_TYPE_FLOAT_ONE_INT16:		
        case  QM_DTTV_TYPE_FLOAT_TWO_INT16:					
        {
            int16_t int16_value;
            operation->len = sizeof(int16_t);
            qm_tlv_unpack_int16(tlv_block, &int16_value);
            memcpy(&operation->payload.data_param.buff[0], &int16_value, sizeof(int16_t));
        }
        break;

        case QM_DTTV_TYPE_UINT16:
        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            uint16_t uint16_value;
            operation->len = sizeof(uint16_t);
            qm_tlv_unpack_uint16(tlv_block, &uint16_value);
            memcpy(&operation->payload.data_param.buff[0], &uint16_value, sizeof(uint16_t));
        }break;

        case QM_DTTV_TYPE_INT32:
        case  QM_DTTV_TYPE_FLOAT_ONE_INT32:
        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            int32_t int32_value;
            operation->len = sizeof(int32_t);
            qm_tlv_unpack_int32(tlv_block, &int32_value);
            memcpy(&operation->payload.data_param.buff[0], &int32_value, sizeof(int32_t));
        }
        break;

        case QM_DTTV_TYPE_UINT32:
        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            uint32_t uint32_value;
            operation->len = sizeof(uint32_t);
            qm_tlv_unpack_uint32(tlv_block, &uint32_value);
            memcpy(&operation->payload.data_param.buff[0], &uint32_value, sizeof(uint32_t));
        }
        break;

        case QM_DTTV_TYPE_ARRAY:
        {

        }
        break;

        default:
        break;

    }

    

    return QM_EOK;
}

static int _baidu_to_xj_transformat(void *map_handle, qm_tlv_block_t *tlv_block,  baidu_mesh_data_t *operation)
{

    int ret = QM_EOK;
    uint32_t temp = 0;
    iot_map_baidu_mesh_t id_map_data = {0};

    if(tlv_block == NULL || map_handle == NULL || operation == NULL){
        return -QM_EINVAL;
    }


    id_map_data.baidu_id = operation->payload.baidu_id;

    ret = iot_map_match(map_handle, MAP_MATCH_TYPE_ID_BY_PLATFORM, (uint8_t *)&id_map_data, sizeof(iot_map_baidu_mesh_t));
    if(ret != QM_EOK){
        return -QM_EINVAL;
    }

    tlv_block->tag  = id_map_data.attr_id;
    tlv_block->type = id_map_data.type_id;
    
    switch(tlv_block->type)
    {
        case QM_DTTV_TYPE_BOOL:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_bool(tlv_block, (bool_t)temp);
        }
        break;

        case QM_DTTV_TYPE_INT8: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int8(tlv_block, (int8_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_UINT8: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint8(tlv_block, (uint8_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_INT16: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int16(tlv_block, (int16_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_UINT16: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint16(tlv_block, (uint16_t)temp);     
        }
        break;

        case QM_DTTV_TYPE_INT32: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int32(tlv_block, (int32_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_UINT32: 
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint32(tlv_block, (uint32_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_FLOAT_ONE_INT32:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int32(tlv_block, (int32_t)temp);   
        }
        break;

        case QM_DTTV_TYPE_FLOAT_ONE_INT16:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int16(tlv_block, (int16_t)temp);
        }
        break;

        case QM_DTTV_TYPE_FLOAT_ONE_UINT32:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint32(tlv_block, (uint32_t)temp);
        }
        break;

        case QM_DTTV_TYPE_FLOAT_ONE_UINT16:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint16(tlv_block, (uint16_t)temp);
        }
        break;

        case QM_DTTV_TYPE_FLOAT_TWO_INT16:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int16(tlv_block, (int16_t)temp);
        }
        break;

        case QM_DTTV_TYPE_FLOAT_TWO_UINT16:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint16(tlv_block, (uint16_t)temp);
        }
        break;  

        case QM_DTTV_TYPE_FLOAT_TWO_INT32:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_int32(tlv_block, (int32_t)temp);
        }
        break;  

        case QM_DTTV_TYPE_FLOAT_TWO_UINT32:
        {
            memcpy(&temp, &operation->payload.data_param.buff[0], operation->len);
            qm_tlv_pack_uint32(tlv_block, (uint32_t)temp);
        }
        break;	   

        default:
        break;
    }

    return QM_EOK;
}