#define _GNU_SOURCE

#include "qm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "qm_utils_list.h"
#include "qm_log.h"

#define LOG_TAG "mc3303 os"

static int linux_abstime_from_ms(unsigned int ms, struct timespec *ts)
{
    struct timeval tv;

    if (ts == NULL) {
        return -1;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }

    ts->tv_sec = tv.tv_sec + (time_t)(ms / 1000);
    ts->tv_nsec = (long)tv.tv_usec * 1000L + (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }

    return 0;
}

typedef struct {
    int repeat;
    void *args;               // qm_timer传入参数，用于回调函数
    qm_timer_t *qm_timer;     // 保存qm_timer结构体地址，用于qm_timer的回调函数
    qm_timer_cb_t callback;   // qm_timer的回调函数
    timer_t timerid;
    struct sigevent sev;
    struct itimerspec it;
    struct timespec ts; 
    pthread_mutex_t lock;
} timer_ctx_t;

typedef struct {
    qm_list_t *list;
    pthread_mutex_t lock;
    unsigned int item_size;
} _queue_t;

const char *qm_version_get(void)
{
    return "V1.0.0";
}

void qm_reboot(void)
{
    system("reboot");
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio)
{
    pthread_attr_t attr;
    struct sched_param param;
    pthread_t *pid;

    (void)name;
    (void)stack_size;

    if (task == NULL || fn == NULL) {
        return QM_EINVAL;
    }

    pid = (pthread_t *)qm_malloc(sizeof(pthread_t));
    if (pid == NULL) {
        return QM_ENOMEM;
    }

    memset(&attr, 0, sizeof(attr));
    if (pthread_attr_init(&attr) != 0) {
        qm_free(pid);
        return QM_ERROR;
    }

    memset(&param, 0, sizeof(param));
    param.sched_priority = prio;
    if (pthread_attr_setschedpolicy(&attr, SCHED_RR) == 0 &&
        pthread_attr_setschedparam(&attr, &param) == 0 &&
        pthread_create(pid, &attr, (void *(*)(void *))fn, arg) == 0) {
        pthread_attr_destroy(&attr);
        task->hdl = pid;
        return QM_EOK;
    }

    pthread_attr_destroy(&attr);
    if (pthread_attr_init(&attr) != 0) {
        qm_free(pid);
        return QM_ERROR;
    }

    if (pthread_create(pid, &attr, (void *(*)(void *))fn, arg) != 0) {
        pthread_attr_destroy(&attr);
        qm_free(pid);
        return QM_ERROR;
    }

    pthread_attr_destroy(&attr);
    task->hdl = pid;
    return QM_EOK;
}

int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio, int core_id)
{
    int ret;

    ret = qm_task_new(task, name, fn, arg, stack_size, prio);
    if (ret != QM_EOK || task == NULL || task->hdl == NULL || core_id < 0) {
        return ret;
    }

#if defined(__linux__)
    {
        cpu_set_t cpuset;

        CPU_ZERO(&cpuset);
        CPU_SET((unsigned int)core_id, &cpuset);
        if (pthread_setaffinity_np(*(pthread_t *)task->hdl, sizeof(cpuset), &cpuset) != 0) {
            QM_LOGW(LOG_TAG, "set affinity core=%d failed", core_id);
        }
    }
#endif

    return ret;
}

