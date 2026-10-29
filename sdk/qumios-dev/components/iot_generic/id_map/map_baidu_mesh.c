#include "id_map.h"
#include "qm_errno.h"

#if CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT

#if CONFIG_IOT_MAP_VERSION_V2
static void *map_parse(iot_map_data_t *map_data);
#else
static int map_parse(void *list_handle, uint8_t *data, int len);
#endif

static int map_match(map_match_type_t match_type, void *a, void *b);

static iot_map_hal_t map_hal_baidu = {
    .parse = map_parse,
    .match = map_match,
};

int iot_map_baidu_mesh_init(void)
{
#if CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
    iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_BAIDU, IOT_GENERIC_NETWORK_TYPE_MESH, &map_hal_baidu);
#endif
    return QM_EOK;
}

#if CONFIG_IOT_MAP_VERSION_V2
static void *map_parse(iot_map_data_t *map_data)
{
    return (void *)&map_data->map_mesh_baidu;
}
#else
static int map_parse(void *handle, uint8_t *data, int len)
{
    int offset = 0;
    uint16_t baidu_id = 0;
    qm_err_t ret = QM_EOK;
    iot_map_baidu_mesh_t map_baidu = {0};

    if(handle == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    while(offset < len){
        map_baidu.type_id = *(data + offset + 0);
        map_baidu.attr_id = *(data + offset + 1);
        memcpy(&baidu_id, data  + offset + 2, sizeof(uint16_t));
        map_baidu.baidu_id = be16_to_cpu(baidu_id);
        ret = iot_map_add(handle, (uint8_t*)&map_baidu, sizeof(iot_map_baidu_mesh_t));
        if(ret != QM_EOK){
            return ret;
        }
        offset += sizeof(iot_map_baidu_mesh_t);
    }
    return QM_EOK;
}
#endif

static int map_match(map_match_type_t match_type, void *a, void *b)
{
    iot_map_baidu_mesh_t *a_map_baidu = (iot_map_baidu_mesh_t*)a;
    iot_map_baidu_mesh_t *b_map_baidu = (iot_map_baidu_mesh_t*)b;

    if(match_type == MAP_MATCH_TYPE_ID_BY_PLATFORM){
        if(a_map_baidu->baidu_id == b_map_baidu->baidu_id){
            return 1;
        }else{
            return 0;
        }
    }else if(match_type == MAP_MATCH_TYPE_PLATFORM_BY_ID){
        if(a_map_baidu->attr_id == b_map_baidu->attr_id){
            return 1;
        }else{
            return 0;
        }
    }
    return 0;
}
#endif