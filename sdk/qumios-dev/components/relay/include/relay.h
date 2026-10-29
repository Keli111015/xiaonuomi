#ifndef _QM_RELAY_H_
#define _QM_RELAY_H_

#include "qm_gpio.h"
#include "qm_errno.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef void* relay_handle_t;

typedef enum {
    RELAY_CLOSE_LOW = 0,            /**< pass this param to relay_create if the relay is closed when control-gpio level is low */
    RELAY_CLOSE_HIGH = 1,           /**< pass this param to relay_create if the relay is closed when control-gpio level is high */
} relay_close_level_t;

typedef enum {
    RELAY_STATUS_CLOSE = 0,
    RELAY_STATUS_OPEN,
} relay_status_t;

typedef struct {
    uint8_t port;
} relay_io_t;


/**
  * @brief create relay object.
  *
  * @param relay_io gpio number(s) of relay
  * @param close_level close voltage level of relay
  *
  * @return relay_handle_t the handle of the relay created 
  */
relay_handle_t relay_create(relay_io_t *relay_io, relay_close_level_t close_level);

/**
  * @brief set state of relay
  *
  * @param  relay_handle
  * @param  state RELAY_STATUS_CLOSE or RELAY_STATUS_OPEN
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t relay_state_write(relay_handle_t relay_handle, relay_status_t state);

/**
  * @brief get state of relay
  *
  * @param relay_handle
  *
  * @return state of the relay
  */
relay_status_t relay_state_read(relay_handle_t relay_handle);

/**
  * @brief free the memory of relay
  *
  * @param  relay_handle
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t relay_delete(relay_handle_t relay_handle);

#ifdef __cplusplus
}
#endif

#endif
