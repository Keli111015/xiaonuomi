#ifndef _CLOUD_COMMON_H_
#define _CLOUD_COMMON_H_

#include "cloudapi.h"
#include "qm_utils_list.h"
#include "iot_device.h"
#include "qm_config.h"
#include "qm_types.h"

#define SDK_VERSION       "3.0"


//#define TEST_CLOUD

#ifndef CONFIG_OTA_SUPPORT
#define CONFIG_OTA_SUPPORT      1
#endif

#define STANDARD_SUPPORT
#define RAW_SUPPORT

#ifndef CONFIG_DYN_REG_SUPPORT
#define CONFIG_DYN_REG_SUPPORT  1
#endif

//#define MCU_OTA_SUPPORT
//#define DEVICE_GATEWAY_SUPPORT

#define NO_ERR                      0
#define PARAM_ERR                   -1
#define ALLOC_MEM_ERR               -2
#define PARAM_LEN_ERR               -3
#define PACKET_ID_ERR               -4
#define PACKET_TIME_OUT             -5
#define DEVICE_LOGOUT               -6
#define NETWORK_ERR                 -7
#define NO_INIT_ERR                 -8
#define DEVICE_SN_NOT_EXIST_ERR     -9
#define DEVICE_AUTH_TOKEN_ERR       -10
#define DEVICE_INFO_NOT_EXIST       -11
#define DEVICE_TYPE_ERR             -12
#define XCLOUD_ERR                  -13


#define PACKET_ID_MAX  65535

#define BYTE_ORDER      LITTLE_ENDIAN

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define util_ntohs(x) util_htons(x)
#define util_htons(x) ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#else
#if BYTE_ORDER == BIG_ENDIAN
#define util_htons(x) (x)
#define util_ntohs(x) (x)
#endif
#endif


typedef enum
{
    UP_STANDARD = 1,
    DOWN_STANDARD = 2,
    UP_STANDARD_REPLY = 3,
    DOWN_STANDARD_REPLY = 4,
    UP_RAW = 5,
    DOWN_RAW = 6,
}packet_type_t;


typedef enum
{
    CONFIG_FROM_FLASH = 1,
    CONFIG_FROM_RAM = 2,
    CONFIG_FROM_CLOUD = 3,
}config_type_t;


typedef struct
{
    uint8_t is_ack;
    msg_type_t msg_type;
    uint16_t mqtt_packet_id;
    uint16_t packet_id;
    uint8_t *data;
    uint16_t len;
}packet_info_t;

typedef struct
{
    char version[DEVICE_VERSION_LEN+1];
    char mcu_ver[DEVICE_VERSION_LEN+1];
    char device_id[DEVICE_ID_LEN+1];
    char device_secret[DEVICE_SECRET_LEN+1];
    uint32_t product_id;
    char product_secret[PRODUCT_SECRET_LEN+1];
}product_info_t;


enum
{
  NORMAL_LOOP_TIME = 300,
  FAST_LOOP_TIME = 50,
};

typedef struct
{ 
    uint8_t is_auth;
    uint8_t is_login;
    uint8_t config_type; //default read configuration from flash,except restart restart wifi config
    int loop_timeout;
    void *pclient;
    void *lock;
    qm_list_t *push_list;
    qm_list_t *push_ack_list;

    uint16_t packet_id;
    
    product_info_t product_info;

}cloud_context_t;


int mqtt_client(void *arg);

void *cloud_context_get(void);
char *app_version_get(void);
int32_t push_ack_info_add(cloud_context_t *cloud_context, packet_info_t *push_info);
int32_t push_ack_info_remove(cloud_context_t *cloud_context, uint16_t mqtt_packet_id, packet_info_t **push_info);
int32_t cloud_generic_send(int devid, msg_type_t msg_type, char *id, char* method, int code, unsigned char *paylaod, int paylaod_len);
int32_t cloud_event_reply(int devid, char* id, char* method, int code, unsigned char* paylaod, int paylaod_len);




#endif
