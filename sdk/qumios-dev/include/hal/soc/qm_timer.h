#ifndef QM_TIMER_H
#define QM_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_timer TIMER
 * qm timer API.
 *
 *  @{
 */

#include "qm_types.h"

#define TIMER_RELOAD_AUTO  1 /**< timer reload automatic */
#define TIMER_RELOAD_MANU  2 /**< timer reload manual */

/* Define timer handle function type */
typedef void (*hal_timer_cb_t)(void *arg);

/* Define timer config args */
typedef struct {
    uint32_t        period;         /**< timer period, us */
    uint8_t         reload_mode;    /**< auto reload or not */
    hal_timer_cb_t  cb;             /**< timer handle when expired */
    void           *arg;            /**< timer handle args */
} qm_timer_dev_config_t;

/* Define timer dev handle */
typedef struct {
    int8_t                 port;   /**< timer port */
    qm_timer_dev_config_t  config; /**< timer config */
    void                   *priv;   /**< priv data */
} qm_timer_dev_t;

/**
 * init a hardware timer
 *
 * @param[in]  tim  timer device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_timer_dev_init(qm_timer_dev_t *tim);

/**
 * start a hardware timer
 *
 * @param[in]  tim  timer device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_timer_dev_start(qm_timer_dev_t *tim);

/**
 * stop a hardware timer
 *
 * @param[in]  tim  timer device
 *
 * @return  none
 */
void qm_timer_dev_stop(qm_timer_dev_t *tim);

/**
 * change the config of a hardware timer
 *
 * @param[in]  tim   timer device
 * @param[in]  para  timer config
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_timer_dev_para_change(qm_timer_dev_t *tim, qm_timer_dev_config_t para);

/**
 * De-initialises an TIMER interface, Turns off an TIMER hardware interface
 *
 * @param[in]  tim  timer device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_timer_dev_deinit(qm_timer_dev_t *tim);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_TIMER_H */
