#include "qm.h"
#include "qm_pwm.h"

#include "led_breath.h"

#define LOG_TAG "breath"



typedef struct{
    qm_timer_t  breath_led_timer;
    qm_mutex_t breath_lock;
}breath_led_info_t;


typedef struct{
    uint8_t         is_use;
    uint8_t         is_active;
    uint32_t        breath_time;
    qm_pwm_dev_t    pwm;

    uint16_t lightness_begin;
    uint16_t lightness_end;
    uint16_t current_lightness;
    bool in_forward_phase;
    uint8_t current_forward_steps;
    uint8_t forward_steps;
    int32_t forward_step_delta;
    uint8_t current_reverse_steps;
    uint8_t reverse_steps;
    int32_t reverse_step_delta;
    bool half_breath_end;

}breath_led_handle_t;

static breath_led_info_t  g_breath_led_info = {0};
static breath_led_handle_t g_breath_led_handle[LED_BREATH_MAX_ACTION_NUM] = {0};

static breath_led_handle_t *request_led_handle(void)
{
    /* find same first */
    for (uint8_t i = 0; i < LED_BREATH_MAX_ACTION_NUM; ++i)
    {
        if(!(g_breath_led_handle[i].is_use)){
            return &g_breath_led_handle[i];
        }
    }

    return NULL;
}

static breath_led_handle_t *find_led_handle(int gpio_pin)
{
    /* find same first */
    for (uint8_t i = 0; i < LED_BREATH_MAX_ACTION_NUM; ++i)
    {
        if(!(g_breath_led_handle[i].is_use)){
            continue;
        }

        if(g_breath_led_handle[i].pwm.pin.pwm_pin == gpio_pin){
            return &g_breath_led_handle[i];
        }
    }

    return NULL;
}
static void light_lighten(qm_pwm_dev_t *light, uint16_t lightness)
{
    qm_pwm_config_t para = {
        .pulse = lightness,
        .freq = LED_BREATH_PWM_LIGHT_FREQUENCY,
    };
    qm_pwm_para_change(light, para);
}

static uint8_t is_all_light_idle(void)
{
    for (uint8_t channel = 0; channel < LED_BREATH_MAX_ACTION_NUM; channel++)
    {
        if (g_breath_led_handle[channel].is_use && 
            g_breath_led_handle[channel].is_active){
            return QM_FALSE;
        }
    }
    return QM_TRUE;
}


static void light_ctl_timeout_handle(qm_timer_t *timer, void *arg)
{
    uint8_t is_idle = QM_FALSE;

    qm_mutex_lock(&g_breath_led_info.breath_lock, QM_WAIT_FOREVER);

    for (uint8_t channel = 0; channel < LED_BREATH_MAX_ACTION_NUM; channel++)
    {
        if(!g_breath_led_handle[channel].is_active){
            continue;
        }
        
        if (g_breath_led_handle[channel].in_forward_phase){

            if (g_breath_led_handle[channel].current_forward_steps >= g_breath_led_handle[channel].forward_steps){

                light_lighten(&(g_breath_led_handle[channel].pwm),
                                g_breath_led_handle[channel].lightness_end);
                g_breath_led_handle[channel].current_lightness =  g_breath_led_handle[channel].lightness_end;
                g_breath_led_handle[channel].current_reverse_steps = 0;
                g_breath_led_handle[channel].in_forward_phase = QM_FALSE;

            }else{
                g_breath_led_handle[channel].current_forward_steps ++;
                light_lighten(&g_breath_led_handle[channel].pwm, g_breath_led_handle[channel].current_lightness);
                g_breath_led_handle[channel].current_lightness += g_breath_led_handle[channel].forward_step_delta;
            }
            
        }else{

            if (g_breath_led_handle[channel].current_reverse_steps >= g_breath_led_handle[channel].reverse_steps){

                light_lighten(&(g_breath_led_handle[channel].pwm), g_breath_led_handle[channel].lightness_begin);
                g_breath_led_handle[channel].current_lightness = g_breath_led_handle[channel].lightness_begin;
                g_breath_led_handle[channel].current_forward_steps = 0;
                g_breath_led_handle[channel].in_forward_phase = QM_TRUE;

            }else{
                light_lighten(&(g_breath_led_handle[channel].pwm), g_breath_led_handle[channel].current_lightness);
                g_breath_led_handle[channel].current_lightness += g_breath_led_handle[channel].reverse_step_delta;
                g_breath_led_handle[channel].current_reverse_steps++;
            }
        }
    }

    is_idle = is_all_light_idle();
    qm_mutex_unlock(&g_breath_led_info.breath_lock);

    /* Check light controler status */
    if(is_idle){
        qm_timer_stop(&g_breath_led_info.breath_led_timer);
    }
}

/**
 * @brief breath light
 * @param[in] light: light channel
 * @param[in] lightness_begin: breath start lightness
 * @param[in] lightness_end: breath end lightness
 * @param[in] interval: breath interval, the unit is ms
 * @param[in] forward_duty: start lightness duty in total interval, value range is 0-100
 *            for example, intreval is 1000ms, duty is 60, then start lightness will lighten
 *            600ms, end lightness will lighten 400ms
 * @param[in] times: breath times
 * @param[in] half_breath_end: whether the last breath is half or not, if TURE, lightness will stay
 *            on lightness_end, otherwise lightness till stay on lightness_begin
 * @param[in] cb: light change done callback function
 */
