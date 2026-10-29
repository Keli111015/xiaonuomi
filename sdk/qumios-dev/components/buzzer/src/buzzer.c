#include "qm.h"
#include "buzzer.h"

#if CONFIG_BUZZER_PASSIVE_SUPPORT
#include "qm_pwm.h"
#endif

#if CONFIG_BUZZER_ACTIVE_SUPPORT
#include "qm_gpio.h"
#endif

typedef struct {

#if CONFIG_BUZZER_PASSIVE_SUPPORT
    qm_pwm_dev_t pwm_dev;
#endif

#if CONFIG_BUZZER_ACTIVE_SUPPORT
    qm_gpio_dev_t gpio_dev;
    buzzer_close_level_t close_level;
#endif
    int buzzer_on;
    qm_timer_t timer;
    int chirping_time;
    int count;
    buzzer_type_t type;
} buzzer_dev_t;


static qm_err_t buzzer_new(buzzer_dev_t *buzzer_dev, buzzer_param_t *param)
{
    qm_err_t ret = QM_EOK;

    #if CONFIG_BUZZER_PASSIVE_SUPPORT
    if(param->type == BUZZER_TPYE_PASSIVE){
        buzzer_dev->pwm_dev.port = param->buzzer.passive_buzzer.port;
        buzzer_dev->pwm_dev.pin.pwm_pin = param->buzzer.passive_buzzer.pin;
        buzzer_dev->pwm_dev.config.pulse = param->buzzer.passive_buzzer.buzzer_pulse;
        buzzer_dev->pwm_dev.config.freq = param->buzzer.passive_buzzer.buzzer_freq;
        qm_pwm_init(&buzzer_dev->pwm_dev);
        if(ret != QM_EOK){
            return ret;
        }
        qm_pwm_stop(&buzzer_dev->pwm_dev);
    }
    #endif
    #if CONFIG_BUZZER_ACTIVE_SUPPORT
    if(param->type == BUZZER_TPYE_ACTIVE){
        buzzer_dev->close_level = param->buzzer.active_buzzer.close_level;
        buzzer_dev->gpio_dev.port = param->buzzer.active_buzzer.port;
        buzzer_dev->gpio_dev.config.mode = QM_GPIO_MODE_OUTPUT;
        buzzer_dev->gpio_dev.config.pull_en = QM_GPIO_FLOATING;
        ret = qm_gpio_init(&buzzer_dev->gpio_dev);
        if(ret != QM_EOK){
            return ret;
        }
        qm_gpio_set_level(&buzzer_dev->gpio_dev, 0x00 ^ buzzer_dev->close_level);
    }
    #endif

    buzzer_dev->type = param->type;
    return ret;
}

static void buzzer_del(buzzer_dev_t *buzzer_dev)
{
    #if CONFIG_BUZZER_PASSIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_PASSIVE){
        qm_pwm_deinit(&buzzer_dev->pwm_dev);
    }
    #endif
    #if CONFIG_BUZZER_ACTIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_ACTIVE){
        qm_gpio_deinit(&buzzer_dev->gpio_dev);
    }
    #endif
}

static void buzzer_off(buzzer_dev_t *buzzer_dev)
{
    #if CONFIG_BUZZER_PASSIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_PASSIVE){
        qm_pwm_stop(&buzzer_dev->pwm_dev);
    }
    #endif
    #if CONFIG_BUZZER_ACTIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_ACTIVE){
        qm_gpio_set_level(&buzzer_dev->gpio_dev, 0x00 ^ buzzer_dev->close_level);
    }
    #endif
}

static void buzzer_on(buzzer_dev_t *buzzer_dev)
{
    #if CONFIG_BUZZER_PASSIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_PASSIVE){
        qm_pwm_start(&buzzer_dev->pwm_dev);
    }
    #endif
    #if CONFIG_BUZZER_ACTIVE_SUPPORT
    if(buzzer_dev->type == BUZZER_TPYE_ACTIVE){
        qm_gpio_set_level(&buzzer_dev->gpio_dev, 0x01 ^ buzzer_dev->close_level);
    }
    #endif
}

