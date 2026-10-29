
#ifndef __IOT_OTA_H__
#define __IOT_OTA_H__

#include "qm_iot_import.h"
#include "iot_cloud_debug.h"
#include "cloud_common.h"

#if CONFIG_OTA_SUPPORT

#define OTA_LOG_ERROR       iot_log_err
#define OTA_MALLOC          hal_malloc
#define OTA_FREE            hal_free
#define OTA_LOG_DEBUG       iot_log_debug
#define OTA_LOG_INFO        iot_log_info

#define OTA_BUF_LEN        (4096+1)


typedef enum{
    IOT_OTA_TYPE_NONE = 0,
    IOT_OTA_TYPE_DEVICE = 1,
    IOT_OTA_TYPE_MCU = 2,
}iot_ota_type_t;


typedef enum {

    IOT_OTAE_GENERAL = -1,
    IOT_OTAE_INVALID_PARAM = -2,
    IOT_OTAE_INVALID_STATE = -3,
    IOT_OTAE_STR_TOO_LONG = -4,
    IOT_OTAE_FETCH_FAILED = -5,
    IOT_OTAE_NOMEM = -6,
    IOT_OTAE_OSC_FAILED = -7,
    IOT_OTAE_NONE = 0,

} iot_ota_err_t;


/* State of OTA */
typedef enum {
    IOT_OTAS_UNINITED = 0,  /* Uninitialized State */
    IOT_OTAS_INITED,        /* Initialized State */
    IOT_OTAS_FETCHING,      /* Fetching firmware */
    IOT_OTAS_FETCHED        /* Fetching firmware finish */
} iot_ota_state_t;


/* Progress of OTA */
typedef enum {

    /* device busy failed */
    IOT_OTAP_BUSY_FAILED = -6,

     /* device low battery failed */
    IOT_OTAP_LOWBATTERY_FAILED = -5,

    /* Burn firmware file failed */
    IOT_OTAP_BURN_FAILED = -4,

    /* Check firmware file failed */
    IOT_OTAP_CHECK_FALIED = -3,

    /* Fetch firmware file failed */
    IOT_OTAP_FETCH_FAILED = -2,

    /* Initialized failed */
    IOT_OTAP_GENERAL_FAILED = -1,

    IOT_OTAP_SUCCESS = 0,

    /* [0, 100], percentage of fetch progress */

    /* The minimum percentage of fetch progress */
    IOT_OTAP_FETCH_PERCENTAGE_MIN = 0,

    /* The maximum percentage of fetch progress */
    IOT_OTAP_FETCH_PERCENTAGE_MAX = 100

} iot_ota_progress_t;


typedef enum {

    IOT_OTAG_FETCHED_SIZE,     /* option for get already fetched size */
    IOT_OTAG_FILE_SIZE,        /* size of file */
    IOT_OTAG_MD5SUM,           /* md5 in string format */
    IOT_OTAG_VERSION,          /* version in string format */
    IOT_OTAG_CHECK_FIRMWARE    /* Check firmware is valid or not */

} iot_ota_cmdtype_t;


typedef struct{
    uint8_t md5[16];
    uint32_t file_size;
}iot_ota_notify_t;


typedef struct  {
    
    iot_ota_type_t ota_type;

    uint32_t product_id;    /* point to product key */
    const char *device_id;    /* point to device name */

    uint32_t id;                /* message id */
    iot_ota_state_t state;      /* OTA state */
    uint32_t size_last_fetched; /* size of last downloaded */
    uint32_t size_fetched;      /* size of already downloaded */
    uint32_t size_file;         /* size of file */
    char *purl;                 /* point to URL */
    char *version;              /* point to string */
    char md5sum[33];            /* MD5 string */

    void *md5;                  /* MD5 handle */
    void *ch_signal;            /* channel handle of signal exchanged with OTA server */
    void *ch_fetch;             /* channel handle of download */
    int code;                   /* upgrade of code */

    char *ota_buf;

    int err;                    /* last error code */

} ota_struct_t, *ota_struct_pt;

/** @defgroup group_api api
 *  @{
 */

/** @defgroup group_api_ota ota
 *  @{
 */

/**
 * @brief Initialize OTA module, and return handle.
 *        The MQTT client must be construct before calling this interface.
 *
 * @param [in] product_id: specify the product id.
 * @param [in] device_name: specify the device name.
 * @param [in] code: specify ota code.
 * @param [in] ch_signal: specify the signal channel.
 *
 * @retval  0 : Successful.
 * @retval -1 : Failed.
 * @see None.
 */
