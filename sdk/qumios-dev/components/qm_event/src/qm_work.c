#include "qm.h"
#include "qm_work.h"
#include "qm_errno.h"

#if CONFIG_QM_WORK_SUPPORT

/**
 * Event id reserved for delayed-action dispatch on the default event loop.
 */
#define QM_WORK_DELAYED_ACTION_ID           0

/**
 * Magic marker written into qm_work_t handles managed by this module.
 */
#define QM_WORK_DELAYED_ACTION_MAGIC        0x514d5741u

/**
 * Upper bound for delayed-action sequence numbers before they wrap back to one.
 */
#define QM_WORK_DELAYED_ACTION_MAX_SEQ      0x7fffffffu

/**
 * Internal event base used to route delayed actions through qm_event.
 */
QM_EVENT_DEFINE_BASE(QM_WORK_DELAYED_ACTION);

/**
 * UART event base; no dedicated UART component owns this symbol yet.
 */
QM_EVENT_DEFINE_BASE(QM_EVENT_UART);

/**
 * Payload posted to the default event loop when a delayed action becomes ready.
 */
typedef struct {
    /** Delayed-action handle that owns the callback. */
    qm_work_t *work;
    /** Sequence number captured at post time; stale events are ignored. */
    uint32_t seq;
} qm_work_delayed_action_payload_t;

/**
 * Whether qm_work_on_init() has completed successfully.
 */
static uint8_t g_work_inited;

/**
 * Next delayed-action sequence number allocated by qm_post_delayed_action().
 */
static uint32_t g_delayed_action_next_seq;



#if CONFIG_QM_OS_SUPPORT
/**
 * Lock protecting delayed-action state stored in qm_work_t handles.
 */
static qm_mutex_t g_work_lock;

/**
 * Whether g_work_lock has been created.
 */
static uint8_t g_work_lock_inited;
#endif

/**
 * Acquire the delayed-action lock.
 *
 * @return QM_EOK on success, negative errno otherwise.
 */
static int32_t qm_work_delayed_action_lock(void)
{
#if CONFIG_QM_OS_SUPPORT
    if(!g_work_lock_inited){
        return -QM_EINVAL;
    }

    return qm_mutex_lock(&g_work_lock, QM_WAIT_FOREVER);
#else
    return QM_EOK;
#endif
}

/**
 * Release the delayed-action lock.
 *
 * @return QM_EOK on success, negative errno otherwise.
 */
static int32_t qm_work_delayed_action_unlock(void)
{
#if CONFIG_QM_OS_SUPPORT
    if(!g_work_lock_inited){
        return -QM_EINVAL;
    }

    return qm_mutex_unlock(&g_work_lock);
#else
    return QM_EOK;
#endif
}

/**
 * Allocate the next delayed-action sequence number.
 *
 * @return Non-zero sequence number.
 */
static uint32_t qm_work_delayed_action_next_seq(void)
{
    g_delayed_action_next_seq++;
    if((g_delayed_action_next_seq == 0) ||
       (g_delayed_action_next_seq > QM_WORK_DELAYED_ACTION_MAX_SEQ)){
        g_delayed_action_next_seq = 1;
    }

    return g_delayed_action_next_seq;
}

/**
 * Stop and free any OS timer owned by a delayed-action handle.
 *
 * @param[in,out] work Delayed-action handle whose timer should be released.
 */
static void qm_work_delayed_action_release(qm_work_t *work)
{
    if((work == NULL) || (work->magic != QM_WORK_DELAYED_ACTION_MAGIC)){
        return;
    }

#if defined(CONFIG_QM_OS_TIMER_SUPPORT) && CONFIG_QM_OS_TIMER_SUPPORT
    if(work->timer.hdl != NULL){
        (void)qm_timer_stop(&work->timer);
        (void)qm_timer_free(&work->timer);
        work->timer.hdl = NULL;
    }
#endif
}

/**
 * Ensure the default event loop exists before posting delayed actions.
 *
 * @return QM_EOK on success, negative errno otherwise.
 */