int qm_task_exit(qm_task_t *task)
{
    void *retval;
    if (task == NULL || task->hdl == NULL) {
        return QM_ENOMEM;
    }
    pthread_t pid = *(pthread_t *)task->hdl;
    // 判断线程是否是主动退出
    if (pthread_equal(pthread_self(), pid)) {
        pthread_exit(NULL);
    }
    else {
        pthread_cancel(pid);
        pthread_join(pid, &retval);
    }

    qm_free(task->hdl);
    task->hdl = NULL;

    return QM_EOK;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    pthread_mutex_t *_mutex = (pthread_mutex_t *)qm_malloc(sizeof(pthread_mutex_t));
    if (_mutex == NULL) {
        return QM_ENOMEM;
    }
    if (pthread_mutex_init(_mutex, NULL)) {
        free(_mutex);
        return QM_EINIT;
    }
    mutex->hdl = _mutex;
    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex) 
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return QM_ENOMEM;
    }
    pthread_mutex_destroy(mutex->hdl);
    qm_free(mutex->hdl);
    mutex->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    int ret;

    if (mutex == NULL || mutex->hdl == NULL) {
        return QM_ENOMEM;
    }

    if (ms == QM_WAIT_FOREVER) {
        return pthread_mutex_lock((pthread_mutex_t *)mutex->hdl);
    }

    if (ms == QM_NO_WAIT) {
        ret = pthread_mutex_trylock((pthread_mutex_t *)mutex->hdl);
        return (ret == 0) ? QM_EOK : QM_ETIMEOUT;
    }

    {
        struct timespec ts;

        if (linux_abstime_from_ms(ms, &ts) != 0) {
            return QM_ERROR;
        }

        ret = pthread_mutex_timedlock((pthread_mutex_t *)mutex->hdl, &ts);
        if (ret == ETIMEDOUT) {
            return QM_ETIMEOUT;
        }
        return (ret == 0) ? QM_EOK : QM_ERROR;
    }
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return QM_ENOMEM;
    }
    return pthread_mutex_unlock((pthread_mutex_t *)mutex->hdl);
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex && mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    sem_t *_sem = (sem_t *)qm_malloc(sizeof(sem_t));
    if (_sem == NULL) {
        return QM_ENOMEM;
    }
    if (sem_init(_sem, 0, count) < 0) {
        return QM_ERROR;
    }
    sem->hdl = _sem;

    return QM_EOK;
}

int qm_sem_free(qm_sem_t *sem)
{
    if (sem == NULL || sem->hdl == NULL) {
        return QM_ENOMEM;
    }
    sem_destroy((sem_t *)sem->hdl);
    qm_free(sem->hdl);
    sem->hdl = NULL;
    return QM_EOK;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    int ret;

    if (sem == NULL || sem->hdl == NULL) {
        return QM_ENOMEM;
    }

    if (ms == QM_WAIT_FOREVER) {
        return sem_wait((sem_t *)sem->hdl);
    }

    if (ms == QM_NO_WAIT) {
        ret = sem_trywait((sem_t *)sem->hdl);
        return (ret == 0) ? QM_EOK : QM_ETIMEOUT;
    }

    {
        struct timespec ts;

        if (linux_abstime_from_ms(ms, &ts) != 0) {
            return QM_ERROR;
        }

        ret = sem_timedwait((sem_t *)sem->hdl, &ts);
        if (ret != 0 && errno == ETIMEDOUT) {
            return QM_ETIMEOUT;
        }
        return (ret == 0) ? QM_EOK : QM_ERROR;
    }
}

int qm_sem_signal(qm_sem_t *sem)
{
    if (sem == NULL || sem->hdl == NULL) {
        return QM_ENOMEM;
    }
    return sem_post((sem_t *)sem->hdl);
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    return qm_sem_signal(sem);
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return sem && sem->hdl != NULL;
}

#if CONFIG_QM_OS_EVENT_SUPPORT

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned int bits;
} linux_event_ctx_t;

static int linux_event_bits_match(unsigned int bits, unsigned int wait_bits, unsigned char opt)
{
    if (opt == QM_EVENT_OR || opt == QM_EVENT_OR_CLEAR) {
        return (bits & wait_bits) != 0;
    }
    if (opt == QM_EVENT_AND || opt == QM_EVENT_AND_CLEAR) {
        return (bits & wait_bits) == wait_bits;
    }
    return 0;
}

static void linux_event_clear_matched(unsigned int *bits, unsigned int wait_bits, unsigned char opt)
{
    if (opt == QM_EVENT_OR_CLEAR) {
        *bits &= ~wait_bits;
    } else if (opt == QM_EVENT_AND_CLEAR) {
        *bits &= ~wait_bits;
    }
}

