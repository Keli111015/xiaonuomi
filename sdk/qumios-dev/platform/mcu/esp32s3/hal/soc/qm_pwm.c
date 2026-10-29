#include "qm_pwm.h"
#include "qm.h"
#include "driver/ledc.h"

#define LOG_TAG "pwm"

uint8_t ledc_fade_flag = 1;


typedef struct 
{
    ledc_timer_config_t ledc_timer;
    ledc_channel_config_t ledc_channel;
}qm_pwm_info_t;

qm_pwm_info_t pwm_info[4] = {0};
/* 
static IRAM_ATTR bool cb_ledc_fade_end_event(const ledc_cb_param_t *param, void *user_arg)
{
    BaseType_t taskAwoken = pdFALSE;

    if (param->event == LEDC_FADE_END_EVT) {
        SemaphoreHandle_t counting_sem = (SemaphoreHandle_t) user_arg;
        xSemaphoreGiveFromISR(counting_sem, &taskAwoken);
    }

    return (taskAwoken == pdTRUE);
}
 */
int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    qm_err_t ret = QM_EOK;
    qm_pwm_info_t *info = NULL;

    if(pwm->port < 0 || pwm->port > 3){
        return -QM_EINVAL;
    }
    
    info = &pwm_info[pwm->port];

    info->ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    info->ledc_channel.gpio_num = pwm->pin.pwm_pin;
    info->ledc_channel.flags.output_invert = 0;

    info->ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    info->ledc_timer.duty_resolution = LEDC_TIMER_14_BIT;
    info->ledc_timer.freq_hz = pwm->config.freq;
    info->ledc_timer.clk_cfg = LEDC_AUTO_CLK;

    switch(pwm->port)
    {
        case 0:
            info->ledc_timer.timer_num = LEDC_TIMER_0;

            info->ledc_channel.channel = LEDC_CHANNEL_0;
            info->ledc_channel.timer_sel = LEDC_TIMER_0;
            break;
        case 1:
            info->ledc_timer.timer_num = LEDC_TIMER_1;

            info->ledc_channel.channel = LEDC_CHANNEL_1;
            info->ledc_channel.timer_sel = LEDC_TIMER_1;
            break;
        case 2:
            info->ledc_timer.timer_num = LEDC_TIMER_2;

            info->ledc_channel.channel = LEDC_CHANNEL_2;
            info->ledc_channel.timer_sel = LEDC_TIMER_2;
            break;
        case 3:
            info->ledc_timer.timer_num = LEDC_TIMER_3;

            info->ledc_channel.channel = LEDC_CHANNEL_3;
            info->ledc_channel.timer_sel = LEDC_TIMER_3;
            break;
        default:
            ret = -QM_ERROR;
        break;
    }

    ledc_timer_config(&info->ledc_timer);
    ledc_channel_config(&info->ledc_channel);
    
    if(ledc_fade_flag == 1)
    {
        QM_LOGD(LOG_TAG, "ledc_fade_func_install\n");
        ledc_fade_func_install(0);
        ledc_fade_flag = 0;
    }
 
    pwm->priv = (void *)&pwm_info[pwm->port];

    ledc_set_duty(info->ledc_channel.speed_mode, info->ledc_channel.channel, pwm->config.pulse);

    return QM_EOK;
}

/*<! PWM output freqency = 40M/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
/*<! PWM duty cycle = TIM_PWM_High_Count/(TIM_PWM_High_Count + TIM_PWM_Low_Count) */
int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    ledc_channel_config_t *ledc_channel = NULL;
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    ledc_channel = &info->ledc_channel;

    ledc_update_duty(ledc_channel->speed_mode, ledc_channel->channel);

    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    ledc_channel_config_t *ledc_channel = NULL;
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    ledc_channel = &info->ledc_channel;

    ledc_stop(ledc_channel->speed_mode, ledc_channel->channel, 0);

    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    ledc_channel_config_t *ledc_channel = NULL;
    qm_pwm_info_t *info = (qm_pwm_info_t *)pwm->priv;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    ledc_channel = &info->ledc_channel;
#if 0
    if(para.pulse == 0){
        ret = qm_pwm_stop(pwm);
        return ret;
    }
#endif

    ledc_set_duty(ledc_channel->speed_mode, ledc_channel->channel, para.pulse);
    ledc_update_duty(ledc_channel->speed_mode, ledc_channel->channel);

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
    // ledc_fade_func_uninstall();
    // uint8_t ledc_fade_flag = 1;
    return QM_EOK;
}

