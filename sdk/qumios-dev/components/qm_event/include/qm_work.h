#ifndef __QM_WORK_H__
#define __QM_WORK_H__

#include "qm_kernel.h"
#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_QM_WORK_SUPPORT 
#include "qm_event.h"

/**
 * Delayed work callback invoked by qm_work after the requested delay.
 */
typedef void (*work_fn_t)(void *arg);

typedef struct {
    /** User argument passed to the delayed callback. */
    void *arg;
    /** Callback executed on the default event loop after the delay expires. */
    work_fn_t fn;
    /** Remaining delay in milliseconds; zero when the work is queued for immediate dispatch. */
    uint32_t dly;
    /** OS timer used when dly is non-zero. */
    qm_timer_t timer;
    /** Monotonic sequence used to ignore stale timer or queue events after cancel/repost. */
    uint32_t seq;
    /** Magic marker set when this handle is managed by qm_work. */
    uint32_t magic;
    /** Whether a delayed action is currently pending on this handle. */
    uint8_t active;
} qm_work_t;

/**
 * Post a delayed action to be executed in main loop.
 *
 * @param[in]  work    handle of work
 * @param[in]  action  action to be executed.
 * @param[in]  arg     private data past to action.
 * @param[in]  ms      milliseconds to wait.
 *
 * @return  the operation status, 0 is OK,others is error.
 */
int32_t qm_post_delayed_action(qm_work_t *work, void (*fn)(void *), void *arg, uint32_t ms);

/**
 * Cancel a delayed action to be executed in main loop.
 *
 * @param[in]  work  handle of work
 * 
 * @return  the operation status, 0 is OK,others is error.
 */
int32_t qm_cancel_delayed_action(qm_work_t *work);

/**
 * Initialize the qm work component selected in Kconfig.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_work_on_init(void);


#if CONFIG_QM_WORKQUEUE_NO_TASK
/**
 * @brief 当workqueue不使用task时，需要调用此函数来处理workqueue
 * 
 */
void qm_workqueue_process(void);
#endif


#endif

#endif
