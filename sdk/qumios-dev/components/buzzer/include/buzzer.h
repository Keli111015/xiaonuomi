#ifndef _BUZZER_H_
#define _BUZZER_H_

#include "qm.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_BUZZER_PASSIVE_SUPPORT
#define CONFIG_BUZZER_PASSIVE_SUPPORT (0)
#endif

#ifndef CONFIG_BUZZER_ACTIVE_SUPPORT
#define CONFIG_BUZZER_ACTIVE_SUPPORT (0)
#endif

#ifndef CONFIG_BUZZER_CHRIP_INTERVAL 
#define CONFIG_BUZZER_CHRIP_INTERVAL  (500)
#endif

#if !CONFIG_BUZZER_ACTIVE_SUPPORT && !CONFIG_BUZZER_PASSIVE_SUPPORT
#error "please config active buzzer or passive buzzer"
#endif


typedef void* buzzer_handle_t;

typedef enum {
    BUZZER_TPYE_PASSIVE = 0,   /**<passive buzzer>*/         
    BUZZER_TPYE_ACTIVE = 1,    /**<active buzzer>*/    
} buzzer_type_t;

typedef enum {
    BUZZER_CLOSE_LOW = 0,            /**< pass this param to buzzer_create if the buzzer is closed when control-gpio level is low */
    BUZZER_CLOSE_HIGH = 1,           /**< pass this param to buzzer_create if the buzzer is closed when control-gpio level is high */
} buzzer_close_level_t;

#if CONFIG_BUZZER_PASSIVE_SUPPORT
typedef struct {
    uint8_t port;
    uint8_t pin;
    int buzzer_freq;
    int buzzer_pulse;
}buzzer_passive_param_t;
#endif

#if CONFIG_BUZZER_ACTIVE_SUPPORT
typedef struct {
    uint8_t port;
    buzzer_close_level_t close_level;
}buzzer_active_param_t;
#endif

typedef struct {
    int count;              /**<the count of buzzer chirping >*/
    int chirping_time;      /**<the time of buzzer chirping  unit: ms >*/
    buzzer_type_t type;
    union 
    {
  #if CONFIG_BUZZER_ACTIVE_SUPPORT
      buzzer_active_param_t active_buzzer;
  #endif
  #if CONFIG_BUZZER_PASSIVE_SUPPORT
      buzzer_passive_param_t passive_buzzer;
  #endif
    }buzzer;
}buzzer_param_t;

/**
  * @brief create buzzer object.
  *
  * @param param config buzzer param
  *
  * @return buzzer_handle_t the handle of the buzzer created 
  */
buzzer_handle_t buzzer_create(buzzer_param_t *param);

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
qm_err_t buzzer_open(buzzer_handle_t handle);

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
qm_err_t buzzer_change(buzzer_handle_t handle, int count, int chirping_time);

/**
  * @brief close buzzer
  *
  * @param  handle
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_close(buzzer_handle_t handle);

/**
  * @brief free the memory of buzzer
  *
  * @param  relay_handle
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t buzzer_delete(buzzer_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