static int linux_event_wait(linux_event_ctx_t *ctx, unsigned int wait_event_bits,
                            unsigned char opt, unsigned int *actl_event_bits, unsigned int timeout)
{
    struct timespec ts;
    int use_timeout = 0;

    if (ctx == NULL || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    if (opt != QM_EVENT_OR && opt != QM_EVENT_OR_CLEAR &&
        opt != QM_EVENT_AND && opt != QM_EVENT_AND_CLEAR) {
        return -QM_EINVAL;
    }

    if (timeout != QM_WAIT_FOREVER && timeout != QM_NO_WAIT) {
        struct timeval now;

        gettimeofday(&now, NULL);
        ts.tv_sec = now.tv_sec + timeout / 1000;
        ts.tv_nsec = (long)now.tv_usec * 1000 + (timeout % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        use_timeout = 1;
    }

    pthread_mutex_lock(&ctx->lock);
    while (!linux_event_bits_match(ctx->bits, wait_event_bits, opt)) {
        if (timeout == QM_NO_WAIT) {
            *actl_event_bits = ctx->bits;
            pthread_mutex_unlock(&ctx->lock);
            return QM_EOK;
        }

        if (use_timeout) {
            if (pthread_cond_timedwait(&ctx->cond, &ctx->lock, &ts) != 0) {
                *actl_event_bits = ctx->bits;
                pthread_mutex_unlock(&ctx->lock);
                return QM_EOK;
            }
        } else {
            pthread_cond_wait(&ctx->cond, &ctx->lock);
        }
    }

    *actl_event_bits = ctx->bits;
    linux_event_clear_matched(&ctx->bits, wait_event_bits, opt);
    pthread_mutex_unlock(&ctx->lock);
    return QM_EOK;
}

int qm_event_new(qm_event_t *event)
{
    linux_event_ctx_t *ctx;

    if (event == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_event_ctx_t *)qm_malloc(sizeof(linux_event_ctx_t));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }

    memset(ctx, 0, sizeof(*ctx));
    if (pthread_mutex_init(&ctx->lock, NULL) != 0 ||
        pthread_cond_init(&ctx->cond, NULL) != 0) {
        qm_free(ctx);
        return -QM_EINIT;
    }

    event->hdl = ctx;
    return QM_EOK;
}

int qm_event_free(qm_event_t *event)
{
    linux_event_ctx_t *ctx;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_event_ctx_t *)event->hdl;
    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->lock);
    qm_free(ctx);
    event->hdl = NULL;
    return QM_EOK;
}

int qm_event_is_valid(qm_event_t *event)
{
    return event != NULL && event->hdl != NULL;
}

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt, unsigned int *actl_event_bits, unsigned int timeout)
{
    if (!qm_event_is_valid(event) || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    return linux_event_wait((linux_event_ctx_t *)event->hdl, wait_event_bits, opt,
                            actl_event_bits, timeout);
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    linux_event_ctx_t *ctx;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_event_ctx_t *)event->hdl;
    pthread_mutex_lock(&ctx->lock);
    ctx->bits |= event_bits;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->lock);
    return QM_EOK;
}

int qm_event_clear(qm_event_t *event, unsigned int event_bits)
{
    linux_event_ctx_t *ctx;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_event_ctx_t *)event->hdl;
    pthread_mutex_lock(&ctx->lock);
    ctx->bits &= ~event_bits;
    pthread_mutex_unlock(&ctx->lock);
    return QM_EOK;
}

#endif

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    qm_err_t ret = QM_EOK;
    _queue_t *_queue = (_queue_t *)qm_malloc(sizeof(_queue_t));

    (void)queue_len;

    if (queue == NULL) {
        return QM_EINVAL;
    }

    if (_queue == NULL) {
        ret = QM_ENOMEM;
        goto err_malloc;
    }

    memset(_queue, 0, sizeof(_queue_t));
    _queue->item_size = queue_size;

    _queue->list = qm_list_new();
    if (_queue->list == NULL) {
        ret = QM_ENOMEM;
        goto err_list;
    }

    if (pthread_mutex_init(&_queue->lock, NULL) < 0) {
        ret = QM_EINIT;
        goto err_mutex;
    }

    queue->hdl = _queue;
    return ret;

err_mutex:
    qm_list_destroy(_queue->list);
err_list:
    qm_free(_queue);
