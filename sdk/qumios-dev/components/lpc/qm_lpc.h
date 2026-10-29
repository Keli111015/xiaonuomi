#ifndef QM_LPC_H
#define QM_LPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

typedef int32_t (*lpc_callback_t)(void);

/**
 * @brief Low-power mode requested by the application.
 */
typedef enum {
    QM_LPC_NO_SLEEP,    /**< Keep the system running. */
    QM_LPC_LIGHT_SLEEP, /**< Retain runtime context during sleep. */
    QM_LPC_DEEP_SLEEP,  /**< Enter platform deep sleep. */
    QM_LPC_POWERDOWN,   /**< Enter platform powerdown; boot restarts on wakeup. */
    QM_LPC_MAX,
} qm_lpc_mode_t;

/* Sleep module id Maximum (1<<31) */
typedef enum{
    QM_LPC_ID_RSV   = 1 << 15, /* Previously, the ID was reserved internally */

}qm_lpc_id_t;

/* LPC Wakeup Type */
typedef enum{
    QM_LPC_WAKEUP_TYPE_MIN,
    QM_LPC_WAKEUP_POSEDGE    = 1,     /*!< GPIO wakeup type : rising edge                  */
    QM_LPC_WAKEUP_NEGEDGE    = 2,     /*!< GPIO wakeup type : falling edge                 */
    QM_LPC_WAKEUP_ANYEDGE    = 3,     /*!< GPIO wakeup type : both rising and falling edge */
    QM_LPC_WAKEUP_LOW_LEVEL  = 4,     /*!< GPIO wakeup type : input low level trigger      */
    QM_LPC_WAKEUP_HIGH_LEVEL = 5,     /*!< GPIO wakeup type : input high level trigger     */ 
    QM_LPC_WAKEUP_TYPE_MAX,   
}qm_lpc_wakeup_type_t;

/**
 * @brief Platform wakeup reason reported after boot or resume.
 */
typedef enum {
    QM_LPC_WAKEUP_REASON_NONE = 0,   /**< No supported wakeup reason detected. */
    QM_LPC_WAKEUP_REASON_RTC_ALARM,  /**< Wakeup triggered by an RTC alarm. */
    QM_LPC_WAKEUP_REASON_GPIO,       /**< Wakeup triggered by a GPIO source. */
} qm_lpc_wakeup_reason_t;

/**
 * @brief  Low power initialization.
 *
 * @param  None
 *
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_init(void);

/**
 * @brief  Set current lowpower mode.
 *
 * @param  mode[in] mode #qm_lpc_mode.
 *
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_mode_set(qm_lpc_mode_t mode);

/**
 * @brief Evaluate registered low-power veto callbacks.
 *
 * @return QM_LPC_NO_SLEEP when sleep is blocked; otherwise the most
 *         restrictive mode returned by registered callbacks.
 */
int32_t qm_lpc_check(void);

/**
 * @brief Enter platform powerdown after rechecking the low-power policy.
 *
 * @note The successful platform transition does not return.
 *
 * @return A negative error code when powerdown cannot be entered.
 */
int32_t qm_lpc_enter_powerdown(void);

/**
 * @brief Read the platform wakeup reason.
 *
 * @return The detected platform wakeup reason.
 */
qm_lpc_wakeup_reason_t qm_lpc_wakeup_reason_get(void);

/**
 * @brief Clear the current platform wakeup reason when supported.
 *
 * @return QM_EOK on success, or -QM_ENOSYS when the reason is not clearable.
 */
int32_t qm_lpc_wakeup_reason_clear(void);

/**
 * @brief  Set current lowpower mode.
 *
 * @param  None.
 *
 * @return  #qm_lpc_mode current lowpower mode.
 */
qm_lpc_mode_t qm_lpc_mode_get(void);

/**
 * @brief  Register check callback fucntion.
 *
 * @param  callback [IN] type #lpc_callback_t
 *         If the return value is 0 means to disable sleep,others means enable.
 *         CNcomment:返回值为0禁止睡眠，其他值为允许.CNend
 *
 * @return #void, handle.CNcomment:句柄CNend
 */
void *qm_lpc_check_register(lpc_callback_t callback);

/**
 * @brief  Cancel registation of check callback fucntion.
 *
 * @param  callback [IN] type #lpc_callback_t
 * 
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_check_unregister(void *handle);

/**
 * @brief  Add low power sleep veto , do not enter low power consumption
 *
 * @param  id [IN] type #qm_lpc_id_t module id.
 * 
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_veto_add(qm_lpc_id_t id);

/**
 * @brief  Remove low power sleep veto, allow low power consumption
 *
 * @param  id [IN] type #qm_lpc_id_t module id.
 * 
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_veto_remove(qm_lpc_id_t id);

/**
 * @brief  Register hardware callback func of light_sleep or deep_sleep.
 *
 * @param  prepare [IN] type #lpc_callback_t Callback func of sleep
 *  
 * @param  resume [IN] type #lpc_callback_t Callback func of wake up
 * 
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_hw_register(lpc_callback_t prepare, lpc_callback_t resume);

/**
 * @brief  Config deep sleep gpio wakeup IO.
 *
 * @param  pin  [IN] Wake up source IO
 * 
 * @param  type [IN] IO Wake up type
 * 
 * @param  wakeup_cb  [IN] IO Wake up callback
 * 
 * @param  enable  [IN] type #bool_t whether enable the source IO.
 * 
 * @return  0 on success, negative error on failure.
 */
int32_t qm_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t wakeup_cb, bool_t enable);

#ifdef __cplusplus
}
#endif


#endif /* QM_LPC_H */
