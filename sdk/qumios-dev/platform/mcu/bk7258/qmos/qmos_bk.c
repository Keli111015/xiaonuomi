#include "qm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <os/os.h>
#include <os/mem.h>
#include <components/system.h>

#include "qm_config.h"

#include "mbedtls/platform.h"

#define QM_BK_TIMER_SLOTS   16

/* Dual TLS (MQTT+WSS): put mbedtls buffers in PSRAM instead of editing bk_idk tls_mem.c */
static void *bk_mbedtls_psram_calloc(size_t n, size_t size)
{
    size_t len = n * size;

    if (len == 0) {
        return NULL;
    }
    return psram_zalloc(len);
}

/* os_free is a macro (os_free_debug); need a real function for the function pointer. */
static void bk_mbedtls_psram_free(void *ptr)
{
    os_free(ptr);
}

static uint8_t qm_prio_to_bk(int prio)
{
    if (prio < 0) {
        return 7;
    }
    if (prio > 9) {
        return 2;
    }
    return (uint8_t)(9 - prio);
}

void qm_reboot(void)
{
    bk_reboot();
}

const char *qm_version_get(void)
{
    return "BK7258-QumIOS-1.0";
}

#if CONFIG_QM_OS_SUPPORT

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                int stack_size, int prio)
{
    beken_thread_t *th;

    if (task == NULL || fn == NULL) {
        return -QM_EINVAL;
    }

    th = (beken_thread_t *)os_malloc(sizeof(beken_thread_t));
    if (th == NULL) {
        return -QM_ENOMEM;
    }

    if (rtos_create_thread(th, qm_prio_to_bk(prio), name, (beken_thread_function_t)fn,
                           (uint32_t)stack_size, arg) != kNoErr) {
        os_free(th);
        return -QM_ERROR;
    }

    task->hdl = th;
    return QM_EOK;
}

int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                        int stack_size, int prio, int core_id)
{
    (void)core_id;
    return qm_task_new(task, name, fn, arg, stack_size, prio);
}

qm_bool qm_task_is_current(const qm_task_t *task)
{
    return task != NULL && task->hdl != NULL &&
           rtos_is_current_thread((beken_thread_t *)task->hdl) ?
           QM_TRUE : QM_FALSE;
}