static void buzzer_timeout(qm_timer_t *timer, void *arg)
{
    buzzer_dev_t *buzzer_dev = (buzzer_dev_t*)arg;

    if(buzzer_dev->buzzer_on){
        buzzer_off(buzzer_dev);
        buzzer_dev->buzzer_on = 0;
        qm_timer_change(&buzzer_dev->timer, CONFIG_BUZZER_CHRIP_INTERVAL);
        qm_timer_start(&buzzer_dev->timer);
    }else{
        if(--buzzer_dev->count > 0){
            buzzer_on(buzzer_dev);
            buzzer_dev->buzzer_on = 1;
            qm_timer_change(&buzzer_dev->timer, buzzer_dev->chirping_time);
            qm_timer_start(&buzzer_dev->timer);
        }
    }
}

/**
  * @brief create buzzer object.
  *
  * @param param config buzzer param
  *
  * @return buzzer_handle_t the handle of the buzzer created 
  */
buzzer_handle_t buzzer_create(buzzer_param_t *param)
{
    qm_err_t ret = QM_EOK;
    buzzer_dev_t *buzzer_dev= NULL;

    if(param == NULL){
        return NULL;
    }

    buzzer_dev = (buzzer_dev_t*)qm_malloc(sizeof(buzzer_dev_t));
    if(buzzer_dev == NULL){
        return NULL;
    }
    memset(buzzer_dev, 0, sizeof(buzzer_dev_t));
    
    ret = buzzer_new(buzzer_dev, param);
    if(ret != QM_EOK){
        goto __exit;
    }

    buzzer_dev->count = param->count;
    buzzer_dev->chirping_time = param->chirping_time;
    ret = qm_timer_new(&buzzer_dev->timer, buzzer_timeout, (void*)buzzer_dev, param->chirping_time, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    return (buzzer_handle_t)buzzer_dev;

__exit:
    qm_free(buzzer_dev);
    return NULL;
}

/**
  * @brief open buzzer
  *
  * @param  handle
  * @param  status the status of buzzer
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_open(buzzer_handle_t handle)
{
    qm_err_t ret = QM_EOK;
    buzzer_dev_t *buzzer_dev = (buzzer_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_timer_stop(&buzzer_dev->timer);

    buzzer_dev->buzzer_on = 1;
    buzzer_on(buzzer_dev);

    ret = qm_timer_change(&buzzer_dev->timer, buzzer_dev->chirping_time); 
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_timer_start(&buzzer_dev->timer);   
    if(ret != QM_EOK){
        return ret;
    }

    return QM_EOK;
}

/**
  * @brief change buzzer param
  *
  * @param  handle
  * @param  blink_param the param of buzzer
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_change(buzzer_handle_t handle, int count, int chirping_time)
{
    qm_err_t ret = QM_EOK;
    buzzer_dev_t *buzzer_dev = (buzzer_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_timer_stop(&buzzer_dev->timer);
    
    buzzer_dev->count = count;
    buzzer_dev->buzzer_on = 1;
    buzzer_dev->chirping_time = chirping_time;

    buzzer_on(buzzer_dev);

    ret = qm_timer_change(&buzzer_dev->timer, buzzer_dev->chirping_time); 
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_timer_start(&buzzer_dev->timer);   
    if(ret != QM_EOK){
        return ret;
    }

    return QM_EOK;
}


/**
  * @brief close buzzer
  *
  * @param  handle
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_close(buzzer_handle_t handle)
{
    buzzer_dev_t *buzzer_dev = (buzzer_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_timer_stop(&buzzer_dev->timer);
    buzzer_dev->buzzer_on = 0;
    buzzer_off(buzzer_dev);
    return QM_EOK;
}


/**
  * @brief free the memory of buzzer
  *
  * @param  relay_handle
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_delete(buzzer_handle_t handle)
{
    buzzer_dev_t *buzzer_dev = (buzzer_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_timer_stop(&buzzer_dev->timer);
    qm_timer_free(&buzzer_dev->timer);
    buzzer_del(buzzer_dev);
    qm_free(buzzer_dev);
    return QM_EOK;
}