void *iot_ota_init(unsigned int product_id, const char *device_id, void *ch_signal);


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


/**
 * @brief Report firmware version information to OTA server (optional).
 *        NOTE: please
 *
 * @param [in] handle: specify the OTA module.
 * @param [in] version: specify the firmware version in string format.
 * @param [in] ota_type: specify ota type
 * @retval   0 : Successful.
 * @retval < 0 : Failed, the value is error code.
 * @see None.
 */
int iot_ota_generic_reportversion(void *handle, const char *version, iot_ota_type_t ota_type);


/**
 * @brief Report firmware version information to OTA server (optional).
 *        NOTE: please
 *
 * @param [in] version: specify the firmware version in string format.
 * @param [in] ota_type: specify ota type
 * @retval   0 : Successful.
 * @retval < 0 : Failed, the value is error code.
 * @see None.
 */
int iot_ota_reportversion(const char *version, iot_ota_type_t ota_type);



/**
 * @brief Report detail progress to OTA server (optional).
 *        NOTE: please
 *
 * @param [in] handle: specify the OTA module.
 * @param [in] progress: specify the progress defined by 'iot_ota_progress_t'.
 * @param [in] msg: detail progress information in string.
 * @param [in] ota_type: specify ota type
 * @retval   0 : Successful.
 * @retval < 0 : Failed, the value is error code.
 * @see None.
 */
int iot_ota_reportprogress(void *handle, iot_ota_progress_t progress, const char *msg, iot_ota_type_t ota_type);


/**
 * @brief Check whether is on fetching state
 *
 * @param [in] handle: specify the OTA module.
 *
 * @retval 1 : Yes.
 * @retval 0 : No.
 * @see None.
 */
int iot_ota_isfetching(void *handle);


/**
 * @brief Check whether is on end-of-fetch state.
 *
 * @param [in] handle: specify the OTA module.
 *
 * @retval 1 : Yes.
 * @retval 0 : False.
 * @see None.
 */
int iot_ota_isfetch_finsh(void *handle);


/**
 * @brief fetch firmware from remote server with specific timeout value.
 *        NOTE: If you want to download more faster, the bigger 'buf' should be given.
 *
 * @param [in] handle: specify the OTA module.
 * @param [out] buf: specify the space for storing firmware data.
 * @param [in] buf_len: specify the length of 'buf' in bytes.
 * @param [in] timeout_s: specify the timeout value in second.
 *
 * @retval      < 0 : Error occur..
 * @retval        0 : No any data be downloaded in 'timeout_s' timeout period.
 * @retval (0, len] : The length of data be downloaded in 'timeout_s' timeout period in bytes.
 * @see None.
 */
int iot_ota_fetchyield(void *handle, char *buf, uint32_t buf_len, uint32_t timeout_s);


/**
 * @brief Get OTA information specified by 'type'.
 *        By this interface, you can get information like state, size of file, md5 of file, etc.
 *
 * @param [in] handle: handle of the specific OTA
 * @param [in] type: specify what information you want, see detail 'iot_ota_cmdtype_t'
 * @param [out] buf: specify buffer for data exchange
 * @param [in] buf_len: specify the length of 'buf' in byte.
 * @return
   @verbatim
      NOTE:
      1) When type is IOT_OTAG_FETCHED_SIZE, 'buf' should be pointer of uint32_t, and 'buf_len' should be 4.
      2) When type is IOT_OTAG_FILE_SIZE, 'buf' should be pointer of uint32_t, and 'buf_len' should be 4.
      3) When type is IOT_OTAG_MD5SUM, 'buf' should be a buffer, and 'buf_len' should be 33.
      4) When type is IOT_OTAG_VERSION, 'buf' should be a buffer, and 'buf_len' should be OTA_VERSION_LEN_MAX.
      5) When type is IOT_OTAG_CHECK_FIRMWARE, 'buf' should be pointer of uint32_t, and 'buf_len' should be 4.
         0, firmware is invalid; 1, firmware is valid.
  @endverbatim
 *
 * @retval   0 : Successful.
 * @retval < 0 : Failed, the value is error code.
 * @see None.
 */
int iot_ota_ioctl(void *handle, iot_ota_cmdtype_t type, void *buf, size_t buf_len);


/**
 * @brief Get last error code.
 *
 * @param [in] handle: specify the OTA module.
 *
 * @return The error code.
 * @see None.
 */
int iot_ota_get_lasterror(void *handle);


int ota_loop(void** handle, void *pcontext);

/** @} */ /* end of api_ota */
/** @} */ /* end of api */

#endif

#endif /* __OTA_EXPORT_H__ */