int qm_task_exit(qm_task_t *task)
{
    if (task == NULL || task->hdl == NULL) {
        rtos_delete_thread(NULL);
        return QM_EOK;
    }

    rtos_delete_thread((beken_thread_t *)task->hdl);
    os_free(task->hdl);
    task->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    beken_mutex_t *m;

    if (mutex == NULL || mutex->hdl != NULL) {
        return -QM_EINVAL;
    }

    m = (beken_mutex_t *)os_malloc(sizeof(beken_mutex_t));
    if (m == NULL) {
        return -QM_ENOMEM;
    }

    if (rtos_init_mutex(m) != kNoErr) {
        os_free(m);
        return -QM_ERROR;
    }

    mutex->hdl = m;
    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }

    rtos_deinit_mutex((beken_mutex_t *)mutex->hdl);
    os_free(mutex->hdl);
    mutex->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    bk_err_t ret;

    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }

    if (ms == QM_WAIT_FOREVER) {
        ret = rtos_lock_mutex((beken_mutex_t *)mutex->hdl);
    } else {
        ret = rtos_lock_mutex_timeout((beken_mutex_t *)mutex->hdl, ms);
    }

    return (ret == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if (mutex == NULL || mutex->hdl == NULL) {
        return -QM_EINVAL;
    }

    return (rtos_unlock_mutex((beken_mutex_t *)mutex->hdl) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return (mutex != NULL && mutex->hdl != NULL) ? 1 : 0;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    beken_semaphore_t *s;

    if (sem == NULL) {
        return -QM_EINVAL;
    }

    s = (beken_semaphore_t *)os_malloc(sizeof(beken_semaphore_t));
    if (s == NULL) {
        return -QM_ENOMEM;
    }

    if (rtos_init_semaphore_ex(s, 100, count) != kNoErr) {
        os_free(s);
        return -QM_ERROR;
    }

    sem->hdl = s;
    return QM_EOK;
}

int qm_sem_free(qm_sem_t *sem)
{
    if (sem == NULL || sem->hdl == NULL) {
        return -QM_ERROR;
    }

    rtos_deinit_semaphore((beken_semaphore_t *)sem->hdl);
    os_free(sem->hdl);
    sem->hdl = NULL;
    return QM_EOK;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    uint32_t to = (ms == QM_WAIT_FOREVER) ? BEKEN_WAIT_FOREVER : ms;

    if (sem == NULL || sem->hdl == NULL) {
        return -QM_ERROR;
    }

    return (rtos_get_semaphore((beken_semaphore_t *)sem->hdl, to) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_sem_signal(qm_sem_t *sem)
{
    if (sem == NULL || sem->hdl == NULL) {
        return -QM_ERROR;
    }

    return (rtos_set_semaphore((beken_semaphore_t *)sem->hdl) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    return qm_sem_signal(sem);
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return (sem != NULL && sem->hdl != NULL) ? 1 : 0;
}

#if CONFIG_QM_OS_EVENT_SUPPORT

int qm_event_new(qm_event_t *event)
{
    beken_event_t *ev;

    if (event == NULL) {
        return -QM_EINVAL;
    }

    ev = (beken_event_t *)os_malloc(sizeof(beken_event_t));
    if (ev == NULL) {
        return -QM_ENOMEM;
    }

    if (rtos_init_event_flags(ev) != kNoErr) {
        os_free(ev);
        return -QM_ERROR;
    }

    event->hdl = ev;
    return QM_EOK;
}

int qm_event_free(qm_event_t *event)
{
    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    rtos_deinit_event_flags((beken_event_t *)event->hdl);
    os_free(event->hdl);
    event->hdl = NULL;
    return QM_EOK;
}

int qm_event_is_valid(qm_event_t *event)
{
    return event != NULL && event->hdl != NULL;
}

static beken_event_flags_wait_option_t qm_event_wait_opt(unsigned char opt)
{
    if (opt == QM_EVENT_AND || opt == QM_EVENT_AND_CLEAR) {
        return WAIT_FOR_ALL_EVENTS;
    }
    return WAIT_FOR_ANY_EVENT;
}

static beken_bool_t qm_event_clear_on_exit(unsigned char opt)
{
    return (opt == QM_EVENT_OR_CLEAR || opt == QM_EVENT_AND_CLEAR) ? BK_TRUE : BK_FALSE;
}

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt,
                 unsigned int *actl_event_bits, unsigned int timeout)
{
    beken_event_flags_t bits;

    if (!qm_event_is_valid(event) || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    bits = rtos_wait_for_event_flags((beken_event_t *)event->hdl, wait_event_bits,
                                     qm_event_clear_on_exit(opt), qm_event_wait_opt(opt), timeout);
    *actl_event_bits = (unsigned int)bits;
    return QM_EOK;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    rtos_set_event_flags((beken_event_t *)event->hdl, event_bits);
    return QM_EOK;
}

int qm_event_clear(qm_event_t *event, unsigned int event_bits)
{
    if (event == NULL || event->hdl == NULL) {
        return -QM_EINVAL;
    }

    rtos_clear_event_flags((beken_event_t *)event->hdl, event_bits);
    return QM_EOK;
}

#endif /* CONFIG_QM_OS_EVENT_SUPPORT */

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    beken_queue_t *q;

    if (queue == NULL || queue_len == 0 || queue_size == 0) {
        return -QM_EINVAL;
    }

    q = (beken_queue_t *)os_malloc(sizeof(beken_queue_t));
    if (q == NULL) {
        return -QM_ENOMEM;
    }

    if (rtos_init_queue(q, "qm_q", queue_size, queue_len) != kNoErr) {
        os_free(q);
        return -QM_ERROR;
    }

    queue->hdl = q;
    return QM_EOK;
}

int qm_queue_free(qm_queue_t *queue)
{
    if (queue == NULL || queue->hdl == NULL) {
        return -QM_EINVAL;
    }

    rtos_deinit_queue((beken_queue_t *)queue->hdl);
    os_free(queue->hdl);
    queue->hdl = NULL;
    return QM_EOK;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    (void)size;

    if (queue == NULL || queue->hdl == NULL || msg == NULL) {
        return -QM_EINVAL;
    }

    return (rtos_push_to_queue((beken_queue_t *)queue->hdl, msg, 0) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return qm_queue_send(queue, msg, size);
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    uint32_t to = (ms == QM_WAIT_FOREVER) ? BEKEN_WAIT_FOREVER : ms;

    if (queue == NULL || queue->hdl == NULL || msg == NULL) {
        return -QM_EINVAL;
    }

    if (rtos_pop_from_queue((beken_queue_t *)queue->hdl, msg, to) != kNoErr) {
        return -QM_ERROR;
    }

    if (size) {
        *size = 0;
    }
    return QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return (queue != NULL && queue->hdl != NULL) ? 1 : 0;
}

bool_t qm_queue_is_empty(qm_queue_t *queue)
{
    if (queue == NULL || queue->hdl == NULL) {
        return QM_TRUE;
    }
    return rtos_is_queue_empty((beken_queue_t *)queue->hdl) ? QM_TRUE : QM_FALSE;
}

#if CONFIG_QM_OS_TIMER_SUPPORT

typedef struct {
    uint8_t in_use;
    uint8_t repeat;
    qm_timer_t *qm_timer;
    qm_timer_cb_t cb;
    void *arg;
    beken_timer_t periodic;
    beken2_timer_t oneshot;
} qm_bk_timer_slot_t;

static qm_bk_timer_slot_t g_qm_timers[QM_BK_TIMER_SLOTS];

static int qm_bk_timer_slot_get(void)
{
    int i;

    for (i = 0; i < QM_BK_TIMER_SLOTS; i++) {
        if (!g_qm_timers[i].in_use) {
            return i;
        }
    }
    return -1;
}

static void qm_bk_timer_periodic_handler(void *arg)
{
    int idx = (int)(intptr_t)arg;
    qm_bk_timer_slot_t *slot = &g_qm_timers[idx];

    if (slot->cb && slot->qm_timer) {
        slot->cb(slot->qm_timer, slot->arg);
    }
}

static void qm_bk_timer_oneshot_handler(void *larg, void *rarg)
{
    int idx = (int)(intptr_t)larg;
    qm_bk_timer_slot_t *slot = &g_qm_timers[idx];

    (void)rarg;

    if (slot->cb && slot->qm_timer) {
        slot->cb(slot->qm_timer, slot->arg);
    }
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{
    int idx;
    qm_bk_timer_slot_t *slot;

    if (timer == NULL || cb == NULL || ms <= 0) {
        return -QM_EINVAL;
    }

    idx = qm_bk_timer_slot_get();
    if (idx < 0) {
        return -QM_ERROR;
    }

    slot = &g_qm_timers[idx];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1;
    slot->repeat = (repeat != 0) ? 1 : 0;
    slot->qm_timer = timer;
    slot->cb = cb;
    slot->arg = arg;

    if (slot->repeat) {
        if (rtos_init_timer(&slot->periodic, (uint32_t)ms, qm_bk_timer_periodic_handler,
                            (void *)(intptr_t)idx) != kNoErr) {
            slot->in_use = 0;
            return -QM_ERROR;
        }
        timer->hdl = &slot->periodic;
    } else {
        if (rtos_init_oneshot_timer(&slot->oneshot, (uint32_t)ms, qm_bk_timer_oneshot_handler,
                                    (void *)(intptr_t)idx, NULL) != kNoErr) {
            slot->in_use = 0;
            return -QM_ERROR;
        }
        timer->hdl = &slot->oneshot;
    }

    return QM_EOK;
}

int qm_timer_new_ext(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat,
                     unsigned char auto_run)
{
    int ret = qm_timer_new(timer, cb, arg, ms, repeat);

    if (ret == QM_EOK && auto_run) {
        qm_timer_start(timer);
    }
    return ret;
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return (timer != NULL && timer->hdl != NULL) ? 1 : 0;
}

static qm_bk_timer_slot_t *qm_bk_timer_slot_from_hdl(void *hdl)
{
    int i;

    for (i = 0; i < QM_BK_TIMER_SLOTS; i++) {
        if (!g_qm_timers[i].in_use) {
            continue;
        }
        if (g_qm_timers[i].repeat && hdl == &g_qm_timers[i].periodic) {
            return &g_qm_timers[i];
        }
        if (!g_qm_timers[i].repeat && hdl == &g_qm_timers[i].oneshot) {
            return &g_qm_timers[i];
        }
    }
    return NULL;
}

int qm_timer_free(qm_timer_t *timer)
{
    qm_bk_timer_slot_t *slot;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    slot = qm_bk_timer_slot_from_hdl(timer->hdl);
    if (slot == NULL) {
        return -QM_ERROR;
    }

    if (slot->repeat) {
        rtos_stop_timer(&slot->periodic);
        rtos_deinit_timer(&slot->periodic);
    } else {
        rtos_stop_oneshot_timer(&slot->oneshot);
        rtos_deinit_oneshot_timer(&slot->oneshot);
    }

    slot->in_use = 0;
    timer->hdl = NULL;
    return QM_EOK;
}

int qm_timer_start(qm_timer_t *timer)
{
    qm_bk_timer_slot_t *slot;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    slot = qm_bk_timer_slot_from_hdl(timer->hdl);
    if (slot == NULL) {
        return -QM_ERROR;
    }

    if (slot->repeat) {
        return (rtos_start_timer(&slot->periodic) == kNoErr) ? QM_EOK : -QM_ERROR;
    }
    return (rtos_start_oneshot_timer(&slot->oneshot) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    return qm_timer_start(timer);
}

int qm_timer_stop(qm_timer_t *timer)
{
    qm_bk_timer_slot_t *slot;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    slot = qm_bk_timer_slot_from_hdl(timer->hdl);
    if (slot == NULL) {
        return -QM_ERROR;
    }

    if (slot->repeat) {
        return (rtos_stop_timer(&slot->periodic) == kNoErr) ? QM_EOK : -QM_ERROR;
    }
    return (rtos_stop_oneshot_timer(&slot->oneshot) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_timer_stop_from_isr(qm_timer_t *timer)
{
    return qm_timer_stop(timer);
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    qm_bk_timer_slot_t *slot;
    qm_timer_cb_t cb;
    void *arg;
    int repeat;

    if (timer == NULL || timer->hdl == NULL || ms <= 0) {
        return -QM_EINVAL;
    }

    slot = qm_bk_timer_slot_from_hdl(timer->hdl);
    if (slot == NULL) {
        return -QM_ERROR;
    }

    cb = slot->cb;
    arg = slot->arg;
    repeat = slot->repeat;
    qm_timer_stop(timer);
    qm_timer_free(timer);
    return qm_timer_new(timer, cb, arg, ms, repeat);
}

int qm_timer_change_from_isr(qm_timer_t *timer, int ms)
{
    return qm_timer_change(timer, ms);
}

int qm_timer_reset(qm_timer_t *timer)
{
    qm_bk_timer_slot_t *slot;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    slot = qm_bk_timer_slot_from_hdl(timer->hdl);
    if (slot == NULL || !slot->repeat) {
        return -QM_ERROR;
    }

    return (rtos_reload_timer(&slot->periodic) == kNoErr) ? QM_EOK : -QM_ERROR;
}

int qm_timer_reset_from_isr(qm_timer_t *timer)
{
    return qm_timer_reset(timer);
}

#endif /* CONFIG_QM_OS_TIMER_SUPPORT */

#endif /* CONFIG_QM_OS_SUPPORT */

void *qm_calloc(unsigned int nitems, unsigned int size)
{
    return os_zalloc((size_t)nitems * size);
}

void *qm_aligned_alloc(unsigned int alignment, unsigned int size)
{
    (void)alignment;
    return os_malloc(size);
}

void *qm_malloc(unsigned int size)
{
    return os_malloc(size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return os_realloc(mem, size);
}

void qm_free(void *mem)
{
    os_free(mem);
}

void *qm_psram_malloc(unsigned int size)
{
    return psram_malloc(size);
}

void *qm_psram_calloc(unsigned int nitems, unsigned int size)
{
    return psram_zalloc((size_t)nitems * size);
}

void *qm_psram_realloc(void *mem, unsigned int size)
{
    (void)mem;
    (void)size;
    return NULL;
}

uint32_t qm_free_mem_get(void)
{
    return (uint32_t)rtos_get_free_heap_size();
}

uint32_t qm_now_ms(void)
{
    return rtos_get_time();
}

int qm_msleep(int ms)
{
    rtos_delay_milliseconds((uint32_t)ms);
    return QM_EOK;
}

int qm_usleep(int us)
{
    if (us >= 1000) {
        rtos_delay_milliseconds((uint32_t)(us / 1000));
    }
    return QM_EOK;
}

int qm_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        os_printf("%s", buf);
    }
    return n;
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = vsnprintf(str, (size_t)len, fmt, ap);
    va_end(ap);
    return rc;
}

static uint32_t s_rand_next;

void qm_srandom(uint32_t seed)
{
    s_rand_next = seed;
}

uint32_t qm_random_get(uint32_t region)
{
    if (region == 0) {
        return 0;
    }
    s_rand_next = s_rand_next * 1103515245u + 12345u;
    return s_rand_next % region;
}

void qm_init(void)
{
    (void)mbedtls_platform_set_calloc_free(bk_mbedtls_psram_calloc, bk_mbedtls_psram_free);
}
