#include "qm.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/*
 * macOS does not provide the unnamed POSIX semaphores used by the Linux
 * adapter.  The small native wrappers below use pthread condition variables
 * instead, which also gives all waits one consistent timeout path.
 */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned int count;
} mac_sem_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned int bits;
} mac_event_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t readable;
    pthread_cond_t writable;
    unsigned char *data;
    unsigned int length;
    unsigned int item_size;
    unsigned int head;
    unsigned int tail;
    unsigned int count;
} mac_queue_t;

typedef struct {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    qm_timer_t *public_timer;
    qm_timer_cb_t callback;
    void *arg;
    int period_ms;
    int repeat;
    int running;
    int stopping;
} mac_timer_t;

typedef struct {
    void (*fn)(void *);
    void *arg;
} mac_task_start_t;

static void deadline_after_ms(struct timespec *ts, unsigned int timeout_ms)
{
    /* pthread condition variables use CLOCK_REALTIME on macOS. */
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += (time_t)(timeout_ms / 1000u);
    ts->tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static int cond_wait_ms(pthread_cond_t *cond, pthread_mutex_t *lock,
                        unsigned int timeout_ms)
{
    struct timespec deadline;

    if (timeout_ms == QM_WAIT_FOREVER) {
        return pthread_cond_wait(cond, lock);
    }
    deadline_after_ms(&deadline, timeout_ms);
    return pthread_cond_timedwait(cond, lock, &deadline);
}

static void *task_entry(void *arg)
{
    mac_task_start_t *start = (mac_task_start_t *)arg;
    void (*fn)(void *) = start->fn;
    void *fn_arg = start->arg;

    free(start);
    fn(fn_arg);
    return NULL;
}

const char *qm_version_get(void)
{
    return "macOS-posix";
}

void qm_reboot(void)
{
    /* A host process must never reboot the developer's machine. */
    QM_LOGW("macos os", "qm_reboot is unsupported on the host");
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *),
                void *arg, int stack_size, int prio)
{
    pthread_attr_t attr;
    mac_task_start_t *start;
    pthread_t *thread;
    size_t native_default_stack;
    size_t native_stack;
    int rc;

    (void)name;
    (void)prio;
    if (task == NULL || fn == NULL) {
        return -QM_EINVAL;
    }

    start = (mac_task_start_t *)calloc(1, sizeof(*start));
    thread = (pthread_t *)calloc(1, sizeof(*thread));
    if (start == NULL || thread == NULL) {
        free(start);
        free(thread);
        return -QM_ENOMEM;
    }
    start->fn = fn;
    start->arg = arg;

    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        free(start);
        free(thread);
        return -QM_ERROR;
    }
    if (stack_size > 0) {
        native_stack = (size_t)stack_size;
        rc = pthread_attr_getstacksize(&attr, &native_default_stack);
        if (rc == 0 && native_stack < native_default_stack) {
            /* Embedded task stacks are too small for macOS framework loading. */
            native_stack = native_default_stack;
        }
        if (rc == 0) {
            rc = pthread_attr_setstacksize(&attr, native_stack);
        }
        if (rc != 0) {
            pthread_attr_destroy(&attr);
            free(start);
            free(thread);
            return -QM_ERROR;
        }
    }
    rc = pthread_create(thread, &attr, task_entry, start);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(start);
        free(thread);
        return -QM_ERROR;
    }
    task->hdl = thread;
    return QM_EOK;
}

int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *),
                        void *arg, int stack_size, int prio, int core_id)
{
    (void)core_id;
    return qm_task_new(task, name, fn, arg, stack_size, prio);
}

qm_bool qm_task_is_current(const qm_task_t *task)
{
    return task != NULL && task->hdl != NULL &&
           pthread_equal(pthread_self(), *(pthread_t *)task->hdl) ?
           QM_TRUE : QM_FALSE;
}

