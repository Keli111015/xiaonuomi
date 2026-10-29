
#ifndef _IOT_DEVICE_H_
#define _IOT_DEVICE_H_
#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_iot_import.h"

#define MQTT_SDK_VERSION    "2.0"

/* From device.h */
#define PRODUCT_KEY_LEN       (6)
#define PRODUCT_ID_LEN        (8)
#define DEVICE_ID_LEN          (6)
#define DEVICE_SECRET_LEN      (32)
#define PRODUCT_SECRET_LEN     (32)
#define DEVELOPER_KEY_LEN      (32)
#define DEVELOPER_SECRET_LEN   (32)
#define DEVICE_VERSION_LEN   (16)
#define DEVICE_SN_LEN        (64)

#define HOST_ADDRESS_LEN    (32)
#define HOST_PORT_LEN       (8)
#define CLIENT_ID_LEN       (96)
#define USER_NAME_LEN       (32)   /* Extend length for ID2 */
#define PASSWORD_LEN        (48)   /* Extend length for ID2 */

typedef struct {
    unsigned int product_id;
    char        device_id[DEVICE_ID_LEN + 1];
    char        device_secret[DEVICE_SECRET_LEN + 1];
} iot_device_info_t, *iot_device_info_pt;

typedef struct {
    uint16_t        port;
    char            host_name[HOST_ADDRESS_LEN + 1];
    char            client_id[CLIENT_ID_LEN + 1];
    char            username[USER_NAME_LEN + 1];
    char            password[PASSWORD_LEN + 1];
    const char     *pub_key;
} iot_conn_info_t, *iot_conn_info_pt;

int     iot_device_info_init(void);

int     iot_device_info_set(
            unsigned int product_key,
            const char *device_id,
            const char *device_secret);

iot_device_info_pt iot_device_info_get(void);
iot_conn_info_pt iot_conn_info_get(void);

#if defined(__cplusplus)
}
#endif
#endif  /* #ifndef _IOT_DEVICE_H_ */
