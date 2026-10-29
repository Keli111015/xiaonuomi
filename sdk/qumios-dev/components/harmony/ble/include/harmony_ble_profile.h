#ifndef __HARMONY_BLE_PROFILE_H__
#define __HARMONY_BLE_PROFILE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#define HARMONY_BLE_NAME_LEN   (16)
#define HARMONY_BLE_BUFF_SIZE    (8)

typedef enum{
    HARMONY_BLE_PROFILE_DATA_TYPE_BOOL,
    HARMONY_BLE_PROFILE_DATA_TYPE_NUMBER,   //int类型
    HARMONY_BLE_PROFILE_DATA_TYPE_STRING,
    HARMONY_BLE_PROFILE_DATA_TYPE_MAX,
}harmony_ble_profile_data_type_t;

typedef struct harmony_ble_data
{
    char data_id[HARMONY_BLE_NAME_LEN + 1];
    int len;
    harmony_ble_profile_data_type_t type;
    union 
    {
        uint8_t *pdata;
        uint8_t bytes[HARMONY_BLE_BUFF_SIZE];
    }value;
    struct harmony_ble_data *next;
}harmony_ble_data_t;

typedef struct harmony_ble_service
{
    int count;
    char sid[HARMONY_BLE_NAME_LEN + 1];
    harmony_ble_data_t *data_param;
    struct harmony_ble_service *next;
}harmony_ble_service_t;

typedef struct 
{
    int count;
    harmony_ble_service_t *service;
}harmony_ble_profile_t;

harmony_ble_profile_t *harmony_ble_profile_creat(void);
int harmony_ble_profile_delete(harmony_ble_profile_t *profile);

harmony_ble_service_t *harmony_ble_service_creat(void);
int harmony_ble_service_delete(harmony_ble_service_t *service);
int harmony_ble_service_add(harmony_ble_profile_t *profile, harmony_ble_service_t *service);
harmony_ble_service_t *harmony_ble_service_find(harmony_ble_profile_t *profile, char *service_id);
int harmony_ble_service_id_set(harmony_ble_service_t *service, char *id_name);
char *harmony_ble_service_id_get(harmony_ble_service_t *service);

harmony_ble_service_t *harmony_ble_service_next(harmony_ble_profile_t *profile, harmony_ble_service_t *service);
harmony_ble_data_t *harmony_ble_data_next(harmony_ble_service_t *service, harmony_ble_data_t *data);

harmony_ble_data_t *harmony_ble_data_creat(void);
int harmony_ble_data_delete(harmony_ble_data_t *data);
harmony_ble_profile_data_type_t harmony_ble_data_type_get(harmony_ble_data_t *data);
int harmony_ble_data_add(harmony_ble_service_t *service, harmony_ble_data_t *data);
harmony_ble_data_t *harmony_ble_data_find(harmony_ble_service_t *profile, char *data_id);

int harmony_ble_data_id_set(harmony_ble_data_t *data, char *id_name);
char *harmony_ble_data_id_get(harmony_ble_data_t *data);

int harmony_ble_data_pack_bool(harmony_ble_data_t *data, bool_t value);
int harmony_ble_data_unpack_bool(harmony_ble_data_t *data, bool_t *value);

int harmony_ble_data_pack_number(harmony_ble_data_t *data, int value);
int harmony_ble_data_unpack_number(harmony_ble_data_t *data, int *value);

int harmony_ble_data_pack_string(harmony_ble_data_t *data, char *value);
int harmony_ble_data_unpack_string(harmony_ble_data_t *data, char *value);

#ifdef __cplusplus
}
#endif

#endif