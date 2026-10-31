#ifndef BSP_SERVO_H
#define BSP_SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/*
 * Nominal mapping only. No shaft-position feedback. Bench range 75..105deg.
 * 信号 P6 / GPIO6，电源 4.8V，地共地。P4 留给摄像头 VSYNC/CS。
 */
#define BSP_SERVO_PWM_PIN           6
#define BSP_SERVO_PWM_HZ            50
#define BSP_SERVO_PWM_RES_BIT       14

#define BSP_SERVO_PULSE_MIN_US      500
#define BSP_SERVO_PULSE_MAX_US      2500

#define BSP_SERVO_PULSE_ANGLE_MIN   0
#define BSP_SERVO_PULSE_ANGLE_MAX   180

/* First-command center; no automatic movement at boot. */
#define BSP_SERVO_ZERO_PULSE_US     1500

#define BSP_SERVO_MOVE_STEP_DEG         5
#define BSP_SERVO_MOVE_STEP_MS          40

qm_err_t bsp_servo_init(void);
int bsp_servo_cli_init(void);
/* Nonblocking focus reminder; repeated requests while active return EBUSY. */
qm_err_t bsp_servo_focus_reminder(void);
void bsp_servo_deinit(void);

void bsp_servo_set_pulse_us(uint16_t pulse_us);
uint16_t bsp_servo_get_pulse_us(void);

void bsp_servo_set_angle(int16_t angle);
int16_t bsp_servo_get_angle(void);

#ifdef __cplusplus
}
#endif

#endif
