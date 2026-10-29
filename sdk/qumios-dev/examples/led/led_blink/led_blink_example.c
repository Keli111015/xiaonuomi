#include "qm.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "led_blink.h"

#define LOG_TAG  "led blink"

void qm_application_start(void)
{
    led_blink_io_t io = {
        .port = 10
    };
    led_blink_handle_t led_blink_handle = NULL;
    led_blink_handle = led_blink_create(&io, LED_BLINK_CLOSE_LOW, 1);
    if(led_blink_handle == NULL){
        QM_LOGE(LOG_TAG, "led blink create fail");
        return;
    }

    led_blink_open(led_blink_handle, LED_STATUS_NORMAL);
    qm_msleep(1000);
    led_blink_open(led_blink_handle, LED_STATUS_BLINK);
    qm_msleep(10000);
    led_blink_close(led_blink_handle);

    led_blink_delete(led_blink_handle);
}