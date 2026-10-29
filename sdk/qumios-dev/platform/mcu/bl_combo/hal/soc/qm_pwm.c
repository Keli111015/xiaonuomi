#include "qm.h"
#include "qm_pwm.h"

#include <stdio.h>
#include <FreeRTOS.h>
#include <stream_buffer.h>
#include <task.h>
#include <string.h>
#include <device/vfs_uart.h>
#include <hal/soc/soc.h>
#include <vfs_err.h>
#include <yloop_types.h>
#include <utils_log.h>
#include <blog.h>
#include <libfdt.h>
#include <vfs_register.h>

#include "qm_utils_timer.h"

#include <hosal_gpio.h>
#include <hosal_pwm.h>




#include "qm_pwm.h"


typedef enum 
{
	QM_PWM_CHANNEL_TYPE_0 = 0,
	QM_PWM_CHANNEL_TYPE_1 = 1,
	QM_PWM_CHANNEL_TYPE_2 = 2,
	QM_PWM_CHANNEL_TYPE_3 = 3,
	QM_PWM_CHANNEL_TYPE_4 = 4,
}qm_pwm_channel_type_t;

typedef struct 
{
	uint8_t pwm_ch;
	uint8_t pwm_pin;
    uint32_t freq;
    uint8_t pulse;
}qm_pwm_info_t;

static const qm_pwm_info_t g_pwm_info[] = 
{
	{QM_PWM_CHANNEL_TYPE_0, 0},
	{QM_PWM_CHANNEL_TYPE_0, 5},
	{QM_PWM_CHANNEL_TYPE_0, 20},
	{QM_PWM_CHANNEL_TYPE_1, 1},
	{QM_PWM_CHANNEL_TYPE_1, 11},
	{QM_PWM_CHANNEL_TYPE_1, 21},
	{QM_PWM_CHANNEL_TYPE_2, 2},
	{QM_PWM_CHANNEL_TYPE_2, 12},
	{QM_PWM_CHANNEL_TYPE_2, 17},
	{QM_PWM_CHANNEL_TYPE_2, 22},
	{QM_PWM_CHANNEL_TYPE_3, 3},
	{QM_PWM_CHANNEL_TYPE_3, 8},
	{QM_PWM_CHANNEL_TYPE_4, 4},
	{QM_PWM_CHANNEL_TYPE_4, 14},
};

static const qm_pwm_info_t *bl602_pwm_info_get(uint8_t pwm_pin)
{
	const qm_pwm_info_t *pwm_info = NULL;
	for(int index = 0; index < QM_ARRAY_SIZE(g_pwm_info); index++){
		if((pwm_pin) == g_pwm_info[index].pwm_pin){
			pwm_info = &g_pwm_info[index];
			break;
		}
	}
	return pwm_info;
}

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    uint16_t pin = 0;
    qm_pwm_info_t *pwm_info = NULL;
    hosal_pwm_dev_t hosal_pwm = {0};
    if(pwm == NULL){
          return -QM_EINVAL;
      }
    
    hosal_pwm.port = pwm->port;
    hosal_pwm.config.pin = pwm->pin.pwm_pin;
    hosal_pwm.config.freq = pwm->config.freq;
    hosal_pwm.config.duty_cycle = pwm->config.pulse;

    pwm_info = bl602_pwm_info_get(pwm->pin.pwm_pin);
    if(pwm_info == NULL){
      return -QM_EINVAL;
    }

    pwm_info->freq  = pwm->config.freq;
    pwm_info->pulse = pwm->config.pulse;

    hosal_pwm_init(&hosal_pwm);

    pwm->priv = (void *)pwm_info;
    return QM_EOK;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    const qm_pwm_info_t *pwm_info = NULL;
    hosal_pwm_dev_t hosal_pwm = {0};

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

	pwm_info = (const qm_pwm_info_t *)pwm->priv;
    hosal_pwm.port = pwm_info->pwm_ch;
    hosal_pwm.config.pin = pwm_info->pwm_pin;
    hosal_pwm.config.freq = pwm_info->freq;
    hosal_pwm.config.duty_cycle = pwm_info->pulse;
     /* start pwm */
     hosal_pwm_start(&hosal_pwm);
    return QM_EOK;
}



int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    const qm_pwm_info_t *pwm_info = NULL;
    hosal_pwm_dev_t hosal_pwm = {0};

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwm_info = (const qm_pwm_info_t *)pwm->priv;

    hosal_pwm.port = pwm_info->pwm_ch;
    hosal_pwm.config.pin = pwm_info->pwm_pin;
    hosal_pwm.config.freq = pwm_info->freq;
    hosal_pwm.config.duty_cycle = pwm_info->pulse;

    // pwm->config.freq = para.freq;
    // pwm->config.pulse = para.pulse;
    // bl602_pwm_duty(pwm_info->pwm_ch, para.pulse);

    hosal_pwm_config_t hosal_para;
    hosal_para.duty_cycle = para.pulse; //8000/10000=80%
    hosal_para.freq = para.freq;

    hosal_pwm_para_chg(&hosal_pwm, hosal_para);
    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
	const qm_pwm_info_t *pwm_info = NULL;
    hosal_pwm_dev_t hosal_pwm = {0};
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwm_info = (const qm_pwm_info_t *)pwm->priv;


    hosal_pwm.port = pwm_info->pwm_ch;
    hosal_pwm.config.pin = pwm_info->pwm_pin;
    hosal_pwm.config.freq = pwm_info->freq;
    hosal_pwm.config.duty_cycle = pwm_info->pulse;
  

    /* stop pwm */
    hosal_pwm_stop(&hosal_pwm);
    hosal_pwm_finalize(&hosal_pwm);
    return QM_EOK;
}

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
	const qm_pwm_info_t *pwm_info = NULL;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

	//   pwm_info = (const qm_pwm_info_t *)pwm->priv;

    // pwm_channel_disable(pwm_info->pwm_ch);
    // bl602_gpio_deinit((pwm->pin.pwm_pin << GPIO_PIN_SHIFT));
    
	//   pwm->priv = NULL;

	  return QM_EOK;
}

