
#ifndef __QM_RESET_REASON_H__
#define __QM_RESET_REASON_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/**
 * @brief Reset reasons
 */
typedef enum {
    QM_RST_UNKNOWN,    //!< Reset reason can not be determined
    QM_RST_POWERON,    //!< Reset due to power-on event
    QM_RST_EXT,        //!< Reset by external pin
    QM_RST_SW,         //!< Software reset via restart
    QM_RST_PANIC,      //!< Software reset due to exception/panic
    QM_RST_INT_WDT,    //!< Reset (software or hardware) due to interrupt watchdog
    QM_RST_TASK_WDT,   //!< Reset due to task watchdog
    QM_RST_WDT,        //!< Reset due to other watchdogs
    QM_RST_DEEPSLEEP,  //!< Reset after exiting deep sleep mode
    QM_RST_BROWNOUT,   //!< Brownout reset (software or hardware)
    QM_RST_SDIO,       //!< Reset over SDIO
    QM_RST_OTA_UPGRADE,//!< Reset from OTA Upgrade if support

    QM_RST_CUSTOM_0,//!< Reset from custom 0 if support
    QM_RST_CUSTOM_1,//!< Reset from custom 1 if support
    QM_RST_CUSTOM_2,//!< Reset from custom 2 if support
} qm_reset_reason_t;

/**
 * @brief  Get reason of last reset
 * @return See description of qm_reset_reason_t for explanation of each value.
 */
qm_reset_reason_t qm_reset_get_reason(void);

/**
 * @brief  Get reason of last reset
 * @return See description of qm_reset_reason_t for explanation of each value.
 */
char *qm_reset_get_string_reason(void);

/**
 * @brief  Set reason of last reset
 * 
 * @param[in]  reason
 * 
 * @return See description of qm_reset_reason_t for explanation of each value.
 */
qm_err_t qm_reset_set_reason(qm_reset_reason_t reason);


#ifdef __cplusplus
}
#endif

#endif