
#ifndef __IOT_OTA_H__
#define __IOT_OTA_H__

#include "qm_log.h"
#include "qm_kernel.h"    
#include "qm_config.h" 

#define OTA_LOG_ERROR(M, ...)       QM_LOGE("ota", M, ##__VA_ARGS__)
#define OTA_LOG_DEBUG(M, ...)       QM_LOGD("ota", M, ##__VA_ARGS__)
#define OTA_LOG_INFO(M, ...)        QM_LOGI("ota", M, ##__VA_ARGS__)

#ifndef CONFIG_OTA_BUF_LEN
#define CONFIG_OTA_BUF_LEN      4096
#endif

#ifndef CONFIG_OTA_TASK_SIZE
#define CONFIG_OTA_TASK_SIZE    4096
#endif

#ifndef CONFIG_OTA_TASK_PRIO
#define CONFIG_OTA_TASK_PRIO    20
#endif

#define OTA_MD5_MAX_LEN      32

/* State of OTA */
typedef enum {
    IOT_OTA_STATE_UNINITED = 0,  /* Uninitialized State */
    IOT_OTA_STATE_INITED,        /* Initialized State */
    IOT_OTA_STATE_FETCHING,      /* Fetching firmware */
    IOT_OTA_STATE_FETCHED        /* Fetching firmware finish */
} iot_ota_state_t;


/* Progress of OTA */
typedef enum {

    /* device busy failed */
    IOT_OTA_PROGRESS_BUSY_FAILED = -6,

     /* device low battery failed */
    IOT_OTA_PROGRESS_LOWBATTERY_FAILED = -5,

    /* Burn firmware file failed */
    IOT_OTA_PROGRESS_BURN_FAILED = -4,

    /* Check firmware file failed */
    IOT_OTA_PROGRESS_CHECK_FALIED = -3,

    /* Fetch firmware file failed */
    IOT_OTA_PROGRESS_FETCH_FAILED = -2,

    /* Initialized failed */
    IOT_OTA_PROGRESS_GENERAL_FAILED = -1,

    IOT_OTA_PROGRESS_SUCCESS = 0,

    /* [0, 100], percentage of fetch progress */

    /* The minimum percentage of fetch progress */
    IOT_OTA_PROGRESS_FETCH_PERCENTAGE_MIN = 0,

    /* The maximum percentage of fetch progress */
    IOT_OTA_PROGRESS_FETCH_PERCENTAGE_MAX = 100

} iot_ota_progress_t;


typedef enum {
    IOT_OTA_CMD_FETCHED_SIZE,     /* option for get already fetched size */
    IOT_OTA_CMD_FILE_SIZE,        /* size of file */
    IOT_OTA_CMD_MD5SUM,           /* md5 in string format */
    IOT_OTA_CMD_CHECK_FIRMWARE    /* Check firmware is valid or not */
} iot_ota_cmd_t;

typedef struct {
    uint32_t file_size;
    char *md5str;           /* MD5 string */
    char *url;              /* point to URL */
    uint16_t port;
}iot_ota_upgrade_info_t;

typedef enum {
    IOT_OTA_EVENT_NONE,
    IOT_OTA_EVENT_SUCCESS,
    IOT_OTA_EVENT_FAIL,
    IOT_OTA_EVENT_PROGRESS,
}iot_ota_event_t;

typedef void (*ota_event_fn)(iot_ota_event_t event, void *arg);

typedef struct {
    void *arg;
    ota_event_fn event;
}iot_ota_param_t;

/**
 * @brief Initialize OTA module, and return handle.
 *        The MQTT client must be construct before calling this interface.
 *
 * @param [in] param: specify the product id.
 *
 * @retval  0 : Successful.
 * @retval -1 : Failed.
 * @see None.
 */
void *iot_ota_init(iot_ota_param_t *param);

/**
 * @brief ota start
 *
 * @param [in] upgrade_info: specify upgrade info
 *
 * @retval  0 : Successful.
 * @retval -1 : Failed.
 * @see None.
 */
int iot_ota_start(void *handle, iot_ota_upgrade_info_t *upgrade_info);

/**
 * @brief Deinitialize OTA module specified by the 'handle', and release the related resource.
 *        You must call this interface to release resource if reboot is not invoked after downloading.
 *
 * @param [in] handle: specify the OTA module.
 *
 * @retval   0 : Successful.
 * @retval < 0 : Failed, the value is error code.
 * @see None.
 */
int iot_ota_deinit(void *handle);

#endif
