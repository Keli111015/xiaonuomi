#include "qm.h"
#include "qm_noos.h"
#include "multi_timer.h"

#if CONFIG_QM_NOOS_SUPPORT

#define LOG_TAG "qm noos adapter"

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio)
{
    return qm_noos_task_new(&task->hdl, name, fn, arg, prio);
}

int qm_task_exit(qm_task_t *task)
{
    return qm_noos_task_exit(task->hdl);
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    return qm_noos_generic_queue_new(&queue->hdl, QM_NOOS_QUEUE_TYPE_BASE, queue_len, queue_size);
}

int qm_queue_free(qm_queue_t *queue)
{
    return qm_noos_generic_queue_free(queue->hdl);
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    return qm_noos_generic_queue_send(queue->hdl, msg, size);
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return qm_noos_generic_queue_send(queue->hdl, msg, size);
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    return qm_noos_generic_queue_recv(queue->hdl, msg, (uint32_t *)size);
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return qm_noos_generic_queue_is_valid(queue->hdl);
}

bool_t qm_queue_is_empty(qm_queue_t *queue)
{
    return qm_noos_generic_queue_is_empty(queue->hdl);
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    return 0;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    return 0;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    return 0;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    return 0;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return 1;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    return 0;
}

int qm_sem_free(qm_sem_t *sem)
{
    return 0;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    return 0;
}

int qm_sem_signal(qm_sem_t *sem)
{
    return 0;
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    return 0;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return 1;
}

#if CONFIG_MULTI_TIMER_SUPPORT

typedef struct{
    multi_timer_t timer;
    qm_timer_t *qm_timer;
    void *arg;
    qm_timer_cb_t cb;
}qm_timer_handle_t;

static void timer_callback(multi_timer_t* timer, void* arg)
{
    qm_timer_handle_t *timer_handle = (qm_timer_handle_t* )arg;  

    if(timer_handle->cb){
        timer_handle->cb(timer_handle->qm_timer, timer_handle->arg);
    }
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return (timer && timer->hdl != NULL);
}


int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{          
    qm_timer_handle_t *timer_handle = NULL;  
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    QM_RETURN_ON_FALSE(cb, -QM_EINVAL, LOG_TAG, "cb NULL");
    QM_RETURN_ON_FALSE(ms, -QM_EINVAL, LOG_TAG, "ms error");

    timer_handle = (qm_timer_handle_t *)qm_malloc(sizeof(qm_timer_handle_t));
    if(timer_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(timer_handle, 0, sizeof(qm_timer_handle_t));

    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->qm_timer = timer;

    timer->hdl = (void*)timer_handle;

    return multi_timer_creat(&timer_handle->timer, timer_callback, (void *)timer_handle, (uint32_t)ms, repeat);
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = QM_EOK;
    qm_timer_handle_t *timer_handle = NULL;  

    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    
    if(timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;
    
    multi_timer_delete(&timer_handle->timer);

    qm_free(timer->hdl);
    timer->hdl = NULL;

    return QM_EOK;
}

int qm_timer_start(qm_timer_t *timer)
{
    qm_timer_handle_t *timer_handle = NULL;  

    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");

    if(timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;
    return multi_timer_start(&timer_handle->timer);
}

int qm_timer_stop(qm_timer_t *timer)
{
    qm_timer_handle_t *timer_handle = NULL;  
    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");

    if(timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;
    return multi_timer_stop(&timer_handle->timer);
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    qm_timer_handle_t *timer_handle = NULL;  

    QM_RETURN_ON_FALSE(timer, -QM_EINVAL, LOG_TAG, "timer NULL");
    QM_RETURN_ON_FALSE(timer->hdl, -QM_EINVAL, LOG_TAG, "timer->hdl NULL");

    timer_handle = (qm_timer_handle_t *)timer->hdl;
    return multi_timer_change(&timer_handle->timer, (uint32_t)ms);
}
#endif

#endif
