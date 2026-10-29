#include "qm.h"
#include "qm_event.h"

#if CONFIG_QM_WORK_SUPPORT

#if CONFIG_QM_OS_SUPPORT
#define QM_EVENT_LOCK(loop)      qm_mutex_lock(&(loop)->lock, QM_WAIT_FOREVER)
#define QM_EVENT_UNLOCK(loop)    qm_mutex_unlock(&(loop)->lock)
#else
#define QM_EVENT_LOCK(loop)      QM_UNUSED_ARG(loop)
#define QM_EVENT_UNLOCK(loop)    QM_UNUSED_ARG(loop)
#endif

/**
 * Default event loop handle used by default-loop APIs.
 */
static qm_event_handle_t g_default_loop = NULL;

/**
 * Handler registration node stored in one event loop.
 */
typedef struct {
    /**
     * Whether this registration slot is occupied.
     */
    uint8_t used;
    /**
     * Event base matched by this handler.
     */
    qm_event_base_t event_base;
    /**
     * Event id matched by this handler.
     */
    int32_t event_id;
    /**
     * Callback invoked when a posted event matches event_base and event_id.
     */
    qm_event_handler_t handler;
    /**
     * User argument passed to handler.
     */
    void *event_handler_arg;
} qm_event_handler_node_t;

/**
 * Internal event object stored in the loop queue.
 */
typedef struct {
    /**
     * Event base that identifies the event source.
     */
    qm_event_base_t event_base;
    /**
     * Event id that identifies the event within event_base.
     */
    int32_t event_id;
    /**
     * Event payload passed to handlers.
     */
    void *event_data;
    /**
     * Event payload size in bytes.
     */
    size_t event_data_size;
    /**
     * Whether event_data was allocated by this module and must be freed after dispatch.
     */
    uint8_t data_allocated;
} qm_event_post_to_t;

/**
 * Runtime state of one qm event loop.
 */
struct qm_event {
    /**
     * Queue carrying posted events for this loop.
     */
    qm_queue_t queue;
    /**
     * Optional task that runs the event loop automatically.
     */
    qm_task_t task;
    /**
     * Lock protecting handler registrations and delete state.
     */
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t lock;
#endif
    /**
     * Static handler slots for this event loop.
     */
    qm_event_handler_node_t handlers[CONFIG_QM_EVENT_NUM];
    /**
     * Whether qm_event_create created a task for this loop.
     */
    uint8_t task_created;
    /**
     * Whether qm_event_delete has been called.
     */
    uint8_t deleting;
};

static int32_t qm_event_post_to_set(qm_event_post_to_t *post, qm_event_base_t event_base, int32_t event_id,
                                      const void *event_data, size_t event_data_size)
{
    post->event_base = event_base;
    post->event_id = event_id;
    post->event_data = NULL;
    post->event_data_size = event_data_size;
    post->data_allocated = 0;

    if(event_data){
        if(event_data_size){
            post->event_data = qm_malloc(event_data_size);
            if(post->event_data == NULL){
                return -QM_ENOMEM;
            }
            memcpy(post->event_data, event_data, event_data_size);
            post->data_allocated = 1;
        }else{
            post->event_data = (void *)event_data;
        }
    }

    return QM_EOK;
}

static void qm_event_post_to_clear(qm_event_post_to_t *post)
{
    if(post->data_allocated && post->event_data){
        qm_free(post->event_data);
    }
    post->event_data = NULL;
    post->event_data_size = 0;
    post->data_allocated = 0;
}

static bool_t qm_event_handler_match(const qm_event_handler_node_t *node,
                                          const qm_event_post_to_t *post)
{
    if(!node->used){
        return QM_FALSE;
    }

    if(node->event_base != QM_EVENT_ANY_BASE && node->event_base != post->event_base){
        return QM_FALSE;
    }

    if(node->event_id != QM_EVENT_ANY_ID && node->event_id != post->event_id){
        return QM_FALSE;
    }

    return QM_TRUE;
}

static bool_t qm_event_args_valid(qm_event_base_t event_base, int32_t event_id)
{
    if(event_base == QM_EVENT_ANY_BASE && event_id != QM_EVENT_ANY_ID){
        return QM_FALSE;
    }

    return QM_TRUE;
}

