#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"

#include "sleep.h"
#include "em_gpio.h"
#include "em_rtcc.h"
#include "app_main.h"

static lpc_callback_t wakeup_cb = NULL;

static int32_t g_sleep_status = 0;
extern void xj_thread_timer_start(bool start);

void enter_sleep(void)
{
    g_sleep_status = qm_lpc_check();
    if(g_sleep_status != QM_LPC_NO_SLEEP) {
        qm_lpc_prepare();
        xj_thread_timer_start(false);
        SLEEP_SleepBlockEnd(sleepEM3); 
    }
}

int32_t qm_hal_lpc_init(void)
{
    //TODO: impl
    SLEEP_SleepBlockBegin(sleepEM3); //run in EM0/EM1/EM2 state
    return QM_EOK;
}


int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    //TODO: impl
    return QM_EOK;
}

void gpio_wakup(uint8_t pin)
{
    if(g_sleep_status == QM_LPC_NO_SLEEP){
        return;
    }
    if(wakeup_cb){
        wakeup_cb();
    }
    qm_lpc_resume();
    SLEEP_SleepBlockBegin(sleepEM3);
    xj_thread_timer_start(true);
    g_sleep_status = QM_LPC_NO_SLEEP;
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable)
{
    //GPIO wakeup, set and enable irq
    int gpio_port = (pin >> 4) & 0xF;
    int gpio_pin = pin & 0xF; 
    int pull = 0;
    if ((type == QM_LPC_WAKEUP_NEGEDGE) || (type == QM_LPC_WAKEUP_LOW_LEVEL)) {
        pull = 1;
    }
    if (enable) {
        wakeup_cb = cb;
    } else {
        wakeup_cb = NULL;
    }
    GPIO_PinModeSet(gpio_port, gpio_pin, gpioModeInputPull, pull);
    //setup interrupt
    GPIO_ExtIntConfig(gpio_port, gpio_pin, gpio_pin, pull ? false : true, pull ? true : false, enable);
    GPIOINT_CallbackRegister(gpio_pin, gpio_wakup);

    return QM_EOK;
}
