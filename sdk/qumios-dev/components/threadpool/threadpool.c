#include "threadpool.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_config.h"

#ifndef CONFIG_THREADPOOL_TASK_SIZE
#define CONFIG_THREADPOOL_TASK_SIZE      (4096)
#endif

#ifndef CONFIG_THREADPOOL_TASK_PRIO
#define CONFIG_THREADPOOL_TASK_PRIO      (20)
#endif


typedef enum {
    THREADPOOL_SHUTDOWN_IMMEDIATE = 1,
    THREADPOOL_SHUTDOWN_GRACEFUL = 2
} threadpool_shutdown_t;

/**
 *  @struct threadpool_task
 *  @brief the work struct
 *
 *  @var function Pointer to the function that will perform the task.
 *  @var argument Argument to be passed to the function.
 */
typedef struct {
	void (*function)(void *);
	void *arg;
}threadpool_task_t;

typedef struct 
{
    void *pool;
    qm_task_t *task;
}threadpool_task_arg_t;

/**
 *  @struct threadpool
 *  @brief The threadpool struct
 *
 *  @var notify       Condition variable to notify worker threads.
 *  @var threads      Array containing worker threads ID.
 *  @var thread_count Number of threads
 *  @var queue        Array containing the task queue.
 *  @var queue_size   Size of the task queue.
 *  @var head         Index of the first element.
 *  @var tail         Index of the next element.
 *  @var count        Number of pending tasks
 *  @var shutdown     Flag indicating if the pool is shutting down
 *  @var started      Number of started threads
 */

typedef struct {
    qm_mutex_t lock;
    qm_sem_t notify;
    qm_task_t *task;
    threadpool_task_t *queue;
    threadpool_task_arg_t *task_arg;
    int thread_count;
    int queue_size;
    int head;
    int tail;
    int count;
    int shutdown;
    int started;
}threadpool_t;



static int threadpool_free(threadpool_t *pool);
static void threadpool_task(void *arg);

void *threadpool_create(int thread_count, int queue_size)
{
    qm_err_t ret = QM_EOK;
    threadpool_t *pool = NULL;
    int i = 0;

    if(thread_count <= 0  || queue_size <= 0) {
        return NULL;
    }

    if((pool = (threadpool_t *)qm_malloc(sizeof(threadpool_t))) == NULL) {
        return NULL;
    }

    memset(pool, 0, sizeof(threadpool_t));

    /* Initialize */
    pool->thread_count = 0;
    pool->queue_size = queue_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = pool->started = 0;

    /* Allocate thread and task queue */
    pool->task = (qm_task_t *)qm_malloc(sizeof(qm_task_t) * thread_count);
    if(pool->task == NULL){
        goto __exit;
    }
    pool->queue = (threadpool_task_t *)qm_malloc(sizeof(threadpool_task_t) * queue_size);
    if(pool->queue == NULL){
        goto __exit;
    }
    pool->task_arg = (threadpool_task_arg_t*)qm_malloc(sizeof(threadpool_task_arg_t) * thread_count);
    if(pool->task_arg == NULL){
        goto __exit;
    }

    ret = qm_mutex_new(&pool->lock);
    if(ret != QM_EOK){
        goto __exit; 
    }

    ret = qm_sem_new(&pool->notify, 0);
    if(ret != QM_EOK){
        goto __exit; 
    }

    /* Start worker threads */
    for(i = 0; i < thread_count; i++) {

        pool->task_arg[i].task = &pool->task[i];
        pool->task_arg[i].pool = (void*)pool;

        qm_task_new(&(pool->task[i]), "threadpool", threadpool_task, (void*)&pool->task_arg[i], CONFIG_THREADPOOL_TASK_SIZE, CONFIG_THREADPOOL_TASK_PRIO);

        pool->thread_count++;
        pool->started++;
    }

    return (void*)pool;

 __exit:
    if(pool->task){
        qm_free(pool->task);
        pool->task = NULL;
    }
    if(pool->queue){
        qm_free(pool->queue);
        pool->queue = NULL;
    }
    if(pool->task_arg){
        qm_free(pool->task_arg);
        pool->task_arg = NULL;
    }

    if(qm_mutex_is_valid(&pool->lock)){
        qm_mutex_free(&pool->lock);
    }

    if(qm_sem_is_valid(&pool->notify)){
        qm_sem_free(&pool->notify);
    }

    if(pool) {
        qm_free(pool);
        pool = NULL;
    }

    return NULL;
}

