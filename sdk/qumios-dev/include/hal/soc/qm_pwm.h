#ifndef QM_PWM_H
#define QM_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_pwm PWM
 *  qm pwm API.
 *
 *  @{
 */


#include "qm_types.h"

/**
 * @brief pwm pin
 */
typedef struct {
    uint8_t pwm_pin;
} qm_pwm_pin_t;

typedef struct {
    uint32_t pulse;     /**< the pwm pulse */
    uint32_t freq;       /**< the pwm freq */
} qm_pwm_config_t;

typedef struct {
    uint8_t       port;   /**< pwm port */
    qm_pwm_config_t  config; /**< spi config */
    qm_pwm_pin_t    pin;   /**< pwm pin */
    void         *priv;   /**< priv data */
} qm_pwm_dev_t;

/**
 * Initialises a PWM pin
 *
 * @param[in]  pwm  the PWM device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_init(qm_pwm_dev_t *pwm);

/**
 * Starts Pulse-Width Modulation signal output on a PWM pin
 *
 * @param[in]  pwm  the PWM device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_start(qm_pwm_dev_t *pwm);

/**
 * Stops output on a PWM pin
 *
 * @param[in]  pwm  the PWM device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_stop(qm_pwm_dev_t *pwm);

/**
 * change the para of pwm
 *
 * @param[in]  pwm   the PWM device
 * @param[in]  para  the para of pwm
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para);

/**
 * Initiates the gradient pulse width modulation signal output on the PWM pin
 *
 * @param[in]  pwm          the PWM device
 * @param[in]  start_pulse  pwm starting duty cycle
 * @param[in]  end_pulse    pwm stoping  duty cycle
 * @param[in]  timeout      the time from starting pulse to ending pulse, unit ms
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t timeout);

/**
 * De-initialises an PWM interface, Turns off an PWM hardware interface
 *
 * @param[in]  pwm  the interface which should be de-initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_PWM_H */

