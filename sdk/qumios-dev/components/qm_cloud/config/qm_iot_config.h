#ifndef __QM_IOT_CONFIG_H__
#define __QM_IOT_CONFIG_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"

#define CONFIG_QM_IOT_SDK_VESRION               ("1.3.1")

#ifndef CONFIG_QM_IOT_SET_CANCEL
#define CONFIG_QM_IOT_SET_CANCEL               0
#endif

#ifndef CONFIG_QM_IOT_GET_CANCEL
#define CONFIG_QM_IOT_GET_CANCEL               0
#endif

#ifndef CONFIG_QM_IOT_SPEC_SUPPORT
#define CONFIG_QM_IOT_SPEC_SUPPORT              1
#endif

#ifndef CONFIG_QM_IOT_AUTHCODE_SUPPORT
#define CONFIG_QM_IOT_AUTHCODE_SUPPORT          0
#endif

#ifndef CONFIG_QM_IOT_LOG_PUSH_SUPPORT
#define CONFIG_QM_IOT_LOG_PUSH_SUPPORT          1
#endif

#ifndef CONFIG_QM_IOT_LOG_PUSH_MAX_LEN
#define CONFIG_QM_IOT_LOG_PUSH_MAX_LEN          2048
#endif

#ifndef CONFIG_QM_IOT_AUTHCODE_MAX_LEN
#define CONFIG_QM_IOT_AUTHCODE_MAX_LEN           64
#endif

#ifndef CONFIG_QM_IOT_EVENT_ID
#define CONFIG_QM_IOT_EVENT_ID             (0xF000)
#endif

#ifndef CONFIG_QM_IOT_TASK_USER_ID
#define CONFIG_QM_IOT_TASK_USER_ID       (1)
#endif

#ifndef CONFIG_QM_IOT_TASK_PRIORITY
#define CONFIG_QM_IOT_TASK_PRIORITY             (10)
#endif

#ifndef CONFIG_QM_IOT_TASK_STACK_SIZE
#define CONFIG_QM_IOT_TASK_STACK_SIZE           (8144)
#endif

#ifndef CONFIG_QM_IOT_TASK_QUEUE_NUM
#define CONFIG_QM_IOT_TASK_QUEUE_NUM       (10)
#endif

#ifndef CONFIG_QM_IOT_DYNREG_PORT
#define CONFIG_QM_IOT_DYNREG_PORT         (17605)
#endif

#ifndef CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS
#define CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS  (10 * 1000)
#endif

#ifndef CONFIG_QM_IOT_DYNREG_RETRY_COUNT
#define CONFIG_QM_IOT_DYNREG_RETRY_COUNT   (5)
#endif

#ifndef CONFIG_QM_IOT_MQTT_KEEPALIVE_S
#define CONFIG_QM_IOT_MQTT_KEEPALIVE_S      (60)
#endif

#ifndef CONFIG_QM_IOT_MQTT_REPUB_MS
#define CONFIG_QM_IOT_MQTT_REPUB_MS        (1 * 1000)
#endif

#ifndef CONFIG_QM_IOT_MQTT_CLIENT_ID_LEN
#define CONFIG_QM_IOT_MQTT_CLIENT_ID_LEN        (128)
#endif


#ifndef CONFIG_QM_IOT_SN_KV_SUPPORT
#define CONFIG_QM_IOT_SN_KV_SUPPORT              (0)
#endif

#ifndef CONFIG_QM_IOT_AUDIO_URI_SUPPORT
#define CONFIG_QM_IOT_AUDIO_URI_SUPPORT          0
#endif

#ifndef CONFIG_QM_IOT_OTA_SUPPORT
#define CONFIG_QM_IOT_OTA_SUPPORT                1
#endif

#ifndef CONFIG_QM_IOT_SLIENT_OTA_SUPPORT
#define CONFIG_QM_IOT_SLIENT_OTA_SUPPORT         1
#endif

#ifndef CONFIG_QM_IOT_OTA_PROGRESS_GRADE
/** @brief OTA 进度上报步进（百分比）。 */
#define CONFIG_QM_IOT_OTA_PROGRESS_GRADE        5
#endif

#ifndef CONFIG_QM_IOT_OTAV2_CHUNK_SIZE
/** @brief OTAv2 默认请求分片大小，单位字节。 */
#define CONFIG_QM_IOT_OTAV2_CHUNK_SIZE           4096
#endif