err_malloc:
    return ret;
}

int qm_queue_free(qm_queue_t *queue)
{
    if (queue == NULL || queue->hdl == NULL) {
        return QM_ENOMEM;
    }

    _queue_t *_queue = queue->hdl;

    pthread_mutex_destroy(&_queue->lock);

    qm_list_destroy(_queue->list);

    qm_free(_queue);

    queue->hdl = NULL;

    return QM_EOK;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    qm_err_t ret = QM_EOK;
    qm_list_node_t *node = NULL;

    if (queue == NULL || queue->hdl == NULL || msg == NULL || size == 0) {
        ret = QM_EINVAL;
        goto out;
    }
    
    _queue_t *_queue = (_queue_t *)queue->hdl;

    pthread_mutex_lock(&_queue->lock);
    node = qm_list_node_extra_new(size);
    if (node == NULL) {
        ret = QM_ENOMEM;
        goto out;
    }

    memcpy(node->val, msg, size);
    qm_list_rpush(_queue->list, node);
    node = NULL;
out:
    if (node) {
        qm_free(node);
    }
    pthread_mutex_unlock(&_queue->lock);
    return ret;
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return qm_queue_send(queue, msg, size);
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    qm_err_t ret = QM_EOK;
    qm_list_node_t *node = NULL;
    void *recv = NULL;
    unsigned int timeout = ms;
    unsigned int copy_size;
    _queue_t *_queue;

    if (queue == NULL || queue->hdl == NULL || msg == NULL || size == NULL) {
        return QM_EINVAL;
    }

    _queue = (_queue_t *)queue->hdl;
    copy_size = (*size != 0) ? *size : _queue->item_size;
    if (copy_size == 0) {
        return QM_EINVAL;
    }

    pthread_mutex_lock(&_queue->lock);
    while (!node) {
        node = qm_list_rpop(_queue->list);
        if (node != NULL) {
            break;
        }

        /* QM_NO_WAIT must not `--timeout`: unsigned 0 wraps to UINT_MAX. */
        if (ms == QM_NO_WAIT) {
            pthread_mutex_unlock(&_queue->lock);
            return QM_ETIMEOUT;
        }

        pthread_mutex_unlock(&_queue->lock);
        qm_msleep(1);
        if (ms != QM_WAIT_FOREVER) {
            if (--timeout == 0) {
                return QM_ETIMEOUT;
            }
        }

        pthread_mutex_lock(&_queue->lock);
    }

    recv = qm_list_node_val_get(node);
    memcpy(msg, recv, copy_size);
    *size = copy_size;
    qm_list_node_destroy(node);
    pthread_mutex_unlock(&_queue->lock);
    return ret;
}

bool_t qm_queue_is_empty(qm_queue_t *queue)
{
    qm_list_t *list;

    if (queue == NULL || queue->hdl == NULL) {
        return QM_TRUE;
    }

    list = ((_queue_t *)queue->hdl)->list;
    if (list == NULL || list->len == 0) {
        return QM_TRUE;
    }

    return QM_FALSE;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}

#define QM_TIMER_MAX_NUM  20