static bool_t qm_event_handler_equal(const qm_event_handler_node_t *node,
                                          qm_event_base_t event_base,
                                          int32_t event_id,
                                          qm_event_handler_t event_handler,
                                          void *event_handler_arg)
{
    if(!node->used){
        return QM_FALSE;
    }

    if(node->event_base != event_base){
        return QM_FALSE;
    }

    if(node->event_id != event_id){
        return QM_FALSE;
    }

    if(node->handler != event_handler){
        return QM_FALSE;
    }

    if(node->event_handler_arg != event_handler_arg){
        return QM_FALSE;
    }

    return QM_TRUE;
}

static qm_event_handler_node_t *qm_event_handler_find(qm_event_handle_t event_loop,
                                                                qm_event_base_t event_base,
                                                                int32_t event_id,
                                                                qm_event_handler_t event_handler,
                                                                void *event_handler_arg)
{
    int i = 0;

    for(i = 0; i < CONFIG_QM_EVENT_NUM; i++){
        if(qm_event_handler_equal(&event_loop->handlers[i], event_base, event_id,
                                       event_handler, event_handler_arg)){
            return &event_loop->handlers[i];
        }
    }

    return NULL;
}

static qm_event_handler_node_t *qm_event_handler_find_free(qm_event_handle_t event_loop)
{
    int i = 0;

    for(i = 0; i < CONFIG_QM_EVENT_NUM; i++){
        if(!event_loop->handlers[i].used){
            return &event_loop->handlers[i];
        }
    }

    return NULL;
}

static void qm_event_dispatch(qm_event_handle_t event_loop, qm_event_post_to_t *post)
{
    int i = 0;

    QM_EVENT_LOCK(event_loop);
    for(i = 0; i < CONFIG_QM_EVENT_NUM; i++){
        if(qm_event_handler_match(&event_loop->handlers[i], post)){
            qm_event_handler_t handler = event_loop->handlers[i].handler;
            void *event_handler_arg = event_loop->handlers[i].event_handler_arg;

            QM_EVENT_UNLOCK(event_loop);
            handler(event_handler_arg, post->event_base, post->event_id,
                    post->event_data, post->event_data_size);
            QM_EVENT_LOCK(event_loop);

            if(event_loop->deleting){
                break;
            }
        }
    }
    QM_EVENT_UNLOCK(event_loop);

    qm_event_post_to_clear(post);
}

#if CONFIG_QM_OS_TASK_SUPPORT
static void qm_event_task(void *arg)
{
    qm_event_handle_t event_loop = (qm_event_handle_t)arg;

    while(!event_loop->deleting){
        qm_event_run(event_loop, QM_WAIT_FOREVER);
    }
}
#endif

int32_t qm_event_create(const qm_event_args_t *event_loop_args, qm_event_handle_t *event_loop)
{
    int32_t ret = QM_EOK;
    uint32_t queue_size = CONFIG_QM_WORKQUEUE_NUM;
    qm_event_handle_t loop = NULL;

    if(event_loop_args == NULL || event_loop == NULL){
        return -QM_EINVAL;
    }

    if(event_loop_args->queue_size){
        queue_size = event_loop_args->queue_size;
    }

    loop = (qm_event_handle_t)qm_malloc(sizeof(struct qm_event));
    if(loop == NULL){
        return -QM_ENOMEM;
    }
    memset(loop, 0, sizeof(struct qm_event));

    ret = qm_queue_new(&loop->queue, queue_size, sizeof(qm_event_post_to_t));
    if(ret != QM_EOK){
        qm_free(loop);
        return ret;
    }

#if CONFIG_QM_OS_SUPPORT
    ret = qm_mutex_new(&loop->lock);
    if(ret != QM_EOK){
        qm_queue_free(&loop->queue);
        qm_free(loop);
        return ret;
    }
#endif

    if(event_loop_args->task_name){
#if CONFIG_QM_OS_TASK_SUPPORT
        if(event_loop_args->task_stack_size <= 0){
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_free(&loop->lock);
#endif
            qm_queue_free(&loop->queue);
            qm_free(loop);
            return -QM_EINVAL;
        }

        ret = qm_task_new(&loop->task, event_loop_args->task_name, qm_event_task, loop,
                          event_loop_args->task_stack_size, event_loop_args->task_priority);
        if(ret != QM_EOK){
#if CONFIG_QM_OS_SUPPORT
            qm_mutex_free(&loop->lock);
#endif
            qm_queue_free(&loop->queue);
            qm_free(loop);
            return ret;
        }
        loop->task_created = 1;
#else
        qm_queue_free(&loop->queue);
        qm_free(loop);
        return -QM_EINVAL;
#endif
    }

    *event_loop = loop;

    return QM_EOK;
}

