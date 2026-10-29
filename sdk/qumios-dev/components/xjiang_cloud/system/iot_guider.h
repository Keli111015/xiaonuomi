
#ifndef __IOT_GUIDER_H__
#define __IOT_GUIDER_H__

#include "qm_iot_import.h"
#include "iot_cloud_debug.h"
#include "qm_utils_md5.h"
#include "qm_utils_base64.h"
#include "qm_utils_hmac.h"
#include "iot_device.h"
#include "cloud_common.h"

#define GUIDER_DEFAULT_TS_STR       "2524608000000"
#define CONN_SECMODE_LEN            (32)
#define GUIDER_SIGN_LEN             (64)
#define GUIDER_TS_LEN               (16)


// #define IOT_WITHOUT_TLS
#define MQTT_DIRECT


#ifdef TEST_CLOUD
#define GUIDER_DIRECT_DOMAIN    "192.168.1.50"
#else
#define GUIDER_DIRECT_DOMAIN    "xjemq.xjiangiot.com"
#endif


typedef enum {
    /* China */
    IOT_XJ_CLOUD_REGION_CHINA,

    /* US */
    IOT_XJ_CLOUD_REGION_US,

    /* EUR */
    IOT_XJ_CLOUD_REGION_EUR,

    /* SEA*/
    IOT_XJ_CLOUD_REGION_SEA,

        /* SEA*/
    IOT_XJ_CLOUD_REGION_TEST,

    /* Maximum number of custom region */
    IOT_XJ_CLOUD_REGION_MAX
} iot_xj_cloud_region_types_t;


#define XJ_GUIDER_DYNAMIC_URL_LEN  (64)


typedef enum {
    GUIDER_XJ_DOMAIN_MQTT,
    GUIDER_XJ_DOMAIN_HTTP,
    GUIDER_XJ_DOMAIN_MAX
} domain_xj_type_t;
    



#ifdef IOT_WITHOUT_TLS
#define GUIDER_DIRECT_PORT     1884
#else
#define GUIDER_DIRECT_PORT     8883
#endif


#define SHA_METHOD              "hmacsha1"
#define MD5_METHOD              "hmacmd5"

/* By default we use hmac-sha1 algorithm for hmac in PK/DN/DS case */
#define USING_SHA1_IN_HMAC      (1)

typedef enum _SECURE_MODE {
    MODE_TLS_GUIDER             = -1,
    MODE_TCP_GUIDER_PLAIN       = 0,
    MODE_TCP_GUIDER_ID2_ENCRYPT = 1,
    MODE_TLS_DIRECT             = 2,
    MODE_TCP_DIRECT_PLAIN       = 3,
    MODE_TCP_DIRECT_ID2_ENCRYPT = 4,
    MODE_TLS_GUIDER_ID2_ENCRYPT = 5,
    MODE_TLS_DIRECT_ID2_ENCRYPT = 7,
} secure_mode_t;

extern const char *iot_secmode_str[];

/** @defgroup group_api_conninfo conninfo
 *  @{
 */
/**
 * @brief Based on the 'product_key' + 'device_name' + 'device_secret' produce an MQTT connection username and password.
 *
 * @param [in] product_key: @n Apply for 'product_key' in the AliYun Console.
 * @param [in] device_name: @n Apply for 'device_name' in the AliYun Console.
 * @param [in] device_secret: @n Apply for 'device_secret' in the AliYun Console.
 * @param [out] info_ptr: @n return MQTT connection parameter.
 *
 * @retval -1 : Fail.
 * @retval  0 : Success.
 * @see None.
 */
int     iot_setup_conn_info(unsigned int product_id,
                          const char *device_id,
                          const char *device_secret,
                          void **info_ptr);


secure_mode_t iot_secure_mode_num(void);
void iot_guider_print_dev_guider_info(iot_device_info_pt dev,
                                  int secure_mode,
                                  char *time_stamp,
                                  char *guider_sign,
                                  char *id2,
                                  char *dev_code);
void iot_guider_print_conn_info(iot_conn_info_pt conn);

int iot_guider_authenticate(void);

int iot_guider_set_region_id(int region_id);

int iot_guider_set_dynamic_url(char *p_mqtt_url);
int iot_guider_get_dynamic_url(char *p_mqtt_url, int mqtt_url_buff_len);

int iot_guider_get_domain(domain_xj_type_t domain_type, char *domain, int len);


#endif  /* __GUIDER_INTERNAL_H__ */
