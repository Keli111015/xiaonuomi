#ifndef IOT_MAP_H
#define IOT_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"
#include "iot_generic.h"

typedef enum
{
    ID_MAP_MIN = 0,
    ID_MAP_ALI = 1, 
    ID_MAP_MI,  
    ID_MAP_ONENET,
    ID_MAP_HONOR,  
    ID_MAP_MAX,
}id_map_type_t;


typedef enum
{
    MAP_MATCH_TYPE_ID_BY_PLATFORM, 
    MAP_MATCH_TYPE_PLATFORM_BY_ID,
    MAP_MATCH_TYPE_MAX,
}map_match_type_t;

#ifndef CONFIG_IOT_MI_MAP_XIID_BYTE_LENGTH
#define CONFIG_IOT_MI_MAP_XIID_BYTE_LENGTH      2
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT
#define CONFIG_IOT_MAP_QM_CLOUD_SUPPORT         1
#else
#define CONFIG_IOT_MAP_QM_CLOUD_SUPPORT         0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
#define CONFIG_IOT_MAP_MI_SUPPORT               1
#else
#define CONFIG_IOT_MAP_MI_SUPPORT               0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT
#define CONFIG_IOT_MAP_HONOR_SUPPORT               1
#else
#define CONFIG_IOT_MAP_HONOR_SUPPORT               0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
#define CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT         1
#else
#define CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT         0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
#define CONFIG_IOT_MAP_XJIANG_WIFI_SUPPORT         1
#else
#define CONFIG_IOT_MAP_XJIANG_WIFI_SUPPORT         0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
#define CONFIG_IOT_MAP_ALI_WIFI_SUPPORT         1
#else
#define CONFIG_IOT_MAP_ALI_WIFI_SUPPORT         0
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT && CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
#define CONFIG_IOT_MAP_HARMONY_WIFI_SUPPORT         1
#else
#define CONFIG_IOT_MAP_HARMONY_WIFI_SUPPORT         0
#endif

#if CONFIG_IOT_MAP_QM_CLOUD_SUPPORT
typedef struct{
    uint8_t type_id;
    uint8_t attr_id;
    uint8_t siid;
    uint8_t type;
    uint16_t  xiid;
}iot_map_qm_cloud_t;
#endif

#if CONFIG_IOT_MAP_MI_SUPPORT
typedef struct{
    uint8_t type_id;
    uint8_t attr_id;
    uint8_t siid;
    uint8_t type;
    uint16_t  xiid;
}iot_map_mi_t;
#endif

#if CONFIG_IOT_MAP_HONOR_BLE_SUPPORT
typedef struct{
    uint8_t type_id;
    uint8_t attr_id;
    uint8_t siid;
    uint8_t type;
    uint16_t  xiid;
}iot_map_honor_ble_t;
#endif

#if CONFIG_IOT_MAP_HONOR_SUPPORT
#define HONOR_ID_MAX_LEN 30
#define HONOR_EID_MAX_LEN 5
typedef struct map_honor_unit{
    uint8_t type_id;
    uint8_t attr_id;
    uint8_t type;
    uint8_t propidx;

    uint8_t eiid_list[HONOR_EID_MAX_LEN];

    uint8_t pid_len;
    union{
            char *pid;
            char id[HONOR_ID_MAX_LEN + 1];
    }p;
    struct map_honor_unit *next;
}map_honor_unit_t;

typedef struct{
    uint8_t sid_len;
    union{
            char *sid;
            char id[HONOR_ID_MAX_LEN + 1];
    }s;
    map_honor_unit_t *map_honor_unit;
}iot_map_honor_t;
#endif

#if SERIAL_ONENET_ID_MAP_SUPPORT
typedef struct{
    uint8_t type_id;
    uint8_t attr_id;
    uint16_t func_id;
    uint8_t type;
}onenet_data_type_t;

#define ONENET_DATA_TYPE_LEN    (5)

#endif

#if CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT
typedef struct{
    uint8_t  type_id;
    uint8_t  attr_id;
    uint16_t baidu_id;
}iot_map_baidu_mesh_t;
#endif

#if CONFIG_IOT_MAP_VERSION_V2

typedef union{
#if CONFIG_IOT_MAP_QM_CLOUD_SUPPORT
    iot_map_qm_cloud_t map_qm_cloud;
#endif
#if CONFIG_IOT_MAP_MI_SUPPORT
    iot_map_mi_t map_mi;
#endif
#if CONFIG_IOT_MAP_HONOR_BLE_SUPPORT
    iot_map_honor_ble_t map_honor_ble;
#endif
#if CONFIG_ONENET_ID_MAP_SUPPORT
    onenet_data_type_t map_onenet;
#endif
#if CONFIG_IOT_MAP_HONOR_SUPPORT
    iot_map_honor_t map_honor;
#endif
#if CONFIG_IOT_MAP_BAIDU_MESH_SUPPORT
    iot_map_baidu_mesh_t map_mesh_baidu;
#endif
}iot_map_data_t;

typedef struct {
    void *(*parse)(iot_map_data_t *map_data);
    int  (*match)(map_match_type_t match_type, void *a, void *b);
}iot_map_hal_t; 

typedef struct{
    uint16_t num;
    uint16_t index;
    iot_map_hal_t *map_hal;
    iot_map_data_t *map_data;
}iot_map_ctx_t;

#else
typedef struct {
    int  (*parse)(void *handle, uint8_t *data, int len);
    int  (*match)(map_match_type_t match_type, void *a, void *b);
} iot_map_hal_t; 

typedef struct iot_map_data{
    struct iot_map_data *next;
    uint8_t map_data[1];
} iot_map_data_t;

typedef struct{
    iot_map_hal_t *map_hal;
    iot_map_data_t *head_map_data;
} iot_map_ctx_t;
#endif

int iot_map_hal_register(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_map_hal_t *map_hal);
iot_map_hal_t *iot_map_hal_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type);

#if CONFIG_IOT_MAP_VERSION_V2
void *iot_map_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, int init_num);
void *iot_map_get(void *map_handle);
#else
void *iot_map_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type);
int iot_map_parse(void *map_handle, uint8_t *data, int len);
int iot_map_add(void *handle, uint8_t *map_data, int len);
#endif

int iot_map_deinit(void *map_handle);
int iot_map_match(void *map_handle, map_match_type_t match_type, uint8_t *map_data, int len);


#ifdef __cplusplus
}
#endif

#endif /* IOT_MAP_H */