#ifndef CONFIG_QM_IOT_OTAV2_CHUNK_SIZE_MAX
/** @brief OTAv2 单片最大字节数（协议上限 64KB）。 */
#define CONFIG_QM_IOT_OTAV2_CHUNK_SIZE_MAX       (64 * 1024)
#endif

#ifndef CONFIG_QM_IOT_OTAV2_RETRY_COUNT
/** @brief OTAv2 分片请求超时重试次数。 */
#define CONFIG_QM_IOT_OTAV2_RETRY_COUNT          10
#endif

#ifndef CONFIG_QM_IOT_OTAV2_TIMEOUT_MS
/** @brief OTAv2 分片请求超时时间，单位毫秒。 */
#define CONFIG_QM_IOT_OTAV2_TIMEOUT_MS           (5 * 1000)
#endif

#ifndef CONFIG_QM_IOT_MQTT_SUPPORT
#define CONFIG_QM_IOT_MQTT_SUPPORT               1
#endif

/** @brief @ref qm_iot_event_register 可追加注册的回调槽位数（静态数组，无堆分配）。 */
#ifndef CONFIG_QM_IOT_EVENT_LISTENER_NUM
#define CONFIG_QM_IOT_EVENT_LISTENER_NUM         4
#endif

#ifndef CONFIG_QM_IOT_PUB_SUPPORT
#define CONFIG_QM_IOT_PUB_SUPPORT               1
#endif

#ifndef CONFIG_QM_IOT_DEVMNT_SUPPORT
#define CONFIG_QM_IOT_DEVMNT_SUPPORT             1
#endif

#ifndef CONFIG_QM_IOT_DYNREG_SUPPORT
#define CONFIG_QM_IOT_DYNREG_SUPPORT             1
#endif

#ifndef CONFIG_QM_IOT_NTP_SUPPORT
#define CONFIG_QM_IOT_NTP_SUPPORT                1
#endif

#ifndef CONFIG_QM_IOT_WEATHER_SUPPORT
#define CONFIG_QM_IOT_WEATHER_SUPPORT            1
#endif

#ifndef CONFIG_QM_IOT_MQTT_YIELD_TIME
#define CONFIG_QM_IOT_MQTT_YIELD_TIME     100
#endif

#ifndef CONFIG_QM_IOT_REQ_INTERVAL
#define CONFIG_QM_IOT_REQ_INTERVAL           (60 * 60)
#endif


#ifndef CONFIG_QM_IOT_DID_MAX_LEN
#define CONFIG_QM_IOT_DID_MAX_LEN               16
#endif

#ifndef CONFIG_QM_IOT_PRODUCT_SECRET_MAX_LEN
#define CONFIG_QM_IOT_PRODUCT_SECRET_MAX_LEN    32
#endif

#ifndef CONFIG_QM_IOT_MQTT_HOST_MAX_LEN
#define CONFIG_QM_IOT_MQTT_HOST_MAX_LEN         64
#endif

#ifndef CONFIG_QM_IOT_MQTT_USERNAME_MAX_LEN
/** @brief MQTT 用户名最大长度，不含结尾 '\0'。 */
#define CONFIG_QM_IOT_MQTT_USERNAME_MAX_LEN     128
#endif

#ifndef CONFIG_QM_IOT_AUDIO_URI_MAX_LEN
#define CONFIG_QM_IOT_AUDIO_URI_MAX_LEN         64
#endif

#ifndef CONFIG_QM_IOT_CRT_MAX_LEN
#define CONFIG_QM_IOT_CRT_MAX_LEN               2048
#endif

#ifndef CONFIG_QM_IOT_OTA_CRT_MAX_LEN
#define CONFIG_QM_IOT_OTA_CRT_MAX_LEN           384
#endif

#ifndef CONFIG_QM_IOT_DYNREG_MAX_LEN
#define CONFIG_QM_IOT_DYNREG_MAX_LEN           384
#endif

#ifndef CONFIG_QM_IOT_SN_MAX_LEN
#define CONFIG_QM_IOT_SN_MAX_LEN                32
#endif

#ifndef CONFIG_QM_IOT_OTA_MCU_SUPPORT
#define CONFIG_QM_IOT_OTA_MCU_SUPPORT           0
#endif

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT

#ifndef CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES
#define CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES       4
#endif

