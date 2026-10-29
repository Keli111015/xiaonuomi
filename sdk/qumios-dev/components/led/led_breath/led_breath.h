#ifndef _AUPU_BREATH_LED_H
#define _AUPU_BREATH_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#ifndef LED_BREATH_PWM_CHANNAL_MAX
#define LED_BREATH_PWM_CHANNAL_MAX     5
#endif

#ifndef LED_BREATH_MAX_ACTION_NUM
#define LED_BREATH_MAX_ACTION_NUM      5
#endif
/* timer interval, minimum value is 10ms */
#ifndef LED_BREATH_MONITOR_INTERVAL
#define LED_BREATH_MONITOR_INTERVAL   20
#endif

#ifndef LED_BREATH_PWM_LIGHT_FREQUENCY
#define LED_BREATH_PWM_LIGHT_FREQUENCY 1000
#endif

#ifndef LED_BREATH_PWM_MAX_PULSE
#define LED_BREATH_PWM_MAX_PULSE      (10000)
#endif

void led_breath_controller_init(void);

qm_err_t led_breath_deinit(int gpio_pin);
qm_err_t led_breath_init(int gpio_pin, int breath_time);
qm_err_t led_breath_start(int gpio_pin);
qm_err_t led_breath_stop(int gpio_pin);

qm_err_t led_breath_fast_start(int gpio_pin, uint16_t en_level);

#ifdef __cplusplus
}
#endif

#endif