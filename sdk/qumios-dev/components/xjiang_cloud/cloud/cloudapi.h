#ifndef _CLOUDAPI_H_
#define _CLOUDAPI_H_

#include "qm_iot_import.h"
#include <iot_device.h>


typedef enum
{
    EVENT_VALUE_LOGIN   = 1,
    EVENT_VALUE_LOGOUT  = 2,
    EVENT_VALUE_TOKEN_REPORT_SUC = 3,
    EVENT_VALUE_BIND_NOTIFY = 4,
    EVENT_VALUE_UNBIND_NOTIFY = 5,
    EVENT_VALUE_RESET = 6,
    EVENT_VALUE_OTA_REQUEST = 7,
    EVENT_VALUE_OTA_DATA = 8,
    EVENT_VALUE_OTA_DATA_PROGRESS = 9,
    EVENT_VALUE_OTA_VERIFY = 10,

}event_value_t;

typedef enum
{
    MSG_TYPE_MIN,
    MSG_TYPE_REPORT,
    MSG_TYPE_RAW_DATA_REPORT,
    MSG_TYPE_RAW_DATA_REPLY,
    MSG_TYPE_SET_REPLY,
    MSG_TYPE_TOKEN_REPORT,
    MSG_TYPE_EVENT_NOTIFY_REPLY,
    MSG_TYPE_RESET,
    MSG_TYPE_PRODTST_REPORT,
    MSG_TYPE_PRODTST_SET_REPLY,
    MSG_TYPE_PRODTST_TEST,
    MSG_TYPE_PRODTST_GET_REPLY,
    MSG_TYPE_PRODTST_CONFIG_REPLY,
    MSG_TYPE_PRODTST_RESULT_REPLY,
    MSG_TYPE_PRODTST_RESULT_REPORT,
    MSG_TYPE_PRODTST_SPCL_UP,
    MSG_TYPE_PRODTST_SPCL_DOWN_REPLY,
    MSG_TYPE_MAX,
}msg_type_t;


typedef enum {
    IOT_DEV_TYPE_MASTER,
    IOT_DEV_TYPE_SLAVE,
    IOT_DEV_TYPE_MAX
} iot_dev_type_t;

typedef struct {
    uint32_t product_id;
    char device_id[DEVICE_ID_LEN+1];
    char device_secret[DEVICE_SECRET_LEN+1];
}iot_dev_meta_info_t;

#define  IOT_MASTER_DEVID   (0)
#define  IOT_MCU_DEVID      (1)

const char* sdk_version_get(void);

int32_t app_version_set(char *verison);

char *app_version_get(void);

char *mcu_version_get(void);

int32_t mcu_version_set(char *version);

int32_t cloud_open(iot_dev_type_t dev_type, iot_dev_meta_info_t *meta_info);

int32_t cloud_connect(void);

int32_t cloud_packet_send(int devid, msg_type_t msg_type, unsigned char *payload, int payload_len);

int32_t cloud_packet_reply_send(int devid, msg_type_t msg_type, int id, int code, unsigned char *payload, int payload_len);

int32_t cloud_token_report(char* token);

int32_t cloud_device_reset(void);

void cloud_loop(void);



#endif
