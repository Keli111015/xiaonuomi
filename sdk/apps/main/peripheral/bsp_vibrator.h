#ifndef BSP_VIBRATOR_H
#define BSP_VIBRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/*
 * 振动马达开关，GPIO5 高电平开、低电平关。
 * 引脚只驱动三极管/MOS 的栅极，马达电源单独供，和板子共地。
 * ESP32 GPIO 带不动马达电流。
 */
#define BSP_VIBRATOR_GPIO       5
#define BSP_VIBRATOR_PULSE_MS   500

qm_err_t bsp_vibrator_init(void);
void bsp_vibrator_on(void);
void bsp_vibrator_off(void);
void bsp_vibrator_pulse(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