int32_t qm_event_default_create(void)
{
    qm_event_args_t loop_args = {0};

    if(g_default_loop){
        return -QM_EINIT;
    }

    loop_args.queue_size = CONFIG_QM_WORKQUEUE_NUM;
#if !CONFIG_QM_WORKQUEUE_NO_TASK && CONFIG_QM_OS_TASK_SUPPORT
    loop_args.task_name = CONFIG_QM_WORKQUEUE_TASK_NAME;
    loop_args.task_priority = CONFIG_QM_WORKQUEUE_TASK_PRIO;
    loop_args.task_stack_size = CONFIG_QM_WORKQUEUE_TASK_SIZE;
#else
    loop_args.task_name = NULL;
    loop_args.task_priority = 0;
    loop_args.task_stack_size = 0;
#endif

    return qm_event_create(&loop_args, &g_default_loop);
}

int32_t qm_event_default_run(uint32_t ticks_to_run)
{
    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    return qm_event_run(g_default_loop, ticks_to_run);
}

int32_t qm_event_delete(qm_event_handle_t event_loop)
{
    unsigned int size = 0;
    qm_event_post_to_t post = {0};

    if(event_loop == NULL){
        return -QM_EINVAL;
    }

    QM_EVENT_LOCK(event_loop);
    if(event_loop->task_created){
        QM_EVENT_UNLOCK(event_loop);
        return -QM_EINVAL;
    }

    event_loop->deleting = 1;
    QM_EVENT_UNLOCK(event_loop);

    while(!qm_queue_is_empty(&event_loop->queue)){
        if(qm_queue_recv(&event_loop->queue, &post, &size, QM_NO_WAIT) == QM_EOK){
            qm_event_post_to_clear(&post);
        }
    }

    qm_queue_free(&event_loop->queue);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_free(&event_loop->lock);
#endif
    qm_free(event_loop);

    return QM_EOK;
}

int32_t qm_event_default_delete(void)
{
    int32_t ret = QM_EOK;

    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    ret = qm_event_delete(g_default_loop);
    if(ret != QM_EOK){
        return ret;
    }

    g_default_loop = NULL;

    return QM_EOK;
}

int32_t qm_event_run(qm_event_handle_t event_loop, uint32_t ticks_to_run)
{
    int32_t ret = QM_EOK;
    unsigned int size = 0;
    qm_event_post_to_t post = {0};

    if(event_loop == NULL){
        return -QM_EINVAL;
    }

    if(event_loop->deleting){
        return -QM_EINVAL;
    }

    ret = qm_queue_recv(&event_loop->queue, &post, &size, ticks_to_run);
    if(ret != QM_EOK){
        return ret;
    }

    qm_event_dispatch(event_loop, &post);

    return QM_EOK;
}

int32_t qm_event_handler_register(qm_event_base_t event_base, int32_t event_id,
                                       qm_event_handler_t event_handler, void *event_handler_arg)
{
    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    return qm_event_handler_register_with(g_default_loop, event_base, event_id,
                                               event_handler, event_handler_arg);
}

