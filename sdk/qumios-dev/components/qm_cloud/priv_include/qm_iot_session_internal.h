#ifndef __QM_IOT_SESSION_INTERNAL_H__
#define __QM_IOT_SESSION_INTERNAL_H__

/**
 * @file qm_iot_session_internal.h
 * @brief IOT 会话内部结构，供 @ref qm_iot_api.c 与 @ref qm_iot_config.c 共享。
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "qm_work.h"
#include "qm_iot_config.h"
#include "qm_iot_api.h"
#include "qm_utils_list.h"

#ifndef CONFIG_QM_IOT_SERVICE_SUPPORT
#define CONFIG_QM_IOT_SERVICE_SUPPORT           (1)
#endif

#if CONFIG_QM_IOT_SERVICE_SUPPORT
typedef struct {
    int service_id;
    qm_iot_service_handler_t service_fn;
    void *userdata;
    int service_num;
    uint8_t raw_iid[1];
} qm_iot_service_entry_t;
#endif

typedef struct {
    qm_iot_cloud_info_t cloud_info;
    char mqtt_user_name[CONFIG_QM_IOT_MQTT_USERNAME_MAX_LEN + 1];
    char ca_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
    char client_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
    char client_key[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
    char ota_signatrure[CONFIG_QM_IOT_OTA_CRT_MAX_LEN + 1];
} qm_iot_credential_cache_t;

/** @brief @ref qm_iot_event_register 静态槽位载荷。 */
typedef struct {
    qm_iot_event_handler_t handler;
    void *userdata;
} qm_iot_event_listener_t;

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
typedef struct {
    int type_id;
    char version[CONFIG_QM_IOT_MCU_VER_STR_MAX_LEN + 1];
    char extra[CONFIG_QM_IOT_MCU_VER_EXTRA_MAX_LEN + 1];
} qm_iot_mcu_ver_slot_t;
#endif

/**
 * @brief IOT 会话句柄（与 @ref qm_iot_init 返回值同址）。
 */
typedef struct {
    int exit;
    int status;
    qm_work_t post_work;
    qm_iot_reset_info_t reset_info;
    qm_bool started;
    qm_iot_network_state_t network_state;
    uint32_t did;
    uint32_t pid;
    qm_iot_cloud_info_t cloud_info;
    /** @brief MQTT 连接用户名，由 @ref qm_iot_mqtt_username_set 写入。 */
    char mqtt_user_name[CONFIG_QM_IOT_MQTT_USERNAME_MAX_LEN + 1];
    char str_version[CONFIG_QM_IOT_VER_STR_MAX_LEN + 1];
#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
    qm_iot_mcu_ver_info_t mcu_version;
    qm_iot_mcu_ver_slot_t mcu_ver_slots[CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES];
    qm_iot_mcu_ver_t mcu_ver_pub[CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES];
#endif
    char *prodtuct_sercet;
    char *server_crt;
    uint32_t server_crt_len;
    char *client_crt;
    uint32_t client_crt_len;
    char *client_key;
    uint32_t client_key_len;
    char *ota_public_key;
    uint32_t ota_public_key_len;
    char sn[CONFIG_QM_IOT_SN_MAX_LEN + 1];
#if CONFIG_QM_IOT_AUTHCODE_SUPPORT
    char authcode[CONFIG_QM_IOT_AUTHCODE_MAX_LEN];
#endif
#if CONFIG_QM_IOT_OTA_SUPPORT
    void *ota_handle;
#endif
#if CONFIG_QM_IOT_PUB_SUPPORT
    void *pub_handle;
#endif
#if CONFIG_QM_IOT_MQTT_SUPPORT
    qm_bool use_mqtt;
    void *mqtt_handle;
#endif
#if CONFIG_QM_IOT_DEVMNT_SUPPORT
    void *dev_handle;
    qm_bool has_devinfo;
    qm_iot_devinfo_t devinfo;
    /** @brief 当前 MQTT 连接周期内是否已发起 deviceBindOnline 上报。 */
    qm_bool device_bind_online_reported;
#endif
    qm_work_t request_work;
    qm_mutex_t lock;
    qm_task_t session_task;
    qm_iot_credential_cache_t crt_handle;
    void *user_data;
    qm_iot_recv_handler_t recv_handler;
    qm_iot_event_handler_t event_handler;
    /** @brief Wakes the session task when its MQTT state changes. */
    qm_sem_t task_wakeup;
    /** @brief Signalled exactly once when the session task has exited. */
    qm_sem_t task_exit;
    qm_bool task_started;
    qm_iot_event_listener_t event_listeners[CONFIG_QM_IOT_EVENT_LISTENER_NUM];
#if CONFIG_QM_IOT_SERVICE_SUPPORT
    qm_list_t *service;
#endif
} qm_iot_session_t;

#if defined(__cplusplus)
}
#endif

#endif /* __QM_IOT_SESSION_INTERNAL_H__ */
