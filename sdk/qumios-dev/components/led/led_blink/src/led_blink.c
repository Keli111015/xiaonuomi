#include "led_blink.h"
#include "qm_kernel.h"

typedef struct {
    qm_gpio_dev_t gpio_dev;
    qm_timer_t timer;
    led_blink_close_level_t close_level;
    int led_on;
} led_blink_dev_t;

static void led_blink_timeout(qm_timer_t *timer, void *arg)
{
    led_blink_dev_t *led_blink_dev = (led_blink_dev_t*)arg;

    if(led_blink_dev->led_on){
        qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x00 ^ led_blink_dev->close_level);
        led_blink_dev->led_on = 0;
    }else{
        qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x01 ^ led_blink_dev->close_level);
        led_blink_dev->led_on = 1;
    }
}

led_blink_handle_t led_blink_create(led_blink_io_t *io, led_blink_close_level_t close_level, int blink_freq)
{
    qm_err_t ret = QM_EOK;
    led_blink_dev_t *led_blink_dev= NULL;
    
    if(io == NULL){
        return NULL;
    }
    if(blink_freq < LED_BLINK_FREQ_MIN || blink_freq > LED_BLINK_FREQ_MAX){
        return NULL;
    }

    led_blink_dev = (led_blink_dev_t*)qm_malloc(sizeof(led_blink_dev_t));
    if(led_blink_dev == NULL){
        return NULL;
    }

    memset(led_blink_dev, 0, sizeof(led_blink_dev_t));
    led_blink_dev->close_level = close_level;

    led_blink_dev->gpio_dev.port = io->port;
    led_blink_dev->gpio_dev.config.mode = QM_GPIO_MODE_OUTPUT;
    led_blink_dev->gpio_dev.config.pull_en = QM_GPIO_FLOATING;

    ret = qm_gpio_init(&led_blink_dev->gpio_dev);
    if(ret != QM_EOK){
        goto __exit;
    }
    qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x00 ^ led_blink_dev->close_level);
    
    ret = qm_timer_new(&led_blink_dev->timer, led_blink_timeout, (void*)led_blink_dev, (1000/blink_freq)/2, 1);
    if(ret != QM_EOK){
        goto __exit;
    }

    return led_blink_dev;

__exit:
    qm_free(led_blink_dev);
    return NULL;
}

qm_err_t led_blink_change(led_blink_handle_t handle, int blink_freq)
{
    qm_err_t ret = QM_EOK;
    led_blink_dev_t *led_blink_dev = (led_blink_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    if(blink_freq < LED_BLINK_FREQ_MIN || blink_freq > LED_BLINK_FREQ_MAX){
        return -QM_EINVAL;
    }

    qm_timer_stop(&led_blink_dev->timer);

    led_blink_dev->led_on = 1;
    qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x01 ^ led_blink_dev->close_level);

    ret = qm_timer_change(&led_blink_dev->timer, (1000/blink_freq)/2);
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_timer_start(&led_blink_dev->timer);   
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}   

qm_err_t led_blink_open(led_blink_handle_t handle, led_status_t status)
{
    qm_err_t ret = QM_EOK;
    led_blink_dev_t *led_blink_dev = (led_blink_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    qm_timer_stop(&led_blink_dev->timer);
    led_blink_dev->led_on = 1;
    qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x01 ^ led_blink_dev->close_level);
    if(LED_STATUS_BLINK == status){
        ret = qm_timer_start(&led_blink_dev->timer);   
        if(ret != QM_EOK){
            return ret;
        }
    }
    return QM_EOK;
}

qm_err_t led_blink_close(led_blink_handle_t handle)
{
    led_blink_dev_t *led_blink_dev = (led_blink_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }
    qm_timer_stop(&led_blink_dev->timer);
    led_blink_dev->led_on = 0;
    qm_gpio_set_level(&led_blink_dev->gpio_dev, 0x00 ^ led_blink_dev->close_level);
    return QM_EOK;
}

qm_err_t led_blink_delete(led_blink_handle_t handle)
{
    led_blink_dev_t *led_blink_dev = (led_blink_dev_t*)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_timer_stop(&led_blink_dev->timer);
    qm_timer_free(&led_blink_dev->timer);
    qm_gpio_deinit(&led_blink_dev->gpio_dev);
    qm_free(led_blink_dev);
    return QM_EOK;
}