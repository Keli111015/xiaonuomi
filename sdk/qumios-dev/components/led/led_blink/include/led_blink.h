#ifndef _LED_BLINK_H_
#define _LED_BLINK_H_

#include "qm_gpio.h"
#include "qm_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* led_blink_handle_t;

typedef enum {
    LED_BLINK_CLOSE_LOW = 0,            /**< pass this param to led_blink_create if the led is closed when control-gpio level is low */
    LED_BLINK_CLOSE_HIGH = 1,           /**< pass this param to led_blink_create if the led is closed when control-gpio level is high */
} led_blink_close_level_t;

typedef enum {
    LED_STATUS_NORMAL, //led on or off status
    LED_STATUS_BLINK
}led_status_t;

typedef struct {
    uint8_t port;
} led_blink_io_t;

#define LED_BLINK_FREQ_MIN    1
#define LED_BLINK_FREQ_MAX    5

/**
  * @brief create led blink object.
  *
  * @param io gpio number(s) of led blink
  * @param close_level close voltage level of relay
  * @param blink_freq the frequency of led blink  range 1 to 5
  *
  * @return led_blink_handle_t the handle of the led blink created 
  */
led_blink_handle_t led_blink_create(led_blink_io_t *io, led_blink_close_level_t close_level, int blink_freq);

/**
  * @brief open led blink
  *
  * @param  handle
  * @param  status the status of led
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t led_blink_open(led_blink_handle_t handle, led_status_t status);

/**
  * @brief change led blink param
  *
  * @param  handle
  * @param  blink_param the param of led blink
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t led_blink_change(led_blink_handle_t handle, int blink_freq);

/**
  * @brief close led blink
  *
  * @param  handle
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t led_blink_close(led_blink_handle_t handle);

/**
  * @brief free the memory of led blink
  *
  * @param  relay_handle
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t led_blink_delete(led_blink_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