int qm_task_exit(qm_task_t *task)
{
    pthread_t *thread;

    if (task == NULL || task->hdl == NULL) {
        return -QM_EINVAL;
    }
    thread = (pthread_t *)task->hdl;
    if (pthread_equal(pthread_self(), *thread)) {
        free(thread);
        task->hdl = NULL;
        pthread_exit(NULL);
    }
    pthread_cancel(*thread);
    pthread_join(*thread, NULL);
    free(thread);
    task->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    pthread_mutex_t *native;

    if (mutex == NULL) {
        return -QM_EINVAL;
    }
    native = (pthread_mutex_t *)calloc(1, sizeof(*native));
    if (native == NULL) {
        return -QM_ENOMEM;
    }
    if (pthread_mutex_init(native, NULL) != 0) {
        free(native);
        return -QM_EINIT;
    }
    mutex->hdl = native;
    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }
    pthread_mutex_destroy((pthread_mutex_t *)mutex->hdl);
    free(mutex->hdl);
    mutex->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int timeout_ms)
{
    int rc;

    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }
    if (timeout_ms == QM_WAIT_FOREVER) {
        rc = pthread_mutex_lock((pthread_mutex_t *)mutex->hdl);
    } else if (timeout_ms == QM_NO_WAIT) {
        rc = pthread_mutex_trylock((pthread_mutex_t *)mutex->hdl);
    } else {
        struct timespec deadline;
        struct timespec now;
        deadline_after_ms(&deadline, timeout_ms);
        do {
            rc = pthread_mutex_trylock((pthread_mutex_t *)mutex->hdl);
            if (rc == 0) {
                break;
            }
            clock_gettime(CLOCK_REALTIME, &now);
            nanosleep(&(struct timespec){0, 1000000L}, NULL);
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                rc = ETIMEDOUT;
                break;
            }
        } while (rc == EBUSY);
    }
    return rc == 0 ? QM_EOK : (rc == ETIMEDOUT || rc == EBUSY ? -QM_ETIMEOUT : -QM_ERROR);
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }
    return pthread_mutex_unlock((pthread_mutex_t *)mutex->hdl) == 0 ? QM_EOK : -QM_ERROR;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex != NULL && mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int initial_count)
{
    mac_sem_t *native;

    if (sem == NULL || initial_count < 0) {
        return -QM_EINVAL;
    }
    native = (mac_sem_t *)calloc(1, sizeof(*native));
    if (native == NULL) {
        return -QM_ENOMEM;
    }
    pthread_mutex_init(&native->lock, NULL);
    pthread_cond_init(&native->cond, NULL);
    native->count = (unsigned int)initial_count;
    sem->hdl = native;
    return QM_EOK;
}

int qm_sem_free(qm_sem_t *sem)
{
    mac_sem_t *native;

    if (sem == NULL || sem->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_sem_t *)sem->hdl;
    pthread_cond_destroy(&native->cond);
    pthread_mutex_destroy(&native->lock);
    free(native);
    sem->hdl = NULL;
    return QM_EOK;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int timeout_ms)
{
    mac_sem_t *native;
    int rc = 0;

    if (sem == NULL || sem->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_sem_t *)sem->hdl;
    pthread_mutex_lock(&native->lock);
    while (native->count == 0) {
        rc = cond_wait_ms(&native->cond, &native->lock, timeout_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&native->lock);
            return rc == ETIMEDOUT ? -QM_ETIMEOUT : -QM_ERROR;
        }
    }
    native->count--;
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_sem_signal(qm_sem_t *sem)
{
    mac_sem_t *native;

    if (sem == NULL || sem->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_sem_t *)sem->hdl;
    pthread_mutex_lock(&native->lock);
    native->count++;
    pthread_cond_signal(&native->cond);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    return qm_sem_signal(sem);
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return sem != NULL && sem->hdl != NULL;
}

static int event_matches(unsigned int bits, unsigned int wait_bits, unsigned char opt)
{
    if (opt == QM_EVENT_OR || opt == QM_EVENT_OR_CLEAR) {
        return (bits & wait_bits) != 0;
    }
    if (opt == QM_EVENT_AND || opt == QM_EVENT_AND_CLEAR) {
        return (bits & wait_bits) == wait_bits;
    }
    return 0;
}

int qm_event_new(qm_event_t *event)
{
    mac_event_t *native;

    if (event == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_event_t *)calloc(1, sizeof(*native));
    if (native == NULL) {
        return -QM_ENOMEM;
    }
    pthread_mutex_init(&native->lock, NULL);
    pthread_cond_init(&native->cond, NULL);
    event->hdl = native;
    return QM_EOK;
}

int qm_event_free(qm_event_t *event)
{
    mac_event_t *native;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_event_t *)event->hdl;
    pthread_cond_destroy(&native->cond);
    pthread_mutex_destroy(&native->lock);
    free(native);
    event->hdl = NULL;
    return QM_EOK;
}

