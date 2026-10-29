#include "qm.h"
#include "qm_noos.h"
#include "qm_ringbuf.h"
#include "multi_timer.h"

#define LOG_TAG "noos queue"

#ifndef CONFIG_QM_NOOS_TASK_MIN_PRIO
#define CONFIG_QM_NOOS_TASK_MIN_PRIO    0
#endif

#ifndef CONFIG_QM_NOOS_TASK_MAX_PRIO
#define CONFIG_QM_NOOS_TASK_MAX_PRIO    30
#endif

#ifndef CONFIG_QM_NOOS_TASK_NUM
#define CONFIG_QM_NOOS_TASK_NUM      5
#endif

#ifndef CONFIG_QM_NOOS_QUEUE_NUM
#define CONFIG_QM_NOOS_QUEUE_NUM     5
#endif

typedef struct{
    uint8_t prio;
    void (*fn)(void *arg);
    void *arg;
}qm_noos_task_t;

typedef struct{
    uint8_t type;
    qm_ringbuf_t ringbuf;
    uint32_t queue_size;
    uint32_t queue_len;
    uint8_t *data;
}qm_noos_queue_t;

typedef struct{
    qm_noos_task_t task[CONFIG_QM_NOOS_TASK_NUM];
    qm_noos_queue_t queue[CONFIG_QM_NOOS_QUEUE_NUM];
}qm_noos_ctx_t;

static qm_noos_ctx_t g_qm_noos_ctx = {0};

int qm_noos_generic_queue_new(void **queue, uint8_t type, uint32_t queue_len, uint32_t queue_size)
{
    int i = 0;
    uint32_t size = 0;
    qm_noos_queue_t *noos_queue = NULL;

    QM_RETURN_ON_FALSE(queue, -QM_EINVAL, LOG_TAG, "queue NULL");
    QM_RETURN_ON_FALSE(queue_len, -QM_EINVAL, LOG_TAG, "queue_len error");

    for(i = 0; i < CONFIG_QM_NOOS_QUEUE_NUM; i++){
        noos_queue = &g_qm_noos_ctx.queue[i];

        if(noos_queue->data){
            continue;
        }

        noos_queue->queue_len = queue_len;
        noos_queue->queue_size = queue_size;

        if(queue_size){
            size = queue_len *queue_size;
        }else{
            size = queue_len;
        }

        noos_queue->data = (uint8_t*)qm_malloc(size);
        if(noos_queue->data == NULL){
            return -QM_ENOMEM;
        }
        memset(noos_queue->data, 0, size);

        qm_ringbuf_init(&noos_queue->ringbuf, noos_queue->data, size);

        *queue = (void *)noos_queue;
        return QM_EOK;
    }
    QM_LOGE(LOG_TAG, "queue num is full");
    return -QM_EFULL;
}

int qm_noos_generic_queue_free(void *queue)
{
    qm_noos_queue_t *noos_queue = (qm_noos_queue_t*)queue;

    QM_RETURN_ON_FALSE(queue, -QM_EINVAL, LOG_TAG, "queue NULL");

    if(noos_queue->data){
        qm_free(noos_queue->data);
        noos_queue->data = NULL;
    }
    return QM_EOK;
}

int qm_noos_generic_queue_send(void *queue, void *msg, uint32_t size)
{
    int ret = 0;
    qm_noos_queue_t *noos_queue = (qm_noos_queue_t*)queue;

    QM_RETURN_ON_FALSE(queue, -QM_EINVAL, LOG_TAG, "queue NULL");
    QM_RETURN_ON_FALSE(msg, -QM_EINVAL, LOG_TAG, "msg error");
    QM_RETURN_ON_FALSE(size, -QM_EINVAL, LOG_TAG, "size error");

    if(size != noos_queue->queue_size){
        return -QM_EINVAL;
    }

    if(noos_queue->data == NULL){
        return -QM_EINVAL;
    }
    ret = qm_ringbuf_push(&noos_queue->ringbuf, msg, noos_queue->queue_size);
    if(ret){
        return QM_EOK;
    }else{
        return -QM_EFULL;
    }
}