static void light_breath(breath_led_handle_t *breath_led, uint16_t lightness_begin, uint16_t lightness_end,
                  uint32_t interval, uint8_t forward_duty, bool half_breath_end)
{
    uint32_t total_steps = 0;
    uint8_t is_idle = QM_FALSE;

    if(breath_led == NULL){
        return ;
    }

    if(forward_duty > 100){
        forward_duty = 100;
    }

    total_steps = interval / LED_BREATH_MONITOR_INTERVAL;
    if (0 == total_steps){
        total_steps = 1;
    }

    qm_mutex_lock(&g_breath_led_info.breath_lock, QM_WAIT_FOREVER);

    is_idle = is_all_light_idle();

    breath_led->is_active = QM_TRUE;
    breath_led->in_forward_phase = QM_TRUE;
    breath_led->lightness_end = lightness_end;
    breath_led->lightness_begin = lightness_begin;
    breath_led->current_lightness = lightness_begin;
    breath_led->current_forward_steps = 0;
    breath_led->forward_steps = total_steps * forward_duty / 100;
    breath_led->forward_step_delta = ((int32_t)(lightness_end - lightness_begin)) / breath_led->forward_steps;
    breath_led->current_reverse_steps = 0;
    breath_led->reverse_steps = total_steps - breath_led->forward_steps;
    breath_led->reverse_step_delta = (int32_t)(lightness_begin - lightness_end) / breath_led->reverse_steps;
    breath_led->half_breath_end = half_breath_end;

    qm_mutex_unlock(&g_breath_led_info.breath_lock);

    /* Check light controler status */
    if(is_idle){
        qm_timer_start(&g_breath_led_info.breath_led_timer);
    }
}   

static void light_stop(breath_led_handle_t *breath_led)
{
    qm_mutex_lock(&g_breath_led_info.breath_lock, QM_WAIT_FOREVER);

    breath_led->is_active = QM_FALSE;

    if(breath_led->half_breath_end){
        light_lighten(&(breath_led->pwm), breath_led->lightness_end);
    }else{
        light_lighten(&(breath_led->pwm), breath_led->lightness_begin);
    }

    qm_mutex_unlock(&g_breath_led_info.breath_lock);
}

void led_breath_controller_init(void)
{
    qm_mutex_new(&g_breath_led_info.breath_lock);
    qm_timer_new(&g_breath_led_info.breath_led_timer, light_ctl_timeout_handle, NULL, LED_BREATH_MONITOR_INTERVAL, QM_TRUE);
}


qm_err_t led_breath_deinit(int gpio_pin)
{
    breath_led_handle_t *breath_led = find_led_handle(gpio_pin);
    if(breath_led == NULL){
        return -QM_EINVAL;
    }

    breath_led->is_use = QM_FALSE;
    qm_pwm_stop(&(breath_led->pwm));
    qm_pwm_deinit(&(breath_led->pwm));

    return QM_EOK;
}

qm_err_t led_breath_init(int gpio_pin, int breath_time)
{
    breath_led_handle_t *breath_led = request_led_handle();
    if(breath_led == NULL){
        return -QM_EINVAL;
    }

    breath_led->is_use = QM_TRUE;
    breath_led->breath_time = breath_time;

    breath_led->pwm.port = gpio_pin % LED_BREATH_PWM_CHANNAL_MAX;
    breath_led->pwm.pin.pwm_pin = gpio_pin;
    breath_led->pwm.config.pulse = 0;
    breath_led->pwm.config.freq = LED_BREATH_PWM_LIGHT_FREQUENCY;

    qm_pwm_init(&(breath_led->pwm));
    qm_pwm_start(&(breath_led->pwm));

    return QM_EOK;
}


qm_err_t led_breath_start(int gpio_pin)
{    
    breath_led_handle_t *breath_led = find_led_handle(gpio_pin);
    if(breath_led == NULL){
        return -QM_EINVAL;
    }

    light_breath(breath_led, 0, LED_BREATH_PWM_MAX_PULSE, breath_led->breath_time, 50, QM_FALSE);
    return QM_EOK;
}

qm_err_t led_breath_stop(int gpio_pin)
{
    breath_led_handle_t *breath_led = find_led_handle(gpio_pin);
    if(breath_led == NULL){
        return -QM_EINVAL;
    }

    light_stop(breath_led);
    
    return QM_EOK;
}

qm_err_t led_breath_fast_start(int gpio_pin, uint16_t en_level)
{
    breath_led_handle_t *breath_led = find_led_handle(gpio_pin);
    if(breath_led == NULL){
        return NULL;
    }

    qm_mutex_lock(&g_breath_led_info.breath_lock, QM_WAIT_FOREVER);

    breath_led->is_active = QM_FALSE;

    light_lighten(&(breath_led->pwm), en_level);
  
    qm_mutex_unlock(&g_breath_led_info.breath_lock);

    return QM_EOK;
}