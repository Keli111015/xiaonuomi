#ifndef IOT_GENERIC_HAL_H
#define IOT_GENERIC_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "iot_generic.h"


typedef struct{

    int (*init)(void *arg);
    int (*report)(void *map_handle, qm_tlv_ctx_t *tlv_ctx);
    int (*set)(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
    int (*get_req)(char *data, int len, void *map_handle, qm_tlv_ctx_t *tlv_ctx);
    int (*get_rsp)(void *map_handle, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);
#if CONFIG_IOT_GENERIC_EVENT_SUPPORT
    int (*event)(void *map_handle, uint8_t eid, qm_tlv_ctx_t *tlv_ctx);
#endif
#if CONFIG_IOT_GENERIC_ACTION_SUPPORT
    int (*action_req)(char *data, int len, void *map_handle, uint8_t *aid, qm_tlv_ctx_t *tlv_ctx);
    int (*action_rsp)(void *map_handle, uint8_t aid, qm_tlv_ctx_t *tlv_ctx, char *data, int *len);
#endif
#if CONFIG_IOT_GENERIC_RAWDATA_SUPPORT
    int (*up_rawdata)(char *data, int len);
#endif
    int (*option_set)(iot_generic_option_t option, void *data, int len);
    
    int (*reset)(void *arg);
    int (*deinit)(void *arg);

}iot_generic_hal_t;

int iot_generic_hal_register(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_generic_hal_t *eneric_hal);

int iot_generic_set(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *data, int len);
int iot_generic_event_notify(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, iot_generic_event_t event);
int iot_generic_get(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *in_data, int in_len, char *out_data, int *out_len);
int iot_generic_action(iot_generic_platform_type_t platform_type, iot_generic_network_type_t network_type, char *in_data, int in_len, char *out_data, int *out_len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_GENERIC_HAL_H */