int qm_noos_generic_queue_recv(void *queue, void *msg, uint32_t *size)
{
    int ret = 0;
    qm_noos_queue_t *noos_queue = (qm_noos_queue_t*)queue;

    QM_RETURN_ON_FALSE(queue, -QM_EINVAL, LOG_TAG, "queue NULL");
    QM_RETURN_ON_FALSE(msg, -QM_EINVAL, LOG_TAG, "msg NULL");
    QM_RETURN_ON_FALSE(size, -QM_EINVAL, LOG_TAG, "size NULL");
    QM_RETURN_ON_FALSE(noos_queue->data, -QM_EINVAL, LOG_TAG, "noos_queue->data NULL");

    ret = qm_ringbuf_pop(&noos_queue->ringbuf, msg, noos_queue->queue_size);
    *size = (uint32_t)ret;
    if(ret){
        return QM_EOK;
    }else{
        return -QM_EEMPTY;
    }
}

bool_t qm_noos_generic_queue_is_empty(void *queue)
{
    qm_noos_queue_t *noos_queue = (qm_noos_queue_t*)queue;
    QM_RETURN_ON_FALSE(queue, 0, LOG_TAG, "queue NULL");
    QM_RETURN_ON_FALSE(noos_queue->data, 0, LOG_TAG, "noos_queue->data NULL");
    return qm_ringbuf_isempty(&noos_queue->ringbuf);
}

int qm_noos_generic_queue_is_valid(void *queue)
{
    qm_noos_queue_t *noos_queue = (qm_noos_queue_t*)queue;
    QM_RETURN_ON_FALSE(queue, 0, LOG_TAG, "queue NULL");
    QM_RETURN_ON_FALSE(noos_queue->data, 0, LOG_TAG, "noos_queue->data NULL");
    return 1;
}

int qm_noos_task_new(void **task, const char *name, void (*fn)(void *), void *arg, int prio)
{
    int i = 0;
    qm_noos_task_t *noos_task = NULL;

    QM_RETURN_ON_FALSE(task, -QM_EINVAL, LOG_TAG, "task NULL");
    QM_RETURN_ON_FALSE(fn, -QM_EINVAL, LOG_TAG, "fn NULL");
    QM_RETURN_ON_FALSE(prio >= CONFIG_QM_NOOS_TASK_MIN_PRIO && prio <= CONFIG_QM_NOOS_TASK_MAX_PRIO, -QM_EINVAL, LOG_TAG, "noos_queue->data NULL");

    for(i = 0; i < CONFIG_QM_NOOS_TASK_NUM; i++){
        noos_task = &g_qm_noos_ctx.task[i];
        if(noos_task->fn){
            continue;
        }
        noos_task->arg = arg;
        noos_task->fn = fn;
        noos_task->prio = prio;
        *task = (void *)noos_task;
        return QM_EOK;
    }
    return -QM_EFULL;
}

int qm_noos_task_exit(void *task)
{
    qm_noos_task_t *noos_task = (qm_noos_task_t*)task;
    QM_RETURN_ON_FALSE(task, -QM_EINVAL, LOG_TAG, "task NULL");

    noos_task->fn = NULL;
    return QM_EOK;
}

int qm_noos_task_yield(void)
{
    int i = 0;
    qm_noos_task_t *noos_task = NULL;
    for(i = 0; i < CONFIG_QM_NOOS_TASK_NUM; i++){
        noos_task = &g_qm_noos_ctx.task[i];
        if(noos_task->fn == NULL){
            continue;
        }
        noos_task->fn(noos_task->arg);
    }
    return QM_EOK;
}

int qm_noos_init(void)
{
#if CONFIG_MULTI_TIMER_SUPPORT
    return multi_timer_init();
#else
    return QM_EOK;
#endif
}

int qm_os_yield(void)
{
    qm_noos_task_yield();
#if CONFIG_MULTI_TIMER_SUPPORT
    multi_timer_yield();
#endif
    return QM_EOK;
}
