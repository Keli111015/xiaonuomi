#include "qm.h"
#include "qm_gpio.h"
#include "qm_work.h"
#include "qm_event.h"
#include "multi_button.h"

#define LOG_TAG "example"
/**
 * Event base used by the multi-button example.
 */
static qm_event_base_t const QM_EVENT_MULTI_BUTTON_EVENT = "QM_EVENT_MULTI_BUTTON_EVENT";
#define QM_EVENT_MULTI_BUTTON_PRESS_EVENT       (0x1)



static multi_button_t button_set = {0};
static qm_timer_t button_timer = {0};

static qm_gpio_dev_t gpio_dev = {
        .port = 27,
        .config = {
            .mode = QM_GPIO_MODE_INPUT,
            .pull_en = QM_GPIO_PULLUP_ONLY
        },
        .priv = NULL
    };

static void button_isr_handler(void *arg)
{
    qm_timer_start_from_isr(&button_timer);
}

void button_press_up_handler(void* btn)
{
    qm_timer_stop(&button_timer);
}

static uint8_t pin_level_get(void)
{
    return qm_gpio_get_level(&gpio_dev);
}

static void button_ticks_timeout(qm_timer_t *timer, void *arg)
{
    multi_button_ticks();
}

void button_press_down_handler(void *btn)
{
    qm_event_post(QM_EVENT_MULTI_BUTTON_EVENT, QM_EVENT_MULTI_BUTTON_PRESS_EVENT, NULL, 0, QM_NO_WAIT);
}

static void button_press_notify_callback(void *event_handler_arg, qm_event_base_t event_base,
                                         int32_t event_id, void *event_data, size_t event_data_size)
{
    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_id);
    QM_UNUSED_ARG(event_data);
    QM_UNUSED_ARG(event_data_size);

    QM_LOGD(LOG_TAG, "button press successful !!");
}

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;

    qm_gpio_init(&gpio_dev);
    qm_gpio_enable_irq(&gpio_dev, QM_GPIO_INTR_NEGEDGE, button_isr_handler, (void*)(int)gpio_dev.port);

    ret = qm_timer_new(&button_timer, button_ticks_timeout, NULL, CONFIG_MULTI_BUTTON_TICKS_INTERVAL, 1);
    if(ret != QM_EOK){
        qm_gpio_disable_irq(&gpio_dev);
        return -QM_ERROR;
    }

    multi_button_init(&button_set, pin_level_get, 0);
    multi_button_attach(&button_set, PRESS_EVENT_DOWN, button_press_down_handler);
    multi_button_attach(&button_set, PRESS_EVENT_UP, button_press_up_handler);
    multi_button_start(&button_set);

    qm_event_handler_register(QM_EVENT_MULTI_BUTTON_EVENT, QM_EVENT_MULTI_BUTTON_PRESS_EVENT,
                                   button_press_notify_callback, NULL);

    return QM_EOK;
}
