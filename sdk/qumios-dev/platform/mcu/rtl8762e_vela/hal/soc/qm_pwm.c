#include "qm_pwm.h"
#include "qm.h"

#if CONFIG_QM_PWM_SUPPORT

#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_tim.h"

#define RTL876X_FREQ       (40000000)

typedef struct 
{
    uint8_t tim_func;
    TIM_TypeDef *tim_id;
}qm_pwm_info_t;

static qm_pwm_info_t pwm_info[] = 
{
    {TIM_PWM1, TIM1},
    {TIM_PWM2, TIM2},
    {TIM_PWM3, TIM3},
    {TIM_PWM4, TIM4},
    {TIM_PWM5, TIM5},
};

static void _gpio_output(uint8_t pin_num, int level)
{
    if (level)
    {
        Pad_Config(pin_num, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    }
    else
    {
        Pad_Config(pin_num, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    }
}

/*<! PWM output freqency = 40M/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
/*<! PWM duty cycle = TIM_PWM_High_Count/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    static int is_init = QM_TRUE;
    qm_pwm_info_t *info = NULL;
    TIM_TimeBaseInitTypeDef TIM_InitStruct;

    if(pwm->port < 0 || pwm->port > 4){
        return -QM_EINVAL;
    }
    
    if(is_init){
        is_init = QM_FALSE;
        RCC_PeriphClockCmd(APBPeriph_TIMER, APBPeriph_TIMER_CLOCK, ENABLE);
        *((volatile uint32_t *)0x40000360UL) &= ~(1 << 10);
    }

    info = &pwm_info[pwm->port];
    /* pad & pinmux */
    Pad_Config(pwm->pin.pwm_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    Pinmux_Config(pwm->pin.pwm_pin, info->tim_func);
    /* TIM */
    TIM_StructInit(&TIM_InitStruct);
    TIM_InitStruct.TIM_PWM_En = PWM_ENABLE;
    /*<! PWM output freqency = 40M/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
    /*<! PWM duty cycle = TIM_PWM_High_Count/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */

    TIM_InitStruct.TIM_PWM_High_Count = 0;
    TIM_InitStruct.TIM_PWM_Low_Count = (RTL876X_FREQ/pwm->config.freq);

    TIM_InitStruct.TIM_Mode = TIM_Mode_UserDefine;
    TIM_InitStruct.TIM_SOURCE_DIV = TIM_CLOCK_DIVIDER_1;
    TIM_TimeBaseInit(info->tim_id, &TIM_InitStruct);
    /* Enable PWM output */
    TIM_Cmd(info->tim_id, ENABLE);

    pwm->priv = (void *)info;

    return QM_EOK;
}

/*<! PWM output freqency = 40M/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
/*<! PWM duty cycle = TIM_PWM_High_Count/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    uint32_t high_count;
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    if (pwm->config.pulse == 0xffff)
    {
        high_count = (RTL876X_FREQ/pwm->config.freq);
        _gpio_output(pwm->pin.pwm_pin, 1);
    }
    else if (pwm->config.pulse == 0)
    {
        high_count = 0;
        _gpio_output(pwm->pin.pwm_pin, 0);
    }
    else
    {
        /* pad & pinmux */
        Pad_Config(pwm->pin.pwm_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
        Pinmux_Config(pwm->pin.pwm_pin, pwm_info[pwm->port].tim_func);
        high_count = ((RTL876X_FREQ/pwm->config.freq) / 65535.0) * pwm->config.pulse;
    }


    TIM_PWMChangeFreqAndDuty(info->tim_id, high_count, (RTL876X_FREQ/pwm->config.freq) - high_count);

    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    TIM_PWMChangeFreqAndDuty(info->tim_id, 0, (RTL876X_FREQ/pwm->config.freq));
    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    uint32_t high_count;
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwm->config.freq = para.freq;
    pwm->config.pulse = para.pulse;

    if (pwm->config.pulse == 0xffff)
    {
        high_count = (RTL876X_FREQ/pwm->config.freq);
        _gpio_output(pwm->pin.pwm_pin, 1);
    }
    else if (pwm->config.pulse == 0)
    {
        high_count = 0;
        _gpio_output(pwm->pin.pwm_pin, 0);
    }
    else
    {
        /* pad & pinmux */
        Pad_Config(pwm->pin.pwm_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
        Pinmux_Config(pwm->pin.pwm_pin, pwm_info[pwm->port].tim_func);
        high_count = ((RTL876X_FREQ/pwm->config.freq) / 65535.0) * pwm->config.pulse;
    }

    TIM_PWMChangeFreqAndDuty(info->tim_id, high_count, (RTL876X_FREQ/pwm->config.freq) - high_count);
    return QM_EOK;
}

#if 0
int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t time)
{
    int ret = QM_EOK;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    return ret;
}
#endif


int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

#endif
