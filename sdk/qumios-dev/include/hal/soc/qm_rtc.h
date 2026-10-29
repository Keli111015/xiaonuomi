#ifndef QM_RTC_H
#define QM_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_rtc RTC
 *  qm rtc API.
 *
 *  @{
 */

#include "qm_types.h"

/* Decode format list */
#define QM_RTC_FORMAT_DEC 1
#define QM_RTC_FORMAT_BCD 2

typedef struct {
    uint8_t format; /**< time formart DEC or BCD */
} qm_rtc_config_t;

typedef struct {
    uint8_t         port;   /**< rtc port */
    qm_rtc_config_t  config; /**< rtc config */
    void            *priv;   /**< priv data */
} qm_rtc_dev_t;

/*
 * RTC time
 */
typedef struct {
    uint8_t sec;     /**< DEC format:value range from 0 to 59, BCD format:value range from 0x00 to 0x59 */
    uint8_t min;     /**< DEC format:value range from 0 to 59, BCD format:value range from 0x00 to 0x59 */
    uint8_t hr;      /**< DEC format:value range from 0 to 23, BCD format:value range from 0x00 to 0x23 */
    uint8_t weekday; /**< DEC format:value range from 1 to  7, BCD format:value range from 0x01 to 0x07 */
    uint8_t date;    /**< DEC format:value range from 1 to 31, BCD format:value range from 0x01 to 0x31 */
    uint8_t month;   /**< DEC format:value range from 1 to 12, BCD format:value range from 0x01 to 0x12 */
    uint16_t year;   /**< DEC format:value range from 0 to 9999, BCD format:value range from 0x0000 to 0x9999 */
} qm_rtc_time_t;

/**
 * This function will initialize the on board CPU real time clock
 *
 *
 * @param[in]  rtc  rtc device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_init(qm_rtc_dev_t *rtc);

/**
 * This function will return the value of time read from the on board CPU real time clock.
 *
 * @param[in]   rtc   rtc device
 * @param[out]  time  pointer to a time structure
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time);

/**
 * This function will set MCU RTC time to a new value.
 *
 * @param[in]   rtc   rtc device
 * @param[in]   time  pointer to a time structure
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time);

/**
 * This function will return the value of time read from the on board CPU real time clock.
 *
 * @param[in]   rtc   rtc device
 * @param[out]  timestamp  pointer to a timestamp
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp);

/**
 * This function will set MCU RTC time to a new value.
 *
 * @param[in]   rtc   rtc device
 * @param[in]   timestamp  timestamp
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp);

/**
 * Program a one-shot platform RTC alarm.
 *
 * @param[in]  rtc        RTC device handle. This may be NULL on platforms
 *                        with a fixed RTC device.
 * @param[in]  timestamp  Absolute UTC Unix timestamp in seconds.
 *
 * @return QM_EOK on success, otherwise a negative error code.
 */
int32_t qm_rtc_alarm_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp);

/**
 * Disable the platform RTC alarm.
 *
 * @param[in]  rtc  RTC device handle. This may be NULL on platforms with a
 *                  fixed RTC device.
 *
 * @return QM_EOK on success, otherwise a negative error code.
 */
int32_t qm_rtc_alarm_cancel(qm_rtc_dev_t *rtc);

/**
 * De-initialises an RTC interface, Turns off an RTC hardware interface
 *
 * @param[in]  RTC  the interface which should be de-initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_RTC_H */
