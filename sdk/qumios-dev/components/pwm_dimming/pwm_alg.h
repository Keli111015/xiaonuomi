#ifndef _PWM_ALG_H
#define _PWM_ALG_H

#define DIMMING_CURVE_TYPE_LINEAR 0
#define DIMMING_CURVE_TYPE_EXP    1
#define DIMMING_CURVE_TYPE_LOG    2
#define DIMMING_CURVE_TYPE_S    3

#define EXP_FACTOR  (2.43)

#ifndef LIGHTNESS_MAX 
#  define LIGHTNESS_MAX 10000
#endif

void pwm_alg_table_init(int curve_type, int lightness_min);
int pwm_alg_get_exp(int light_id, int lightness);

#endif