// posix定时器回调函数
static void posix_timer_cb(union sigval sv)
{
    // 获取定时器句柄
    timer_ctx_t *_timer = (timer_ctx_t *)sv.sival_ptr;
    pthread_mutex_lock(&_timer->lock);
    // 调用qumi定时器回调函数
    _timer->callback(_timer->qm_timer, _timer->args);

    pthread_mutex_unlock(&_timer->lock);
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{
    timer_ctx_t *_timer = (timer_ctx_t *)qm_malloc(sizeof(timer_ctx_t));
    if (_timer == NULL) {
        return QM_ENOMEM;
    }
    
    memset(&_timer->sev, 0, sizeof(_timer->sev));

    _timer->callback = cb;
    _timer->args = arg;
    _timer->qm_timer = timer;

    _timer->ts.tv_sec = ms / 1000;
    _timer->ts.tv_nsec = (ms % 1000) * 1e6; 
    _timer->repeat = repeat;

    _timer->sev.sigev_value.sival_ptr = _timer;        // 传入定时器句柄地址
    _timer->sev.sigev_notify = SIGEV_THREAD;           // 使用线程通知
    _timer->sev.sigev_notify_function = posix_timer_cb;
    _timer->sev.sigev_notify_attributes = NULL;        // 使用默认线程属性

    if (pthread_mutex_init(&_timer->lock, NULL) < 0) {
        goto out;
    }

    if (timer_create(CLOCK_MONOTONIC, &_timer->sev, &_timer->timerid) < 0) {
        goto out;
    }

    timer->hdl = _timer;

    return QM_EOK;
out:
    qm_free(_timer);
    return QM_ERROR;
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return timer && timer->hdl != NULL;
}

int qm_timer_free(qm_timer_t *timer)
{
    if (timer == NULL || timer->hdl == NULL) {
        return QM_ENOMEM;
    }

    timer_ctx_t *_timer = (timer_ctx_t *)timer->hdl;

    pthread_mutex_destroy(&_timer->lock);

    if (timer_delete(_timer->timerid) < 0) {
        return QM_ERROR;
    }

    qm_free(timer->hdl);
    timer->hdl = NULL;

    return QM_EOK;
}

int qm_timer_start(qm_timer_t *timer)
{
    if (timer == NULL || timer->hdl == NULL) {
        return QM_ENOMEM;
    }

    timer_ctx_t *_timer = (timer_ctx_t *)timer->hdl;
    // 判断定时器是否周期运行
    if (_timer->repeat) {
        memcpy(&_timer->it.it_interval, &_timer->ts, sizeof(struct timespec));            
        _timer->it.it_value.tv_sec = 0;                             
        _timer->it.it_value.tv_nsec = 1; 
    }
    else {
        memcpy(&_timer->it.it_value, &_timer->ts, sizeof(struct timespec));
        _timer->it.it_interval.tv_sec = 0;                  
        _timer->it.it_interval.tv_nsec = 0; 
    }

    return timer_settime(_timer->timerid, 0, &_timer->it, NULL);
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    qm_timer_start(timer);
    return 0;
}
 
int qm_timer_stop(qm_timer_t *timer)
{
    if (timer == NULL || timer->hdl == NULL) {
        return QM_ENOMEM;
    }

    timer_ctx_t *_timer = (timer_ctx_t *)timer->hdl;

    _timer->it.it_interval.tv_sec = 0;                  
    _timer->it.it_interval.tv_nsec = 0; 
    _timer->it.it_value.tv_sec = 0;                             
    _timer->it.it_value.tv_nsec = 0; 

    return timer_settime(_timer->timerid, 0, &_timer->it, NULL);
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if (timer == NULL || timer->hdl == NULL) {
        return QM_ENOMEM;
    }

    timer_ctx_t *_timer = (timer_ctx_t *)timer->hdl;

    _timer->ts.tv_sec = ms / 1000;
    _timer->ts.tv_nsec = (ms % 1000) * 1e6; 

    return qm_timer_start(timer);
}

#if !CONFIG_QM_MTRACE_SUPPORT

void* qm_calloc(unsigned int nitems, unsigned int size)
{
    return calloc(nitems, size);
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *mem)
{
    free(mem);
}

uint32_t qm_free_mem_get(void)
{
    struct sysinfo info;
    sysinfo(&info);
    return info.freeram;
}

void* qm_aligned_alloc(unsigned int alignment, unsigned int size)
{
    return aligned_alloc(alignment, size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

#endif

uint32_t qm_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1e6;
}

int qm_msleep(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;        
    ts.tv_nsec = (ms % 1000) * 1e6;  
    nanosleep(&ts, NULL);
    return 0;
}

int qm_usleep(int us)
{
    struct timespec ts;
    ts.tv_sec = 0; 
    ts.tv_nsec = us*1e3;
    nanosleep(&ts, NULL);
    return 0;
}

int qm_printf(const char * fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vprintf(fmt, args);
    va_end(args);
    return ret;
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}
  
void qm_srandom(uint32_t seed)
{
    srandom(seed);
}

uint32_t qm_random_get(uint32_t region)
{
    return random() % region;
}

void qm_init(void)
{

}