int threadpool_add(void *threadpool, void (*function)(void *), void *arg)
{
    int next = 0;
    int ret = 0;
    int need_notify = 0;

    threadpool_t *pool = (threadpool_t*)threadpool;

    if(pool == NULL || function == NULL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&pool->lock, QM_WAIT_FOREVER);

    next = (pool->tail + 1) % pool->queue_size;

    do {
        /* Are we full ? */
        if(pool->count == pool->queue_size) {
            ret = -QM_EFULL;
            break;
        }

        /* Are we shutting down ? */
        if(pool->shutdown) {
            break;
        }

        /* Add task to queue */
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].arg = arg;
        pool->tail = next;
        pool->count += 1;

        need_notify = 1;

    } while(0);

    qm_mutex_unlock(&pool->lock);

    if(need_notify){
        qm_sem_signal(&pool->notify);
    }

    return ret;
}

static void threadpool_task(void *arg)
{
    int ret = 0;
    int wait = 0;
    threadpool_task_t task = {0};
    threadpool_task_arg_t *task_arg = (threadpool_task_arg_t*)arg;
    threadpool_t *pool = (threadpool_t*)task_arg->pool;
    for(;;) {

        qm_mutex_lock(&pool->lock, QM_WAIT_FOREVER);
        if((pool->count == 0) && (!pool->shutdown)){
            wait = 1;
        }
        qm_mutex_unlock(&pool->lock);

        if(wait){
            ret = qm_sem_wait(&pool->notify, QM_WAIT_FOREVER);
            if(ret != QM_EOK){
                continue;
            }
        }

        qm_mutex_lock(&pool->lock, QM_WAIT_FOREVER);

        if((pool->shutdown == THREADPOOL_SHUTDOWN_IMMEDIATE) ||
           ((pool->shutdown == THREADPOOL_SHUTDOWN_GRACEFUL) &&
            (pool->count == 0))) {
            break;
        }
        /* Grab our task */
        task.function = pool->queue[pool->head].function;
        task.arg = pool->queue[pool->head].arg;
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count -= 1;

        /* Unlock */
        qm_mutex_unlock(&pool->lock);

        /* Get to work */
        (*(task.function))(task.arg);
    }

    pool->started--;

    qm_mutex_unlock(&pool->lock);
    qm_task_exit(task_arg->task);
}

static int threadpool_free(threadpool_t *pool)
{
    if(pool == NULL || pool->started > 0) {
        return -QM_EINVAL;
    }

    qm_free(pool->task);
    pool->task = NULL;
    qm_free(pool->queue);
    pool->queue = NULL;
    qm_free(pool->task_arg);
    pool->task_arg = NULL;

    qm_mutex_free(&(pool->lock));
    qm_sem_free(&(pool->notify));
    
    qm_free(pool); 
    pool = NULL;   
    return QM_EOK;
}

int threadpool_destroy(void *threadpool, int flags)
{
    int i = 0;
    int started = 0;
    threadpool_t *pool = (threadpool_t*)threadpool;

    if(pool == NULL) {
        return -QM_EINVAL;
    }

    do {

        qm_mutex_lock(&(pool->lock), QM_WAIT_FOREVER);
        /* Already shutting down */
        if(pool->shutdown) {
            break;
        }

        pool->shutdown = (flags & THREADPOOL_DESTORY_GRACEFUL) ?
            THREADPOOL_SHUTDOWN_GRACEFUL : THREADPOOL_SHUTDOWN_IMMEDIATE;

        qm_mutex_unlock(&(pool->lock));

        /* Wake up all worker threads */
        for(i = 0; i < pool->thread_count; i++) {
            qm_sem_signal(&pool->notify);
        }

        /*Wait all work tasks to exit*/
        while(1){
            qm_mutex_lock(&(pool->lock), QM_WAIT_FOREVER);
            started = pool->started;
            qm_mutex_unlock(&(pool->lock));

            if(started == 0){
                break;
            }else{
                qm_msleep(100);
            }  
        }

    } while(0);

    threadpool_free(pool);
    
    return QM_EOK;
}