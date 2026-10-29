#ifndef QM_LPC_HAL_H
#define QM_LPC_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_lpc.h"

int32_t qm_lpc_check(void);
int32_t qm_lpc_resume(void);
int32_t qm_lpc_prepare(void);

int32_t qm_hal_lpc_init(void);
int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode);
int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable);
int32_t qm_hal_lpc_enter_powerdown(void);
qm_lpc_wakeup_reason_t qm_hal_lpc_wakeup_reason_get(void);
int32_t qm_hal_lpc_wakeup_reason_clear(void);

/**
 * @brief  使能低功耗能力,当无人投票时，开启低功耗能力.
 * 
 * @note    需底层适配
 *
 * @return  0 on success, negative error on failure.
 */
int32_t qm_hal_lpc_enable(void);

/**
 * @brief  使能低功耗能力,当有人投票时，关闭低功耗能力.
 * 
 * @note    需底层适配
 *
 * @return  0 on success, negative error on failure.
 */
int32_t qm_hal_lpc_disable(void);

#ifdef __cplusplus
}
#endif


#endif /* QM_LPC_HAL_H */
