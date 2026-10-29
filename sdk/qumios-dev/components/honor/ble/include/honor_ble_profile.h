#ifndef __HONOR_BLE_PROFILE_H__
#define __HONOR_BLE_PROFILE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#define HONOR_BLE_NAME_LEN   (16)
#define HONOR_BLE_BUFF_SIZE    (8)

typedef enum{
    HONOR_BLE_PROFILE_DATA_TYPE_BOOL,
    HONOR_BLE_PROFILE_DATA_TYPE_NUMBER,   //int类型
    HONOR_BLE_PROFILE_DATA_TYPE_STRING,
    HONOR_BLE_PROFILE_DATA_TYPE_MAX,
}honor_ble_profile_data_type_t;

typedef struct honor_ble_data
{
    char data_id[HONOR_BLE_NAME_LEN + 1];
    int len;
    honor_ble_profile_data_type_t type;
    union 
    {
        uint8_t *pdata;
        uint8_t bytes[HONOR_BLE_BUFF_SIZE];
    }value;
    struct honor_ble_data *next;
}honor_ble_data_t;

typedef struct honor_ble_service
{
    int count;
    char sid[HONOR_BLE_NAME_LEN + 1];
    honor_ble_data_t *data_param;
    struct honor_ble_service *next;
}honor_ble_service_t;

typedef struct 
{
    int count;
    honor_ble_service_t *service;
}honor_ble_profile_t;



#ifdef __cplusplus
}
#endif

#endif