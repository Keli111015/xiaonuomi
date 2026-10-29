#ifndef IOT_GENERIC_H
#define IOT_GENERIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_tlv.h"
#include "qm_types.h"

#ifndef CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT   0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT        0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_ALI_IOT_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_ALI_IOT_SUPPORT    0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT         0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT      0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT      0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_HONOR_BLE_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_HONOR_BLE_SUPPORT      0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT    0
#endif

#ifndef CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT
#define CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT     0
#endif

#ifndef CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
#define CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT        0
#endif

#ifndef CONFIG_IOT_GENERIC_NETWORK_BLE_SUPPORT
#define CONFIG_IOT_GENERIC_NETWORK_BLE_SUPPORT         0
#endif

#ifndef CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
#define CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT        0
#endif

#ifndef CONFIG_IOT_GENERIC_EVENT_SUPPORT
#define CONFIG_IOT_GENERIC_EVENT_SUPPORT               0
#endif

#ifndef CONFIG_IOT_GENERIC_ACTION_SUPPORT
#define CONFIG_IOT_GENERIC_ACTION_SUPPORT              0
#endif

#ifndef CONFIG_IOT_GENERIC_RAWDATA_SUPPORT
#define CONFIG_IOT_GENERIC_RAWDATA_SUPPORT             0
#endif

#ifndef CONFIG_IOT_MAP_VERSION_V2
#define CONFIG_IOT_MAP_VERSION_V2   0
#endif 

typedef enum {
    IOT_GENERIC_DEV_TYPE_MASTER,
    IOT_GENERIC_DEV_TYPE_SLAVE,
    IOT_GENERIC_DEV_TYPE_MAX,
} iot_generic_dev_type_t;

typedef enum {
#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_ALI,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_ALI_IOT_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_ALI_IOT,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_MI,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_HONOR_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_HONOR,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_HARMONY_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_HARMONY,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_XJIANG_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_XJIANG,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_BAIDU,
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_QM_CLOUD_SUPPORT
    IOT_GENERIC_PLATFORM_TYPE_QM_CLOUD,
#endif
    IOT_GENERIC_PLATFORM_TYPE_MAX,
}iot_generic_platform_type_t;

typedef enum {
#if CONFIG_IOT_GENERIC_NETWORK_WIFI_SUPPORT
    IOT_GENERIC_NETWORK_TYPE_WIFI,
#endif
#if CONFIG_IOT_GENERIC_NETWORK_BLE_SUPPORT
    IOT_GENERIC_NETWORK_TYPE_BLE,
#endif
#if CONFIG_IOT_GENERIC_NETWORK_MESH_SUPPORT
    IOT_GENERIC_NETWORK_TYPE_MESH,
#endif
    IOT_GENERIC_NETWORK_TYPE_MAX,
}iot_generic_network_type_t;

typedef enum {
    IOT_GENERIC_MSG_TYPE_SET,
    IOT_GENERIC_MSG_TYPE_GET,
    IOT_GENERIC_MSG_TYPE_REPORT,
    IOT_GENERIC_MSG_TYPE_EVENT,
    IOT_GENERIC_MSG_TYPE_ACTION,
    IOT_GENERIC_MSG_TYPE_RAWDATA,
    IOT_GENERIC_MSG_TYPE_MAX,
}iot_generic_msg_type_t;

typedef enum{
    IOT_GENERIC_EVENT_WIFI_CONFIG,
    IOT_GENERIC_EVENT_WIFI_CONFIG_FAIL,
    IOT_GENERIC_EVENT_BLE_CONFIG,
    IOT_GENERIC_EVENT_BLE_CONFIG_FAIL,
    IOT_GENERIC_EVENT_BLE_APP_CONFIG,
    IOT_GENERIC_EVENT_CONFIG_TIMEOUT,
    IOT_GENERIC_EVENT_GOT_SSID_PASSWD,
    IOT_GENERIC_EVENT_CONNECTING_ROUTER,
    IOT_GENERIC_EVENT_CONNECTED_ROUTER,
    IOT_GENERIC_EVENT_CONNECT_ROUTER_FAIL,
    IOT_GENERIC_EVENT_CONNECTING_CLOUD,
    IOT_GENERIC_EVENT_CONNECTED_CLOUD,
    IOT_GENERIC_EVENT_CONNECT_CLOUD_FAIL,
    IOT_GENERIC_EVENT_DEVICE_REGISTERED,
    IOT_GENERIC_EVENT_DEVICE_UNREGISTER,
    IOT_GENERIC_EVENT_DEVICE_OTA_START,
    IOT_GENERIC_EVENT_DEVICE_OTA_COMPLETE,
    IOT_GENERIC_EVENT_DEVICE_OTA_FAIL,
    IOT_GENERIC_EVENT_MAX,
    
}iot_generic_event_t;

typedef enum{

    IOT_OPTION_PRODUCT_ID,
    IOT_OPTION_MODEL,
    IOT_OPTION_PRODUCT_KEY,
    IOT_OPTION_PRODUCT_SECRET,
    IOT_OPTION_DEVICE_KEY,
    IOT_OPTION_DEVICE_SECRET,
    IOT_OPTION_PUBLISH_LIST,
    IOT_OPTION_MESH_GROUP,
    IOT_OPTION_MIIO_BLE_CONFIRM_BUTTON,
    IOT_OPTION_MIIO_BLE_CONFIRM_PAIR_ADV,
    IOT_OPTION_MAX,
}iot_generic_option_t;


typedef struct{

    void (*event_notify)(iot_generic_event_t event, void *ctx);
    int (*set)(qm_tlv_ctx_t *tlv_ctx, void *ctx);
    int (*get)(qm_tlv_ctx_t *tlv_ctx, void *ctx);
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    int (*action)(uint8_t aid, qm_tlv_ctx_t *action, qm_tlv_ctx_t *action_rsp, void *ctx);
#endif
#if CONFIG_IOT_GENERIC_RAWDATA_SUPPORT
    int (*down_rawdata)(char *buf, int size, void *ctx);
#endif
    void *ctx;
}iot_generic_hooks_t;

void *iot_generic_init(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type);

int iot_generic_option_set(void *handle, iot_generic_option_t option, void *data, int len);

int iot_generic_option_get(void *handle, iot_generic_option_t option, void *data, int *len);

int iot_generic_hooks_register(void *handle, iot_generic_hooks_t *hooks);

#if CONFIG_IOT_MAP_VERSION_V2
void *iot_generic_map_init(void *handle, int set_num);
void *iot_generic_map_get(void *map_ctx); 
#else
int iot_generic_map_set(void *handle, uint8_t *data, int len);
#endif

int iot_generic_report(void *handle, qm_tlv_block_t *tlv_block);

void *iot_generic_multi_report_start(void);
int iot_generic_multi_report(void *multi_handle, qm_tlv_block_t *tlv_block);
int iot_generic_multi_report_end(void *handle, void *multi_handle);

#if CONFIG_IOT_GENERIC_EVENT_SUPPORT

int iot_generic_event_report(void *handle, uint8_t eid, qm_tlv_block_t *tlv_block);

void *iot_generic_multi_event_report_start(uint8_t eid);
int iot_generic_multi_event_report(void *multi_handle, qm_tlv_block_t *tlv_block);
int iot_generic_multi_event_report_end(void *handle, void *multi_handle);

#endif

int iot_generic_reset(void *handle);
int iot_generic_deinit(void *handle);


#ifdef __cplusplus
}
#endif

#endif /* IOT_GENERIC_H */
