
#ifndef _MULTI_TIMER_H_
#define _MULTI_TIMER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

typedef struct multi_timer multi_timer_t;

typedef void (*multi_timer_cb_t)(multi_timer_t* timer, void* arg);

struct multi_timer{
    struct multi_timer *next;
    uint32_t delay;
    uint32_t period;
    multi_timer_cb_t callback;
    void *arg;
    int auto_reload;
    void **list;             /*< Pointer to the list in which this list item is placed (if any). */
};

/**
 * initilize timer.
 * 
 * @return  0 on success, otherwise failure.
 */
int multi_timer_init(void);

/**
 * This function will create a timer.
 *
 * @param[in]  timer   pointer to the timer.
 * @param[in]  cb      callbak of the timer.
 * @param[in]  arg     the argument of the callback.
 * @param[in]  ms      ms of the normal timer triger.
 * @param[in]  auto_reload  The timers will auto-reload themselves when they expire.
 *
 * @return  0 on success, otherwise failure.
 */
int multi_timer_creat(multi_timer_t *timer, multi_timer_cb_t cb, void *arg, uint32_t ms, int auto_reload);

/**
 * This function will delete a timer.
 *
 * @param[in]  timer  pointer to a timer.
 *  
 * @return  0 on success, otherwise failure.
 */
int multi_timer_delete(multi_timer_t *timer);

/**
 * This function will start a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0 on success, otherwise failure.
 */
int multi_timer_start(multi_timer_t *timer);

/**
 * This function will stop a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0 on success, otherwise failure.
 */
int multi_timer_stop(multi_timer_t *timer);

/**
 * This function will change attributes of a timer.
 *
 * @param[in]  timer  pointer to the timer.
 * @param[in]  ms     ms of the timer triger.
 *
 * @return  0 on success, otherwise failure.
 */
int multi_timer_change(multi_timer_t *timer, uint32_t ms);

/**
 * @brief Check the timer expried and call callback.
 * 
 * @return int The next timer expires.
 */
uint32_t multi_timer_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* MULTI_TIMER_H */