#ifndef CONFIG_QM_IOT_MCU_VER_STR_MAX_LEN
#define CONFIG_QM_IOT_MCU_VER_STR_MAX_LEN       16
#endif

#ifndef CONFIG_QM_IOT_MCU_VER_EXTRA_MAX_LEN
#define CONFIG_QM_IOT_MCU_VER_EXTRA_MAX_LEN     64
#endif

#endif

#ifndef CONFIG_QM_IOT_VER_STR_MAX_LEN
#define CONFIG_QM_IOT_VER_STR_MAX_LEN           16
#endif


typedef struct {
    enum{
        QM_IOT_NOT_RESET = 0,
        QM_IOT_RESET_UNPROV = 1,
    }is_reset;
}qm_iot_reset_info_t;

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
typedef struct {
    int type_id;
    char *version;
    char *extra;
}qm_iot_mcu_ver_t;

typedef struct {
    uint32_t num;
    qm_iot_mcu_ver_t *version;
}qm_iot_mcu_ver_info_t;
#endif


int32_t qm_iot_reset_info_set(void *iot_handle, qm_iot_reset_info_t *reset_info);
int32_t qm_iot_reset_info_get(void *iot_handle, qm_iot_reset_info_t *reset_info);

int32_t qm_iot_pid_set(void *iot_handle, uint32_t pid);
uint32_t qm_iot_pid_get(void *iot_handle);

int32_t qm_iot_did_set(void *iot_handle, uint32_t did);
uint32_t qm_iot_did_get(void *iot_handle);

typedef struct {
    char host[CONFIG_QM_IOT_MQTT_HOST_MAX_LEN + 1];
    uint16_t port;
}qm_iot_cloud_info_t;

int32_t qm_iot_cloud_info_set(void *iot_handle, qm_iot_cloud_info_t *cloud_info);
int32_t qm_iot_cloud_info_get(void *iot_handle, qm_iot_cloud_info_t *cloud_info);

int32_t qm_iot_mqtt_username_set(void *iot_handle, char *username, uint32_t len);
char *qm_iot_mqtt_username_get(void *iot_handle);

int32_t qm_iot_server_crt_set(void *iot_handle, char *server_crt, uint32_t len);
int32_t qm_iot_server_crt_get(void *iot_handle, char *server_crt, uint32_t *len);

int32_t qm_iot_client_crt_set(void *iot_handle, char *client_crt, uint32_t len);
int32_t qm_iot_client_crt_get(void *iot_handle, char *client_crt, uint32_t *len);

int32_t qm_iot_client_key_set(void *iot_handle, char *client_key, uint32_t len);
int32_t qm_iot_client_key_get(void *iot_handle, char *client_key, uint32_t *len);

int32_t qm_iot_ota_public_key_set(void *iot_handle, char *public_key, uint32_t len);
int32_t qm_iot_ota_public_key_get(void *iot_handle, char *public_key, uint32_t *len);

int32_t qm_iot_version_str_set(void *iot_handle, char *version);
char *qm_iot_version_str_get(void *iot_handle);

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
int32_t qm_iot_mcu_version_set(void *iot_handle, qm_iot_mcu_ver_t *ver_info, uint16_t num);
qm_iot_mcu_ver_info_t *qm_iot_mcu_version_get(void *iot_handle);
#endif

int32_t qm_iot_prodtuct_sercet_set(void *iot_handle, char *prodtuct_sercet, int len);

char *qm_iot_prodtuct_sercet_get(void *iot_handle);

#if CONFIG_QM_IOT_AUDIO_URI_SUPPORT
int qm_iot_audio_uri_set(void *iot_handle, char *uri, uint32_t len);
int32_t qm_iot_audio_uri_get(void *iot_handle, char *uri, uint32_t *len);
int qm_iot_agora_uri_set(void *iot_handle, char *uri, uint32_t len);
int32_t qm_iot_agora_uri_get(void *iot_handle, char *uri, uint32_t *len);
#endif

#if CONFIG_QM_IOT_AUTHCODE_SUPPORT
char *qm_iot_authcode_get(void *iot_handle);
int qm_iot_authcode_set(void *iot_handle, char *auth_code, uint32_t len);
#endif

char *qm_iot_sn_get(void *iot_handle);
int qm_iot_sn_set(void *iot_handle, char *sn, uint32_t len);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_CONFIG_H__ */