int32_t qm_event_handler_register_with(qm_event_handle_t event_loop, qm_event_base_t event_base,
                                       int32_t event_id, qm_event_handler_t event_handler,
                                       void *event_handler_arg)
{
    int32_t ret = -QM_EFULL;
    qm_event_handler_node_t *exist_node = NULL;
    qm_event_handler_node_t *free_node = NULL;

    if(event_loop == NULL || event_handler == NULL || !qm_event_args_valid(event_base, event_id)){
        return -QM_EINVAL;
    }

    QM_EVENT_LOCK(event_loop);
    if(event_loop->deleting){
        ret = -QM_EINVAL;
    }else{
        exist_node = qm_event_handler_find(event_loop, event_base, event_id,
                                                event_handler, event_handler_arg);
        if(exist_node){
            ret = QM_EOK;
        }else{
            free_node = qm_event_handler_find_free(event_loop);
            if(free_node){
                free_node->used = 1;
                free_node->event_base = event_base;
                free_node->event_id = event_id;
                free_node->handler = event_handler;
                free_node->event_handler_arg = event_handler_arg;
                ret = QM_EOK;
            }
        }
    }
    QM_EVENT_UNLOCK(event_loop);

    return ret;
}

int32_t qm_event_handler_unregister(qm_event_base_t event_base, int32_t event_id,
                                         qm_event_handler_t event_handler, void *event_handler_arg)
{
    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    return qm_event_handler_unregister_with(g_default_loop, event_base, event_id,
                                                 event_handler, event_handler_arg);
}

int32_t qm_event_handler_unregister_with(qm_event_handle_t event_loop, qm_event_base_t event_base,
                                         int32_t event_id, qm_event_handler_t event_handler,
                                         void *event_handler_arg)
{
    qm_event_handler_node_t *exist_node = NULL;

    if(event_loop == NULL || event_handler == NULL || !qm_event_args_valid(event_base, event_id)){
        return -QM_EINVAL;
    }

    QM_EVENT_LOCK(event_loop);
    exist_node = qm_event_handler_find(event_loop, event_base, event_id,
                                            event_handler, event_handler_arg);
    if(exist_node){
        memset(exist_node, 0, sizeof(qm_event_handler_node_t));
    }
    QM_EVENT_UNLOCK(event_loop);

    return QM_EOK;
}

static int32_t qm_event_post_generic(qm_event_handle_t event_loop, qm_event_base_t event_base,
                                     int32_t event_id, const void *event_data, size_t event_data_size,
                                     uint32_t ticks_to_wait, uint8_t from_isr)
{
    int32_t ret = QM_EOK;
    qm_event_post_to_t post = {0};

    if(event_loop == NULL || !qm_event_args_valid(event_base, event_id)){
        return -QM_EINVAL;
    }

    if(event_loop->deleting){
        return -QM_EINVAL;
    }

    ret = qm_event_post_to_set(&post, event_base, event_id, event_data, event_data_size);
    if(ret != QM_EOK){
        return ret;
    }

    if(from_isr){
        ret = qm_queue_send_from_isr(&event_loop->queue, &post, sizeof(qm_event_post_to_t));
    }else{
        ret = qm_queue_send(&event_loop->queue, &post, sizeof(qm_event_post_to_t));
        QM_UNUSED_ARG(ticks_to_wait);
    }

    if(ret != QM_EOK){
        qm_event_post_to_clear(&post);
    }

    return ret;
}

int32_t qm_event_post(qm_event_base_t event_base, int32_t event_id,
                           const void *event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    return qm_event_post_to(g_default_loop, event_base, event_id,
                                 event_data, event_data_size, ticks_to_wait);
}

int32_t qm_event_post_to(qm_event_handle_t event_loop, qm_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    return qm_event_post_generic(event_loop, event_base, event_id, event_data, event_data_size, ticks_to_wait, 0);
}

int32_t qm_event_isr_post(qm_event_base_t event_base, int32_t event_id,
                               const void *event_data, size_t event_data_size)
{
    if(g_default_loop == NULL){
        return -QM_ENOENT;
    }

    return qm_event_isr_post_to(g_default_loop, event_base, event_id,
                                     event_data, event_data_size);
}

int32_t qm_event_isr_post_to(qm_event_handle_t event_loop, qm_event_base_t event_base, int32_t event_id,
                             const void *event_data, size_t event_data_size)
{
    return qm_event_post_generic(event_loop, event_base, event_id, event_data, event_data_size, QM_NO_WAIT, 1);
}

#endif
