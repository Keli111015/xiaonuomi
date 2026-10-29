#include "multi_timer.h"
#include "qm.h"

#define LOG_TAG "multi_timer"

#if CONFIG_MULTI_TIMER_SUPPORT

typedef struct {
    /* timer handle list head. */
    multi_timer_t *timer_list;
     /* overflow timer handle list head. */
    multi_timer_t *overflow_timer_list;
    #if CONFIG_QM_OS_SUPPORT
    qm_mutex_t lock;    
    #endif
}multi_timer_ctx_t;

static multi_timer_ctx_t g_multi_timer_ctx = {0};

int multi_timer_init(void)
{
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_new(&g_multi_timer_ctx.lock);   
#endif
    return QM_EOK;
}

static int timer_list_insert(multi_timer_t **head_list, multi_timer_t *timer)
{
    multi_timer_t **next_timer = head_list;

    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    QM_RETURN_ON_FALSE(head_list, -QM_EINVAL, LOG_TAG, "head_list NULL");

    /* Remove the existing target timer. */
    for (; *next_timer; next_timer = &(*next_timer)->next) {
        if (timer == *next_timer) {
            *next_timer = timer->next; /* remove from list */
            break;
        }
    }

    /* Insert timer. */
    for (next_timer = head_list;; next_timer = &(*next_timer)->next) {
        if (!*next_timer) {
            timer->next = NULL;
            *next_timer = timer;
            break;
        }
        if (timer->delay < (*next_timer)->delay) {
            timer->next = *next_timer;
            *next_timer = timer;
            break;
        }
    }
    timer->list = (void**)head_list;
    return QM_EOK;
}

static int timer_insert(multi_timer_t *timer, uint32_t now_time)
{
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");

    timer->delay = timer->period + now_time;
    if(timer->delay < now_time){
        timer_list_insert(&g_multi_timer_ctx.overflow_timer_list, timer);
    }else{
        timer_list_insert(&g_multi_timer_ctx.timer_list, timer);
    }
    return QM_EOK;
}

static int timer_list_switch(void)
{   
    g_multi_timer_ctx.timer_list = g_multi_timer_ctx.overflow_timer_list;
    g_multi_timer_ctx.overflow_timer_list = NULL;
    return QM_EOK;
}

static int timer_list_remove(multi_timer_t **head_list, multi_timer_t *timer)
{
    multi_timer_t *m_timer = NULL;
    multi_timer_t **next_timer = head_list;

    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    QM_RETURN_ON_FALSE(head_list, -QM_EINVAL, LOG_TAG, "head_list NULL");

    /* Find and remove timer. */
    for (; *next_timer; next_timer = &(*next_timer)->next) {
        m_timer = *next_timer;
        if (m_timer == timer) {
            *next_timer = timer->next;
            break;
        }
    }
    return QM_EOK;
}

static uint32_t next_expire_time_get(void)
{
    if(g_multi_timer_ctx.timer_list){
        return g_multi_timer_ctx.timer_list->delay;
    }

    if(g_multi_timer_ctx.overflow_timer_list){
        return g_multi_timer_ctx.overflow_timer_list->delay;
    }
    return 0;
}

int multi_timer_creat(multi_timer_t *timer, multi_timer_cb_t callback, void *arg, uint32_t ms, int auto_reload)
{
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");

    memset(timer, 0, sizeof(multi_timer_t));
    timer->arg = arg;
    timer->period = ms;
    timer->callback = callback;
    timer->auto_reload = auto_reload;
    return QM_EOK;
}

int multi_timer_delete(multi_timer_t *timer)
{
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    
    if(timer->list == NULL){
        return QM_EOK;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_multi_timer_ctx.lock, QM_WAIT_FOREVER);
#endif
    timer_list_remove((multi_timer_t**)timer->list, timer);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_multi_timer_ctx.lock);
#endif

    timer->list = NULL;
    
    return QM_EOK;
}

int multi_timer_start(multi_timer_t *timer)
{
    uint32_t now_time = 0;
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");

    now_time = qm_now_ms();
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_multi_timer_ctx.lock, QM_WAIT_FOREVER);
#endif
    timer_insert(timer, now_time);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_multi_timer_ctx.lock);
#endif
    return QM_EOK;
}

int multi_timer_stop(multi_timer_t *timer)
{
    return multi_timer_delete(timer);
}

int multi_timer_change(multi_timer_t *timer, uint32_t ms)
{
    multi_timer_t m_timer = {0};
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    memcpy(&m_timer, timer, sizeof(multi_timer_t));

    multi_timer_delete(timer);
    multi_timer_creat(timer, m_timer.callback, m_timer.arg, ms, m_timer.auto_reload);
    multi_timer_start(timer);
    return QM_EOK;
}

static int overflow_timer_process(uint32_t now_time)
{   
    multi_timer_t *timer = g_multi_timer_ctx.timer_list;
    while(timer){
        g_multi_timer_ctx.timer_list = timer->next;
        if(timer->auto_reload){
            timer_insert(timer, now_time);
        }
        if(timer->callback){
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_unlock(&g_multi_timer_ctx.lock);
#endif
            timer->callback(timer, timer->arg);
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_lock(&g_multi_timer_ctx.lock, QM_WAIT_FOREVER);
#endif
        }
        timer = g_multi_timer_ctx.timer_list;
    }
    return QM_EOK;
}

static int timer_process(uint32_t now_time)
{   
    multi_timer_t *timer = g_multi_timer_ctx.timer_list;
    while(timer){
        if(now_time < timer->delay){
            break;
        }
        g_multi_timer_ctx.timer_list = timer->next;
        if(timer->auto_reload){
            timer_insert(timer, now_time);
        }
        if(timer->callback){
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_unlock(&g_multi_timer_ctx.lock);
#endif
            timer->callback(timer, timer->arg);
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_lock(&g_multi_timer_ctx.lock, QM_WAIT_FOREVER);
#endif
        }
        timer = g_multi_timer_ctx.timer_list;
    }
    return QM_EOK;
}

uint32_t multi_timer_yield(void)
{
    int overflow = 0;
    uint32_t now_time = 0;
    uint32_t next_period = 0;
    static uint32_t last_time = 0;
    now_time = qm_now_ms();

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_multi_timer_ctx.lock, QM_WAIT_FOREVER);
#endif

    if(now_time < last_time){
        overflow = 1;
        overflow_timer_process(now_time);
        timer_list_switch();
    }
    timer_process(now_time);

    next_period = next_expire_time_get();

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_multi_timer_ctx.lock);
#endif

    last_time = qm_now_ms();
    if(next_period > now_time){
        return next_period - now_time;
    }else{
        if(overflow){
            return next_period - now_time; 
        }
    }
    return 0;
}   

#endif