int qm_event_is_valid(qm_event_t *event)
{
    return event != NULL && event->hdl != NULL;
}

int qm_event_get(qm_event_t *event, unsigned int wait_bits, unsigned char opt,
                 unsigned int *active_bits, unsigned int timeout_ms)
{
    mac_event_t *native;
    int rc;

    if (!qm_event_is_valid(event) || active_bits == NULL ||
        (opt != QM_EVENT_OR && opt != QM_EVENT_OR_CLEAR &&
         opt != QM_EVENT_AND && opt != QM_EVENT_AND_CLEAR)) {
        return -QM_EINVAL;
    }
    native = (mac_event_t *)event->hdl;
    pthread_mutex_lock(&native->lock);
    while (!event_matches(native->bits, wait_bits, opt)) {
        if (timeout_ms == QM_NO_WAIT) {
            *active_bits = native->bits;
            pthread_mutex_unlock(&native->lock);
            return QM_EOK;
        }
        rc = cond_wait_ms(&native->cond, &native->lock, timeout_ms);
        if (rc != 0) {
            *active_bits = native->bits;
            pthread_mutex_unlock(&native->lock);
            return rc == ETIMEDOUT ? -QM_ETIMEOUT : -QM_ERROR;
        }
    }
    *active_bits = native->bits;
    if (opt == QM_EVENT_OR_CLEAR || opt == QM_EVENT_AND_CLEAR) {
        native->bits &= ~wait_bits;
    }
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    mac_event_t *native;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_event_t *)event->hdl;
    pthread_mutex_lock(&native->lock);
    native->bits |= event_bits;
    pthread_cond_broadcast(&native->cond);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_event_clear(qm_event_t *event, unsigned int event_bits)
{
    mac_event_t *native;

    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_event_t *)event->hdl;
    pthread_mutex_lock(&native->lock);
    native->bits &= ~event_bits;
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    mac_queue_t *native;

    if (queue == NULL || queue_len == 0 || queue_size == 0) {
        return -QM_EINVAL;
    }
    native = (mac_queue_t *)calloc(1, sizeof(*native));
    if (native == NULL) {
        return -QM_ENOMEM;
    }
    native->data = (unsigned char *)calloc(queue_len, queue_size);
    if (native->data == NULL) {
        free(native);
        return -QM_ENOMEM;
    }
    native->length = queue_len;
    native->item_size = queue_size;
    pthread_mutex_init(&native->lock, NULL);
    pthread_cond_init(&native->readable, NULL);
    pthread_cond_init(&native->writable, NULL);
    queue->hdl = native;
    return QM_EOK;
}

int qm_queue_free(qm_queue_t *queue)
{
    mac_queue_t *native;

    if (queue == NULL || queue->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_queue_t *)queue->hdl;
    pthread_cond_destroy(&native->readable);
    pthread_cond_destroy(&native->writable);
    pthread_mutex_destroy(&native->lock);
    free(native->data);
    free(native);
    queue->hdl = NULL;
    return QM_EOK;
}

static int queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    mac_queue_t *native;

    if (queue == NULL || queue->hdl == NULL || msg == NULL || size == 0) {
        return -QM_EINVAL;
    }
    native = (mac_queue_t *)queue->hdl;
    if (size > native->item_size) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&native->lock);
    if (native->count == native->length) {
        pthread_mutex_unlock(&native->lock);
        return -QM_EFULL;
    }
    memcpy(native->data + native->tail * native->item_size, msg, size);
    if (size < native->item_size) {
        memset(native->data + native->tail * native->item_size + size, 0,
               native->item_size - size);
    }
    native->tail = (native->tail + 1u) % native->length;
    native->count++;
    pthread_cond_signal(&native->readable);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    return queue_send(queue, msg, size);
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return queue_send(queue, msg, size);
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int timeout_ms)
{
    mac_queue_t *native;
    unsigned int copy_size;
    int rc;

    if (queue == NULL || queue->hdl == NULL || msg == NULL || size == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_queue_t *)queue->hdl;
    copy_size = *size == 0 ? native->item_size : *size;
    if (copy_size > native->item_size) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&native->lock);
    while (native->count == 0) {
        rc = cond_wait_ms(&native->readable, &native->lock, timeout_ms);
        if (rc != 0) {
            pthread_mutex_unlock(&native->lock);
            return rc == ETIMEDOUT ? -QM_ETIMEOUT : -QM_ERROR;
        }
    }
    memcpy(msg, native->data + native->head * native->item_size, copy_size);
    *size = copy_size;
    native->head = (native->head + 1u) % native->length;
    native->count--;
    pthread_cond_signal(&native->writable);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue != NULL && queue->hdl != NULL;
}