static int32_t qm_work_default_loop_ready(void)
{
    int32_t ret;

    ret = qm_event_default_create();
    if((ret == -QM_EINIT) || (ret == QM_EOK)){
        return QM_EOK;
    }

    return ret;
}

/**
 * Post a delayed-action dispatch event to the default event loop.
 *
 * @param[in] work Delayed-action handle to dispatch.
 * @param[in] seq Sequence number that must still match when the event is handled.
 *
 * @return QM_EOK on success, negative errno otherwise.
 */
static int32_t qm_work_post_delayed_action_event(qm_work_t *work, uint32_t seq)
{
    qm_work_delayed_action_payload_t payload;

    payload.work = work;
    payload.seq = seq;

    return qm_event_post(QM_WORK_DELAYED_ACTION,
                              QM_WORK_DELAYED_ACTION_ID,
                              &payload,
                              sizeof(payload),
                              QM_NO_WAIT);
}

/**
 * Dispatch a delayed action when its event reaches the default event loop.
 *
 * @param[in] event_handler_arg Unused registration argument.
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Copied payload posted by qm_work_post_delayed_action_event().
 * @param[in] event_data_size Payload size in bytes.
 */
static void qm_work_delayed_action_handler(void *event_handler_arg,
                                           qm_event_base_t event_base,
                                           int32_t event_id,
                                           void *event_data,
                                           size_t event_data_size)
{
    qm_work_delayed_action_payload_t *payload = (qm_work_delayed_action_payload_t *)event_data;
    qm_work_t *work;
    work_fn_t fn;
    void *arg;
    int32_t ret;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_id);
    QM_UNUSED_ARG(event_data_size);

    if((payload == NULL) || (payload->work == NULL)){
        return;
    }

    work = payload->work;
    ret = qm_work_delayed_action_lock();
    if(ret != QM_EOK){
        return;
    }
    if(!work->active || (work->seq != payload->seq) || (work->fn == NULL)){
        (void)qm_work_delayed_action_unlock();
        return;
    }

    fn = work->fn;
    arg = work->arg;
    work->active = 0;
    work->dly = 0;
    qm_work_delayed_action_release(work);
    ret = qm_work_delayed_action_unlock();
    if(ret != QM_EOK){
        return;
    }

    fn(arg);
}

/**
 * Timer callback that posts a delayed action to the default event loop.
 *
 * @param[in] timer Timer that expired; unused because the work handle is passed as arg.
 * @param[in] arg Delayed-action handle associated with the timer.
 */
static void qm_work_delayed_action_timer_cb(qm_timer_t *timer, void *arg)
{
    qm_work_t *work = (qm_work_t *)arg;
    uint32_t seq;
    int32_t ret;

    QM_UNUSED_ARG(timer);

    if(work == NULL){
        return;
    }

    ret = qm_work_delayed_action_lock();
    if(ret != QM_EOK){
        return;
    }
    if(!work->active){
        (void)qm_work_delayed_action_unlock();
        return;
    }
    seq = work->seq;
    (void)qm_work_delayed_action_unlock();

    ret = qm_work_post_delayed_action_event(work, seq);
    if(ret == QM_EOK){
        if(qm_work_delayed_action_lock() == QM_EOK){
            if(work->active && (work->seq == seq)){
                qm_work_delayed_action_release(work);
            }
            (void)qm_work_delayed_action_unlock();
        }
        return;
    }

    if(ret != QM_EOK){
        if(qm_work_delayed_action_lock() != QM_EOK){
            return;
        }
        if(work->active && (work->seq == seq)){
            work->active = 0;
            work->dly = 0;
            qm_work_delayed_action_release(work);
        }
        (void)qm_work_delayed_action_unlock();
    }
}

