#include "qm_pwm.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "system/app_core.h"
// #include "system/includes.h"
#include "system/user_cfg.h"
#include "app_config.h"
#include "user_pwm.h"

static int pwm_index = 0;
struct pwm_platform_data g_pwm_p_data[] =
{
    {.pwm_ch_num = pwm_ch0, .pwm_timer_num = pwm_timer0, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
    {.pwm_ch_num = pwm_ch1, .pwm_timer_num = pwm_timer1, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
    {.pwm_ch_num = pwm_ch2, .pwm_timer_num = pwm_timer2, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
    {.pwm_ch_num = pwm_ch3, .pwm_timer_num = pwm_timer3, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
    {.pwm_ch_num = pwm_ch4, .pwm_timer_num = pwm_timer4, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
    {.pwm_ch_num = pwm_ch5, .pwm_timer_num = pwm_timer5, .pwm_aligned_mode = pwm_edge_aligned, .h_pin_output_ch_num = 1, .l_pin_output_ch_num = 1,},
};

extern void mcpwm_init(struct pwm_platform_data * arg);
extern void mcpwm_close(pwm_ch_num_type pwm_ch, pwm_timer_num_type timer_ch);
extern void mcpwm_set_duty(pwm_ch_num_type pwm_ch, pwm_timer_num_type timer_ch, u16 duty);
extern void mcpwm_set_frequency(pwm_timer_num_type ch, pwm_aligned_mode_type align, u32 frequency);

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{   
    struct pwm_platform_data *p_data = NULL;

    if(pwm == NULL){
        return -QM_EINVAL;
    }

    p_data = &g_pwm_p_data[pwm->port];

    p_data->frequency = pwm->config.freq;                           //KHz
    p_data->duty = pwm->config.pulse * 100;                                    //占空比50%
    //hw
    p_data->l_pin = -1;//TCFG_BACKLIGHT_PWM_IO;               //硬件引脚，l_pin_output_ch_num无效，可不配置
    p_data->h_pin = pwm->pin.pwm_pin;//IO_PORTA_00;                    //没有则填 -1。h_pin_output_ch_num无效，可不配置
    //output_channel
    p_data->complementary_en = 1;
    pwm_index++;
    mcpwm_init(p_data);
    pwm->priv = (void *)p_data;

    return QM_EOK;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    struct pwm_platform_data *p_data = NULL;
    if(pwm == NULL){
        return -QM_EINVAL;
    }
    
    p_data = (struct pwm_platform_data *)pwm->priv;

    return QM_EOK;

    // return user_pwm_set_start(p_data->pwm_ch_num, 0, TRUE, pwm->config.pulse * 100, 0);
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    struct pwm_platform_data *p_data = NULL;
    if(pwm == NULL){
        return -QM_EINVAL;
    }
    
    p_data = (struct pwm_platform_data *)pwm->priv;

    mcpwm_close(p_data->pwm_ch_num, p_data->pwm_timer_num);

    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    struct pwm_platform_data *p_data = NULL;
    if(pwm == NULL){
        return -QM_EINVAL;
    }
    
    p_data = (struct pwm_platform_data *)pwm->priv;

    
    mcpwm_set_duty(p_data->pwm_ch_num, p_data->pwm_timer_num, para.pulse * 100);

    memcpy(&pwm->config, &para, sizeof(qm_pwm_config_t));
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
    struct pwm_platform_data *p_data = NULL;
    if(pwm == NULL){
        return -QM_EINVAL;
    }
    
    p_data = (struct pwm_platform_data *)pwm->priv;

    mcpwm_close(p_data->pwm_ch_num, p_data->pwm_timer_num);

    pwm->priv = NULL;
    return -QM_EINVAL;
}