bool_t qm_queue_is_empty(qm_queue_t *queue)
{
    mac_queue_t *native;
    bool_t empty;

    if (queue == NULL || queue->hdl == NULL) {
        return QM_TRUE;
    }
    native = (mac_queue_t *)queue->hdl;
    pthread_mutex_lock(&native->lock);
    empty = native->count == 0 ? QM_TRUE : QM_FALSE;
    pthread_mutex_unlock(&native->lock);
    return empty;
}

static void *timer_entry(void *arg)
{
    mac_timer_t *timer = (mac_timer_t *)arg;

    pthread_mutex_lock(&timer->lock);
    while (!timer->stopping) {
        struct timespec deadline;
        int rc;

        while (!timer->running && !timer->stopping) {
            pthread_cond_wait(&timer->cond, &timer->lock);
        }
        if (timer->stopping) {
            break;
        }
        deadline_after_ms(&deadline, (unsigned int)(timer->period_ms < 0 ? 0 : timer->period_ms));
        rc = pthread_cond_timedwait(&timer->cond, &timer->lock, &deadline);
        if (timer->stopping) {
            break;
        }
        if (rc == ETIMEDOUT && timer->running && timer->callback != NULL) {
            qm_timer_cb_t callback = timer->callback;
            qm_timer_t *public_timer = timer->public_timer;
            void *callback_arg = timer->arg;
            if (!timer->repeat) {
                timer->running = 0;
            }
            pthread_mutex_unlock(&timer->lock);
            callback(public_timer, callback_arg);
            pthread_mutex_lock(&timer->lock);
        }
    }
    pthread_mutex_unlock(&timer->lock);
    return NULL;
}

int qm_timer_new_ext(qm_timer_t *timer, qm_timer_cb_t callback, void *arg,
                     int period_ms, int repeat, unsigned char auto_run)
{
    mac_timer_t *native;

    if (timer == NULL || callback == NULL || period_ms < 0) {
        return -QM_EINVAL;
    }
    native = (mac_timer_t *)calloc(1, sizeof(*native));
    if (native == NULL) {
        return -QM_ENOMEM;
    }
    pthread_mutex_init(&native->lock, NULL);
    pthread_cond_init(&native->cond, NULL);
    native->public_timer = timer;
    native->callback = callback;
    native->arg = arg;
    native->period_ms = period_ms;
    native->repeat = repeat != 0;
    native->running = auto_run != 0;
    if (pthread_create(&native->thread, NULL, timer_entry, native) != 0) {
        pthread_cond_destroy(&native->cond);
        pthread_mutex_destroy(&native->lock);
        free(native);
        return -QM_ERROR;
    }
    timer->hdl = native;
    pthread_cond_signal(&native->cond);
    return QM_EOK;
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t callback, void *arg,
                 int period_ms, int repeat)
{
    return qm_timer_new_ext(timer, callback, arg, period_ms, repeat, 0);
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return timer != NULL && timer->hdl != NULL;
}

int qm_timer_start(qm_timer_t *timer)
{
    mac_timer_t *native;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_timer_t *)timer->hdl;
    pthread_mutex_lock(&native->lock);
    native->running = 1;
    pthread_cond_signal(&native->cond);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    return qm_timer_start(timer);
}

int qm_timer_stop(qm_timer_t *timer)
{
    mac_timer_t *native;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_timer_t *)timer->hdl;
    pthread_mutex_lock(&native->lock);
    native->running = 0;
    pthread_cond_signal(&native->cond);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_timer_change(qm_timer_t *timer, int period_ms)
{
    mac_timer_t *native;

    if (timer == NULL || timer->hdl == NULL || period_ms < 0) {
        return -QM_EINVAL;
    }
    native = (mac_timer_t *)timer->hdl;
    pthread_mutex_lock(&native->lock);
    native->period_ms = period_ms;
    pthread_cond_signal(&native->cond);
    pthread_mutex_unlock(&native->lock);
    return QM_EOK;
}