int32_t qm_work_on_init(void)
{
    int32_t ret;

    if(g_work_inited){
        return QM_EOK;
    }

#if CONFIG_QM_OS_SUPPORT
    if(!g_work_lock_inited){
        ret = qm_mutex_new(&g_work_lock);
        if(ret != QM_EOK){
            return ret;
        }
        g_work_lock_inited = 1;
    }
#endif

    ret = qm_work_default_loop_ready();
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_event_handler_register(QM_WORK_DELAYED_ACTION,
                                         QM_WORK_DELAYED_ACTION_ID,
                                         qm_work_delayed_action_handler,
                                         NULL);
    if(ret != QM_EOK){
        return ret;
    }

    g_work_inited = 1;
    return QM_EOK;
}

int32_t qm_post_delayed_action(qm_work_t *work, void (*fn)(void *), void *arg, uint32_t ms)
{
    uint32_t seq;
    int32_t ret;

    if((work == NULL) || (fn == NULL)){
        return -QM_EINVAL;
    }

    ret = qm_work_on_init();
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_work_delayed_action_lock();
    if(ret != QM_EOK){
        return ret;
    }
    if(work->magic == QM_WORK_DELAYED_ACTION_MAGIC){
        work->active = 0;
        qm_work_delayed_action_release(work);
        work->seq++;
        if(work->seq == 0){
            work->seq = 1;
        }
    }else{
        memset(&work->timer, 0, sizeof(work->timer));
    }

    work->fn = fn;
    work->arg = arg;
    work->dly = ms;
    work->magic = QM_WORK_DELAYED_ACTION_MAGIC;
    work->seq = qm_work_delayed_action_next_seq();
    seq = work->seq;
    work->active = 1;

    if(ms == 0){
        ret = qm_work_delayed_action_unlock();
        if(ret != QM_EOK){
            return ret;
        }
        ret = qm_work_post_delayed_action_event(work, seq);
        if(ret != QM_EOK){
            if(qm_work_delayed_action_lock() == QM_EOK){
                if(work->seq == seq){
                    work->active = 0;
                    qm_work_delayed_action_release(work);
                }
                (void)qm_work_delayed_action_unlock();
            }
        }
        return ret;
    }

#if defined(CONFIG_QM_OS_TIMER_SUPPORT) && CONFIG_QM_OS_TIMER_SUPPORT
    ret = qm_timer_new(&work->timer, qm_work_delayed_action_timer_cb, work, (int)ms, 0);
    if(ret != QM_EOK){
        work->active = 0;
        work->dly = 0;
        (void)qm_work_delayed_action_unlock();
        return ret;
    }

    ret = qm_work_delayed_action_unlock();
    if(ret != QM_EOK){
        return ret;
    }

    ret = qm_timer_start(&work->timer);
    if(ret != QM_EOK){
        if(qm_work_delayed_action_lock() == QM_EOK){
            if(work->seq == seq){
                work->active = 0;
                work->dly = 0;
                qm_work_delayed_action_release(work);
            }
            (void)qm_work_delayed_action_unlock();
        }
        return ret;
    }

    return QM_EOK;
#else
    work->active = 0;
    work->dly = 0;
    (void)qm_work_delayed_action_unlock();
    return -QM_ENOSYS;
#endif
}

int32_t qm_cancel_delayed_action(qm_work_t *work)
{
    int32_t ret;

    if(work == NULL){
        return -QM_EINVAL;
    }

    if(work->magic != QM_WORK_DELAYED_ACTION_MAGIC){
        return QM_EOK;
    }

    ret = qm_work_delayed_action_lock();
    if(ret != QM_EOK){
        return ret;
    }
    work->active = 0;
    work->dly = 0;
    work->seq++;
    if(work->seq == 0){
        work->seq = 1;
    }
    qm_work_delayed_action_release(work);
    ret = qm_work_delayed_action_unlock();
    if(ret != QM_EOK){
        return ret;
    }

    return QM_EOK;
}

#if CONFIG_QM_WORKQUEUE_NO_TASK
void qm_workqueue_process(void)
{
    int32_t ret;

    do{
        ret = qm_event_default_run(QM_NO_WAIT);
    }while(ret == QM_EOK);
}
#endif

#endif
