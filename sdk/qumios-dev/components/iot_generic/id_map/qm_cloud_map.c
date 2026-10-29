#include "qm.h"
#include "id_map.h"

#if CONFIG_IOT_MAP_QM_CLOUD_SUPPORT

#define MIJIA_TYPE_MAX_SIZE             (2)
#define MIJIA_GENERIAL_XIID_MAX_SIZE    (255)
#define MIJIA_MESH_XIID_MIN_SIZE        (1000)

#if !CONFIG_IOT_MAP_VERSION_V2
#error "only support iot version"
#endif

static void *map_parse(iot_map_data_t *map_data);
static int map_match(map_match_type_t match_type, void *a, void *b);

static iot_map_hal_t map_hal_mi = {
    .parse = map_parse,
    .match = map_match,
};

int iot_map_qm_cloud_init(void)
{
#if CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
    iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_QM_CLOUD, IOT_GENERIC_NETWORK_TYPE_WIFI, &map_hal_mi);
#endif
#if CONFIG_IOT_GENERIC_NETWORK_BLE_SUPPORT
    iot_ma p_hal_register(IOT_GENERIC_PLATFORM_TYPE_QM_CLOUD, IOT_GENERIC_NETWORK_TYPE_BLE, &map_hal_mi);
#endif
#if CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
    iot_map_hal_register(IOT_GENERIC_PLATFORM_TYPE_QM_CLOUD, IOT_GENERIC_NETWORK_TYPE_MESH, &map_hal_mi);
#endif
    return QM_EOK;
}

static void *map_parse(iot_map_data_t *map_data)
{
    return (void *)&map_data->map_qm_cloud;
}

static int map_match(map_match_type_t match_type, void *a, void *b)
{
    iot_map_qm_cloud_t *a_map_qm_cloud = (iot_map_qm_cloud_t*)a;
    iot_map_qm_cloud_t *b_map_qm_cloud = (iot_map_qm_cloud_t*)b;

    if(match_type == MAP_MATCH_TYPE_ID_BY_PLATFORM){
        if( a_map_qm_cloud->siid == b_map_qm_cloud->siid && 
            a_map_qm_cloud->xiid == b_map_qm_cloud->xiid && 
            a_map_qm_cloud->type == b_map_qm_cloud->type){
            return 1;
        }else{
            return 0;
        }
    }else if(match_type == MAP_MATCH_TYPE_PLATFORM_BY_ID){
        if(a_map_qm_cloud->attr_id == b_map_qm_cloud->attr_id){
            return 1;
        }else{
            return 0;
        }
    }
    return 0;
}
#endif