int qm_timer_free(qm_timer_t *timer)
{
    mac_timer_t *native;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }
    native = (mac_timer_t *)timer->hdl;
    pthread_mutex_lock(&native->lock);
    native->stopping = 1;
    pthread_cond_signal(&native->cond);
    pthread_mutex_unlock(&native->lock);
    pthread_join(native->thread, NULL);
    pthread_cond_destroy(&native->cond);
    pthread_mutex_destroy(&native->lock);
    free(native);
    timer->hdl = NULL;
    return QM_EOK;
}

#define QM_MACOS_WEAK __attribute__((weak))

void *QM_MACOS_WEAK qm_calloc(unsigned int nitems, unsigned int size)
{
    return calloc(nitems, size);
}

void *QM_MACOS_WEAK qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *QM_MACOS_WEAK qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void QM_MACOS_WEAK qm_free(void *mem)
{
    free(mem);
}

void *QM_MACOS_WEAK qm_aligned_alloc(unsigned int alignment, unsigned int size)
{
    void *mem = NULL;
    return posix_memalign(&mem, alignment, size) == 0 ? mem : NULL;
}

void *QM_MACOS_WEAK qm_psram_malloc(unsigned int size) { return qm_malloc(size); }
void *QM_MACOS_WEAK qm_psram_calloc(unsigned int nitems, unsigned int size) { return qm_calloc(nitems, size); }
void *QM_MACOS_WEAK qm_psram_realloc(void *mem, unsigned int size) { return qm_realloc(mem, size); }
uint32_t QM_MACOS_WEAK qm_free_mem_get(void) { return 0; }

uint32_t qm_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
}

int qm_msleep(int ms)
{
    struct timespec delay;
    if (ms < 0) {
        return -QM_EINVAL;
    }
    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (long)(ms % 1000) * 1000000L;
    return nanosleep(&delay, NULL) == 0 ? QM_EOK : -QM_EINTR;
}

int qm_usleep(int us)
{
    struct timespec delay;
    if (us < 0) {
        return -QM_EINVAL;
    }
    delay.tv_sec = us / 1000000;
    delay.tv_nsec = (long)(us % 1000000) * 1000L;
    return nanosleep(&delay, NULL) == 0 ? QM_EOK : -QM_EINTR;
}

int qm_printf(const char *fmt, ...)
{
    va_list args;
    int rc;
    va_start(args, fmt);
    rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int rc;
    va_start(args, fmt);
    rc = vsnprintf(str, (size_t)len, fmt, args);
    va_end(args);
    return rc;
}

int32_t qm_dbg_write(void *data, uint32_t size, uint32_t timeout)
{
    const unsigned char *cursor = (const unsigned char *)data;
    uint32_t written = 0;

    (void)timeout;
    if (data == NULL && size != 0U) {
        return -QM_EINVAL;
    }
    while (written < size) {
        ssize_t ret = write(STDOUT_FILENO, cursor + written, size - written);
        if (ret > 0) {
            written += (uint32_t)ret;
        } else if (ret < 0 && errno == EINTR) {
            continue;
        } else {
            return written != 0U ? (int32_t)written : -QM_EIO;
        }
    }
    return (int32_t)written;
}

int32_t qm_dbg_read(void *data, uint32_t expect_size, uint32_t timeout)
{
    fd_set readable;
    struct timeval tv;
    struct timeval *tv_ptr = &tv;
    ssize_t ret;

    if (data == NULL || expect_size == 0U) {
        return -QM_EINVAL;
    }

    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);
    if (timeout == QM_WAIT_FOREVER) {
        tv_ptr = NULL;
    } else {
        tv.tv_sec = (time_t)(timeout / 1000U);
        tv.tv_usec = (suseconds_t)(timeout % 1000U) * 1000;
    }
    do {
        ret = select(STDIN_FILENO + 1, &readable, NULL, NULL, tv_ptr);
    } while (ret < 0 && errno == EINTR);
    if (ret == 0) {
        return 0;
    }
    if (ret < 0) {
        return -QM_EIO;
    }

    do {
        ret = read(STDIN_FILENO, data, expect_size);
    } while (ret < 0 && errno == EINTR);
    return ret >= 0 ? (int32_t)ret : -QM_EIO;
}

void qm_srandom(uint32_t seed) { srandom(seed); }
uint32_t qm_random_get(uint32_t region) { return region == 0 ? 0 : (uint32_t)(random() % region); }
void qm_init(void) {}
