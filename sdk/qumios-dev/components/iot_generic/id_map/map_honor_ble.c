#include "qm.h"
#include "id_map.h"

#if CONFIG_IOT_MAP_HONOR_BLE_SUPPORT

#define HONOR_BLE_TYPE_MAX_SIZE             (2)
#define HONOR_BLE_GENERIAL_XIID_MAX_SIZE    (255)
#define HONOR_BLE_MESH_XIID_MIN_SIZE        (1000)

#if !CONFIG_IOT_MAP_VERSION_V2
static int map_parse(void *list_handle, uint8_t *data, int len);
#else
static void *map_parse(iot_map_data_t *map_data);
#endif
static int map_match(map_match_type_t match_type, void *a, void *b);


static iot_map_hal_t map_hal_honor_ble = {
    .parse = map_parse,
    .match = map_match,
};

int iot_map_honor_init(void)
{
    iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_HONOR, IOT_GENERIC_NETWORK_TYPE_BLE, &map_hal_honor_ble);
    return QM_EOK;
}
#if CONFIG_IOT_MAP_VERSION_V2

static void *map_parse(iot_map_data_t *map_data)
{
    return (void *)&map_data->map_honor_ble;
}

//从a里面找b，b是app下发的，a是串口写入的
static int map_match(map_match_type_t match_type, void *a, void *b)
{
    iot_map_honor_ble_t *a_map_honor_ble = (iot_map_honor_ble_t*)a;
    iot_map_honor_ble_t *b_map_honor_ble = (iot_map_honor_ble_t*)b;

    if(match_type == MAP_MATCH_TYPE_ID_BY_PLATFORM){
        if( a_map_honor_ble->siid == b_map_honor_ble->siid && 
            a_map_honor_ble->xiid == b_map_honor_ble->xiid && 
            a_map_honor_ble->type == b_map_honor_ble->type){
            return 1;
        }else{
            return 0;
        }
    }else if(match_type == MAP_MATCH_TYPE_PLATFORM_BY_ID){
        if(a_map_honor_ble->attr_id == b_map_honor_ble->attr_id){
            return 1;
        }else{
            return 0;
        }
    }
    return 0;
}
#else
static int map_honor_ble_status_set(iot_map_honor_ble_t *map_honor_ble, uint8_t *data, int32_t len)
{
    uint16_t xiid = 0;
    if(map_honor_ble == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    map_honor_ble->type_id = *(data + 0);
    map_honor_ble->attr_id = *(data + 1);
    map_honor_ble->siid    = *(data + 2);
    map_honor_ble->type    = *(data + 3);

    if(len == 5){
        map_honor_ble->xiid  = *(data + 4);
    }else if(len == 6){
        memcpy(&xiid, data + 4, sizeof(uint16_t));
        map_honor_ble->xiid = be16_to_cpu(xiid);
    }else{
        return -QM_EINVAL;
    }

    return QM_EOK;
}

static int map_data_compare(uint8_t *data, int len)
{
    int32_t count = 0;
    int32_t index = 0;
    qm_err_t ret = QM_EOK;
    int32_t tmp_index = 0;
    uint8_t compare_id = 0;
    iot_map_honor_ble_t map_honor_ble = {0};

    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }

    while(index < len)
    {
        ret = map_honor_ble_status_set(&map_honor_ble, (data + index), 6);
        if(ret != QM_EOK){
            return -QM_EINVAL; 
        }

        if(map_honor_ble.type > HONOR_BLE_TYPE_MAX_SIZE){
            return 5;
        }

        if( map_honor_ble.xiid <  HONOR_BLE_MESH_XIID_MIN_SIZE &&
            map_honor_ble.xiid > HONOR_BLE_GENERIAL_XIID_MAX_SIZE){
            return 5;
        }

        while (tmp_index < index)
        {
            compare_id = *(data + tmp_index + 1);
            if(map_honor_ble.attr_id == compare_id && ++count > 1){
                return 5;
            }
            tmp_index += 6;
        }

        count = 0;
        index += 6;
        tmp_index = 0;
    }
    return 6;
}

static int map_data_len_get(uint8_t *data, int len)
{

    if(len % 5 == 0 && len % 6 == 0){
        return map_data_compare(data, len);
    }else{
        if(len % 5 == 0){
            return 5;
        }else if(len % 6 == 0){
            return 6;
        }
    }
    return -QM_EINVAL;
}

static int map_parse(void *handle, uint8_t *data, int len)
{
    int offset = 0;
    qm_err_t ret = QM_EOK;
    iot_map_honor_ble_t map_honor_ble = {0};
    uint16_t map_data_len = 0;

    if(handle == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    map_data_len = map_data_len_get(data, len);
    if(map_data_len != 5 && map_data_len != 6){
        return -QM_EINVAL;
    }

    while(offset < len){
        map_honor_ble_status_set(&map_honor_ble,  (data + offset), map_data_len);
        ret = iot_map_add(handle, (uint8_t*)&map_honor_ble, sizeof(map_honor_ble));
        if(ret != QM_EOK){
            return ret;
        }
        offset += map_data_len;
    }
    return QM_EOK;
}

static int map_match(map_match_type_t match_type, void *a, void *b)
{
    iot_map_honor_ble_t *a_map_honor_ble = (iot_map_honor_ble_t*)a;
    iot_map_honor_ble_t *b_map_honor_ble = (iot_map_honor_ble_t*)b;

    if(match_type == MAP_MATCH_TYPE_ID_BY_PLATFORM){
        if( a_map_honor_ble->siid == b_map_honor_ble->siid && 
            a_map_honor_ble->xiid == b_map_honor_ble->xiid && 
            a_map_honor_ble->type == b_map_honor_ble->type){
            return 1;
        }else{
            return 0;
        }
    }else if(match_type == MAP_MATCH_TYPE_PLATFORM_BY_ID){
        if(a_map_honor_ble->attr_id == b_map_honor_ble->attr_id){
            return 1;
        }else{
            return 0;
        }
    }
    return 0;
}
#endif
#endif