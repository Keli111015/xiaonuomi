#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"

// #include "sleep.h"
// #include "em_gpio.h"
// #include "em_rtcc.h"
// #include "app_main.h"

static lpc_callback_t wakeup_cb = NULL;

int32_t qm_hal_lpc_init(void)
{
    //TODO: impl
    // SLEEP_SleepBlockBegin(sleepEM3); //run in EM0/EM1/EM2 state
    return QM_EOK;
}


int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    //TODO: impl
    return QM_EOK;
}

void gpio_wakup(uint8_t pin)
{
    QM_LOGD("LOG", "gpio wakeup:%d\n", pin);
    // enter_sleep(false);
    // if (wakeup_cb)
    // {
    //     wakeup_cb();
    // }
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable)
{
    //GPIO wakeup, set and enable irq
    // int gpio_port = (pin >> 4) & 0xF;
    // int gpio_pin = pin & 0xF; 
    // int pull = 0;
    // if ((type == QM_LPC_WAKEUP_NEGEDGE) || (type == QM_LPC_WAKEUP_LOW_LEVEL)) {
    //     pull = 1;
    // }
    // if (enable) {
    //     wakeup_cb = cb;
    // } else {
    //     wakeup_cb = NULL;
    // }
    // GPIO_PinModeSet(gpio_port, gpio_pin, gpioModeInputPull, pull);
    // //setup interrupt
    // GPIO_ExtIntConfig(gpio_port, gpio_pin, gpio_pin, pull ? false : true, pull ? true : false, enable);

    // GPIOINT_CallbackRegister(gpio_pin, gpio_wakup);

    return QM_EOK;
}
