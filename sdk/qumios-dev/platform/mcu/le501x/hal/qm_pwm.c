#include "qm.h"
#include "qm_pwm.h"
#include "ls_hal_timer.h"
#include "ls_soc_gpio.h"
#include "platform.h"

#define LOG_TAG "pwm"

/***目前暂适配相同频率的pwm接口,后续更新***/

#define TIM_PRESCALER     (SDK_HCLK_MHZ-1)

static int is_init = 1;
static uint8_t use_port_num = 0;

static TIM_HandleTypeDef g_TimHandle = {0};

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{   
    unsigned int channelx = 0;
    TIM_OC_InitTypeDef sConfig = {0};
    QM_RETURN_ON_FALSE(pwm, -QM_EINVAL, LOG_TAG, "pwm invail");

    switch (pwm->port)
    {
        case 0:
            channelx = TIM_CHANNEL_1;
            pinmux_gptima1_ch1_init(pwm->pin.pwm_pin, true, 0);  
        break;
        case 1:
            channelx = TIM_CHANNEL_2;
            pinmux_gptima1_ch2_init(pwm->pin.pwm_pin, true, 0);  
        break;
        case 2:
            channelx = TIM_CHANNEL_3;
            pinmux_gptima1_ch3_init(pwm->pin.pwm_pin, true, 0);  
        break;
        case 3:
            channelx = TIM_CHANNEL_4;
            pinmux_gptima1_ch4_init(pwm->pin.pwm_pin, true, 0);  
        break;    
        default:
        break;
    }
    
    if(is_init){
        is_init = 0;
        /*##-1- Configure the TIM peripheral #######################################*/
        g_TimHandle.Instance = LSGPTIMA;
        g_TimHandle.Init.Prescaler = TIM_PRESCALER; 
        g_TimHandle.Init.Period = (1000000 / pwm->config.freq) - 1;
        g_TimHandle.Init.ClockDivision = 0;
        g_TimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
        g_TimHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
        HAL_TIM_Init(&g_TimHandle);
    }


    /*##-2- Configure the PWM channels #########################################*/
    sConfig.OCMode = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;

    sConfig.Pulse = ((1000000 / pwm->config.freq) - 1) * pwm->config.pulse / 100;
    HAL_TIM_PWM_ConfigChannel(&g_TimHandle, &sConfig, channelx);

    use_port_num++;
    pwm->priv = (void *)&g_TimHandle;
	return 0;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    unsigned int channelx = 0;
    TIM_HandleTypeDef *TimHandle = (TIM_HandleTypeDef *)pwm->priv;
    QM_RETURN_ON_FALSE(pwm, -QM_EINVAL, LOG_TAG, "pwm invail");
    QM_RETURN_ON_FALSE(pwm->priv, -QM_EINVAL, LOG_TAG, "pwm priv invail");

    switch (pwm->port)
    {
        case 0:
            channelx = TIM_CHANNEL_1; 
        break;
        case 1:
            channelx = TIM_CHANNEL_2;
        break;
        case 2:
            channelx = TIM_CHANNEL_3;
        break;
        case 3:
            channelx = TIM_CHANNEL_4;
        break;    
        default:
        break;
    }
    HAL_TIM_PWM_Start(TimHandle, channelx);

    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    unsigned int channelx = 0;
    TIM_HandleTypeDef *TimHandle = (TIM_HandleTypeDef *)pwm->priv;
    QM_RETURN_ON_FALSE(pwm, -QM_EINVAL, LOG_TAG, "pwm invail");
    QM_RETURN_ON_FALSE(pwm->priv, -QM_EINVAL, LOG_TAG, "pwm priv invail");

    switch (pwm->port)
    {
        case 0:
            channelx = TIM_CHANNEL_1; 
        break;
        case 1:
            channelx = TIM_CHANNEL_2;
        break;
        case 2:
            channelx = TIM_CHANNEL_3;
        break;
        case 3:
            channelx = TIM_CHANNEL_4;
        break;    
        default:
        break;
    }
    HAL_TIM_PWM_Stop(TimHandle, channelx);

    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    unsigned int channelx = 0;
    TIM_OC_InitTypeDef sConfig = {0};
    TIM_HandleTypeDef *TimHandle = (TIM_HandleTypeDef *)pwm->priv;
    QM_RETURN_ON_FALSE(pwm, -QM_EINVAL, LOG_TAG, "pwm invail");
    QM_RETURN_ON_FALSE(pwm->priv, -QM_EINVAL, LOG_TAG, "pwm priv invail");

    switch (pwm->port)
    {
        case 0:
            channelx = TIM_CHANNEL_1; 
        break;
        case 1:
            channelx = TIM_CHANNEL_2;
        break;
        case 2:
            channelx = TIM_CHANNEL_3;
        break;
        case 3:
            channelx = TIM_CHANNEL_4;
        break;    
        default:
        break;
    }
    HAL_TIM_PWM_Stop(TimHandle, channelx);

    /*##-2- Configure the PWM channels #########################################*/
    sConfig.OCMode = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;

    sConfig.Pulse = ((1000000 / para.freq) - 1) * para.pulse / 100;
    HAL_TIM_PWM_ConfigChannel(TimHandle, &sConfig, channelx);
    HAL_TIM_PWM_Start(TimHandle, channelx);
    return  QM_EOK;
}

#if 0 //暂不支持
int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t time)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    return -QM_EINVAL;
}
#endif

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    unsigned int channelx = 0;
    TIM_HandleTypeDef *TimHandle = (TIM_HandleTypeDef *)pwm->priv;
    QM_RETURN_ON_FALSE(pwm, -QM_EINVAL, LOG_TAG, "pwm invail");
    QM_RETURN_ON_FALSE(pwm->priv, -QM_EINVAL, LOG_TAG, "pwm priv invail");

    switch (pwm->port)
    {
        case 0:
            channelx = TIM_CHANNEL_1; 
        break;
        case 1:
            channelx = TIM_CHANNEL_2;
        break;
        case 2:
            channelx = TIM_CHANNEL_3;
        break;
        case 3:
            channelx = TIM_CHANNEL_4;
        break;    
        default:
        break;
    }
    HAL_TIM_PWM_Stop(TimHandle, channelx);

    pwm->priv = NULL;

    if(--use_port_num <= 0){
        is_init = 1;
        HAL_TIM_DeInit(&g_TimHandle);
    }
    return QM_EOK;
}