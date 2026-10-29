/*
 * qmos_jl.c - QUMI OS Abstraction for JieLi (WL82/AC79) Platform
 *
 * Maps QUMI OS primitives to JieLi RTOS API.
 * Reference: fw-AC79_AIoT_SDK/include_lib/system/os/os_api.h
 *
 * Tick period: 1 tick = 10ms (os_time_dly unit)
 */

#define BOOL_DEFINE_CONFLICT

#include "qm_kernel.h"
#include "qm_errno.h"

#include "os/os_api.h"
#include "system/timer.h"
#include "asm/irq.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* AC7911BX's linked system library exports the remaining heap query under
 * get_malloc_remain_heap_size(); this is the JieLi heap used by malloc(). */
extern unsigned int get_malloc_remain_heap_size(void);
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

extern void *memalign(size_t alignment, size_t size);

/* ------------------------------------------------------------------ */
/* Logging                                                              */
/* ------------------------------------------------------------------ */

/*
 * JieLi's SDK printf/vprintf is a minimal implementation that ignores the
 * precision field of conversions such as "%.*s". With it, logging a length
 * bounded, non-NUL-terminated buffer keeps printing past the requested length
 * and dumps trailing fill bytes as garbage (e.g. the "¥¥¥" tails seen for
 * WebSocket frames). Route qm_printf through the project's portable tiny printf
 * (utility/sprintf/qm_sprintf.c, mpaland), which honors width/precision, and
 * emit the formatted bytes via JieLi's raw console output.
 *
 * vsnprintf_ is always compiled (the qm_sprintf.c body is unconditional); only
 * its declaration/macro in qm_sprintf.h is gated by CONFIG_QM_SPRINTF_SUPPORT,
 * so we declare what we need locally to avoid flipping that switch (which would
 * collide with the platform QMOS implementation). Float/exponential support
 * for the engine is enabled via qm_config.h.
 */
extern int vsnprintf_(char *buffer, size_t count, const char *format, va_list va);
int qm_snprintf(char *str, const int len, const char *format, ...);
/* putbyte() emits one raw byte to the JieLi console. NOTE: do NOT use
 * put_buf(), which formats the buffer as a hex dump rather than raw text. */
extern void putbyte(char a);
extern int qm_debug_transport_is_ready(void);
extern int qm_debug_transport_write(const void *data, uint32_t size,
                                    uint32_t timeout);

#define QM_JL_PRINTF_STACK_BUF 256

int qm_printf(const char *fmt, ...)
{
    char stack_buf[QM_JL_PRINTF_STACK_BUF];
    char *buf = stack_buf;
    va_list args;
    int len;
    int i;

#if CONFIG_QM_LOG_SUPPORT == 1
    /*
     * qm_printf() is also the common sink for alarm_app application logs.
     * Keeping the mute check here makes QM_LL_NONE silence both QM_LOGx()
     * and migrated application prints, while AT replies still use their
     * channel send callback and remain available during production test.
     */
    extern unsigned int qm_log_level;
    if (qm_log_level == 0) {
        return 0;
    }
#endif

    va_start(args, fmt);
    len = vsnprintf_(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    if (len <= 0) {
        return len;
    }

    /* vsnprintf_ returns the length that WOULD have been written (C99). When
     * the line is longer than the stack window, reformat into an exact-size
     * heap buffer so long logs (e.g. raw frame dumps) are not truncated. */
    if ((size_t)len >= sizeof(stack_buf)) {
        char *heap_buf = (char *)malloc((size_t)len + 1);
        if (heap_buf) {
            va_start(args, fmt);
            vsnprintf_(heap_buf, (size_t)len + 1, fmt, args);
            va_end(args);
            buf = heap_buf;
        } else {
            /* Out of memory: emit the truncated stack content. */
            len = (int)sizeof(stack_buf) - 1;
        }
    }

    if (qm_debug_transport_is_ready()) {
        qm_debug_transport_write(buf, (uint32_t)len, QM_WAIT_FOREVER);
    } else {
        for (i = 0; i < len; ++i) {
            putbyte(buf[i]);
        }
    }

    if (buf != stack_buf) {
        free(buf);
    }
    return len;
}

int qm_putchar(int ch)
{
    return qm_printf("%c", ch);
}

void qm_put_buf(const unsigned char *buf, int len)
{
    int i;

    if (buf == NULL || len <= 0) {
        return;
    }

    for (i = 0; i < len; ++i) {
        qm_printf("%02X ", buf[i]);
        if ((i & 0x0f) == 0x0f) {
            qm_printf("\r\n");
        }
    }
    if ((len & 0x0f) != 0) {
        qm_printf("\r\n");
    }
}

/* ------------------------------------------------------------------ */
/* Time                                                                 */
/* ------------------------------------------------------------------ */

uint32_t qm_now_ms(void)
{
    return (uint32_t)(os_time_get() * 10);
}

int qm_msleep(int ms)
{
    if (ms <= 0) {
        return 0;
    }
    /* os_time_dly() unit: 1 tick = 10 ms; round up without overflow. */
    os_time_dly(ms / 10 + ((ms % 10) != 0));
    return 0;
}

static int qm_timeout_to_jl_ticks(unsigned int timeout_ms)
{
    if (timeout_ms == QM_WAIT_FOREVER) {
        return 0;
    }
    unsigned int ticks = timeout_ms / 10 + ((timeout_ms % 10) != 0);
    return ticks > (unsigned int)INT_MAX ? INT_MAX : (int)ticks;
}

int qm_usleep(int us)
{
    if (us <= 0) return 0;
    delay_us((unsigned int)us);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Memory                                                               */
/* ------------------------------------------------------------------ */

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *qm_calloc(unsigned int nitems, unsigned int size)
{
    return calloc(nitems, size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void *qm_aligned_alloc(unsigned int alignment, unsigned int size)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || size == 0) {
        return NULL;
    }
    return memalign((size_t)alignment, (size_t)size);
}

void qm_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

/* JL heap already lives in SDRAM; there is no separate PSRAM allocator on
 * this platform. Returning NULL makes callers fall back to qm_malloc(). */
void *qm_psram_malloc(unsigned int size)
{
    (void)size;
    return NULL;
}

void *qm_psram_calloc(unsigned int nitems, unsigned int size)
{
    (void)nitems;
    (void)size;
    return NULL;
}

void *qm_psram_realloc(void *mem, unsigned int size)
{
    (void)mem;
    (void)size;
    return NULL;
}

uint32_t qm_free_mem_get(void)
{
    size_t free_size = (size_t)get_malloc_remain_heap_size();

    return free_size > UINT32_MAX ? UINT32_MAX : (uint32_t)free_size;
}

/* ------------------------------------------------------------------ */
/* System                                                               */
/* ------------------------------------------------------------------ */

void qm_reboot(void)
{
    qm_printf("QUMI: System Reboot!\n");
    cpu_reset();
}

const char *qm_version_get(void)
{
    return "JL-AC7911BX";
}

/* ------------------------------------------------------------------ */
/* Mutex                                                                */
/* ------------------------------------------------------------------ */

int qm_mutex_new(qm_mutex_t *mutex)
{
    if (!mutex) return -1;
    OS_MUTEX *m = (OS_MUTEX *)malloc(sizeof(OS_MUTEX));
    if (!m) return -1;
    if (os_mutex_create(m) != 0) {
        free(m);
        return -1;
    }
    mutex->hdl = m;
    return 0;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if (mutex && mutex->hdl) {
        os_mutex_del((OS_MUTEX *)mutex->hdl, OS_DEL_ALWAYS);
        free(mutex->hdl);
        mutex->hdl = NULL;
    }
    return 0;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int timeout_ms)
{
    if (!mutex || !mutex->hdl) return -1;
    if (timeout_ms == QM_NO_WAIT) {
        return os_mutex_accept((OS_MUTEX *)mutex->hdl) == 0 ? 0 : -1;
    }
    return os_mutex_pend((OS_MUTEX *)mutex->hdl,
                         qm_timeout_to_jl_ticks(timeout_ms)) == 0 ? 0 : -1;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if (!mutex || !mutex->hdl) return -1;
    return os_mutex_post((OS_MUTEX *)mutex->hdl) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Semaphore                                                            */
/* ------------------------------------------------------------------ */

int qm_sem_new(qm_sem_t *sem, int count)
{
    if (!sem) return -1;
    OS_SEM *s = (OS_SEM *)malloc(sizeof(OS_SEM));
    if (!s) return -1;
    if (os_sem_create(s, count) != 0) {
        free(s);
        return -1;
    }
    sem->hdl = s;
    return 0;
}

int qm_sem_free(qm_sem_t *sem)
{
    if (sem && sem->hdl) {
        os_sem_del((OS_SEM *)sem->hdl, OS_DEL_ALWAYS);
        free(sem->hdl);
        sem->hdl = NULL;
    }
    return 0;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int timeout_ms)
{
    if (!sem || !sem->hdl) return -1;
    if (timeout_ms == QM_NO_WAIT) {
        return os_sem_accept((OS_SEM *)sem->hdl) == 0 ? 0 : -1;
    }
    return os_sem_pend((OS_SEM *)sem->hdl,
                       qm_timeout_to_jl_ticks(timeout_ms)) == 0 ? 0 : -1;
}

int qm_sem_signal(qm_sem_t *sem)
{
    if (!sem || !sem->hdl) return -1;
    return os_sem_post((OS_SEM *)sem->hdl) == 0 ? 0 : -1;
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    /* JieLi exposes no separate ISR semaphore primitive; os_sem_post is the
     * non-blocking operation and does not allocate or wait. */
    return qm_sem_signal(sem);
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return (sem && sem->hdl && os_sem_valid((OS_SEM *)sem->hdl)) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Event group                                                          */
/* ------------------------------------------------------------------ */

int qm_event_new(qm_event_t *event)
{
    OS_EVENT_GRP *grp = NULL;
    if (!event) return -1;
    if (os_eventgroup_create(&grp, 0) != 0 || !grp) {
        return -1;
    }
    event->hdl = grp;
    return 0;
}

int qm_event_free(qm_event_t *event)
{
    if (!event || !event->hdl) return -1;
    int ret = os_eventgroup_delete((OS_EVENT_GRP *)event->hdl, OS_DEL_ALWAYS);
    event->hdl = NULL;
    return ret == 0 ? 0 : -1;
}

int qm_event_is_valid(qm_event_t *event)
{
    return event != NULL && event->hdl != NULL;
}

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt,
                 unsigned int *actl_event_bits, unsigned int timeout_ms)
{
    if (!qm_event_is_valid(event) || !actl_event_bits) return -QM_EINVAL;

    int clear_on_exit;
    int wait_for_all;
    switch (opt) {
    case QM_EVENT_OR:
        clear_on_exit = 0;
        wait_for_all = 0;
        break;
    case QM_EVENT_OR_CLEAR:
        clear_on_exit = 1;
        wait_for_all = 0;
        break;
    case QM_EVENT_AND:
        clear_on_exit = 0;
        wait_for_all = 1;
        break;
    case QM_EVENT_AND_CLEAR:
        clear_on_exit = 1;
        wait_for_all = 1;
        break;
    default:
        return -1;
    }

    int wait_ms;
    if (timeout_ms == QM_WAIT_FOREVER) {
        wait_ms = -1;
    } else {
        wait_ms = timeout_ms > (unsigned int)INT_MAX ? INT_MAX : (int)timeout_ms;
    }
    int bits = os_eventgroup_wait_bits((OS_EVENT_GRP *)event->hdl,
                                       (int)wait_event_bits,
                                       clear_on_exit, wait_for_all, wait_ms);
    unsigned int actual_bits = (unsigned int)bits;
    *actl_event_bits = actual_bits;
    return ((actual_bits & wait_event_bits) == wait_event_bits ||
            (!wait_for_all && (actual_bits & wait_event_bits))) ? 0 : -1;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    if (!event || !event->hdl) return -1;
    (void)os_eventgroup_set_bits((OS_EVENT_GRP *)event->hdl, (int)event_bits);
    return 0;
}

int qm_event_clear(qm_event_t *event, unsigned int event_bits)
{
    if (!event || !event->hdl) return -1;
    (void)os_eventgroup_clear_bits((OS_EVENT_GRP *)event->hdl, (int)event_bits);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Queue                                                                */
/*                                                                      */
/* JieLi os_q_post / os_q_pend pass one void* pointer per message.       */
/* Queue payloads are kept in fixed slots so send-from-ISR never allocates. */
/* ------------------------------------------------------------------ */

typedef struct {
    OS_QUEUE q;
    unsigned int item_size;
    unsigned int queue_len;
    unsigned char *storage;
    unsigned char *used;
    unsigned short *free_indices;
    volatile unsigned int free_count;
} jl_q_t;

static int jl_queue_slot_take(jl_q_t *jq, unsigned int *index)
{
    unsigned int flags = 0;

    local_irq_save(flags);
    if (jq->free_count == 0) {
        local_irq_restore(flags);
        return -1;
    }
    *index = jq->free_indices[--jq->free_count];
    jq->used[*index] = 1;
    local_irq_restore(flags);
    return 0;
}

static void jl_queue_slot_release(jl_q_t *jq, unsigned int index)
{
    unsigned int flags = 0;
    if (index >= jq->queue_len) return;
    local_irq_save(flags);
    if (jq->used[index] && jq->free_count < jq->queue_len) {
        jq->used[index] = 0;
        jq->free_indices[jq->free_count++] = (unsigned short)index;
    }
    local_irq_restore(flags);
}

static int jl_queue_slot_index(jl_q_t *jq, const void *slot, unsigned int *index)
{
    uintptr_t base = (uintptr_t)jq->storage;
    uintptr_t p = (uintptr_t)slot;
    uintptr_t end = base + (uintptr_t)jq->item_size * jq->queue_len;

    if (!slot || p < base || p >= end || jq->item_size == 0 ||
        ((p - base) % jq->item_size) != 0) {
        return -1;
    }
    *index = (unsigned int)((p - base) / jq->item_size);
    return (*index < jq->queue_len) ? 0 : -1;
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    if (!queue || queue_len == 0 || queue_len > 0xffffU || queue_size == 0) return -1;
    size_t total = (size_t)queue_len * queue_size;
    if (total / queue_size != queue_len) return -1;
    jl_q_t *jq = (jl_q_t *)malloc(sizeof(jl_q_t));
    if (!jq) return -1;
    jq->item_size = queue_size;
    jq->queue_len = queue_len;
    jq->storage = (unsigned char *)malloc(total);
    jq->used = (unsigned char *)calloc(queue_len, sizeof(unsigned char));
    jq->free_indices = (unsigned short *)malloc((size_t)queue_len * sizeof(unsigned short));
    jq->free_count = queue_len;
    if (jq->free_indices) {
        unsigned int i;
        for (i = 0; i < queue_len; i++) {
            jq->free_indices[i] = (unsigned short)i;
        }
    }
    if (!jq->storage || !jq->used || !jq->free_indices) {
        free(jq->storage);
        free(jq->used);
        free(jq->free_indices);
        free(jq);
        return -1;
    }
    if (os_q_create(&jq->q, (QS)queue_len) != 0) {
        free(jq->storage);
        free(jq->used);
        free(jq->free_indices);
        free(jq);
        return -1;
    }
    queue->hdl = jq;
    return 0;
}

int qm_queue_free(qm_queue_t *queue)
{
    if (queue && queue->hdl) {
        jl_q_t *jq = (jl_q_t *)queue->hdl;
        os_q_del(&jq->q, OS_DEL_ALWAYS);
        free(jq->storage);
        free(jq->used);
        free(jq->free_indices);
        free(jq);
        queue->hdl = NULL;
    }
    return 0;
}

static int jl_queue_send_common(qm_queue_t *queue, void *msg, unsigned int size)
{
    if (!queue || !queue->hdl || !msg) return -1;
    jl_q_t *jq = (jl_q_t *)queue->hdl;
    unsigned int index;
    if (jl_queue_slot_take(jq, &index) != 0) return -1;
    void *slot = jq->storage + (size_t)index * jq->item_size;
    memset(slot, 0, jq->item_size);
    memcpy(slot, msg, size < jq->item_size ? size : jq->item_size);
    if (os_q_post(&jq->q, slot) != 0) {
        jl_queue_slot_release(jq, index);
        return -1;
    }
    return 0;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    return jl_queue_send_common(queue, msg, size);
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return jl_queue_send_common(queue, msg, size);
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int timeout_ms)
{
    if (!queue || !queue->hdl) return -1;
    jl_q_t *jq = (jl_q_t *)queue->hdl;

    void *val = NULL;
    int ret = (timeout_ms == QM_NO_WAIT)
              ? os_q_accept(&jq->q, &val)
              : os_q_pend(&jq->q, qm_timeout_to_jl_ticks(timeout_ms), &val);
    if (ret != 0) return -1;

    unsigned int index;
    if (jl_queue_slot_index(jq, val, &index) != 0) return -1;
    if (msg) {
        memcpy(msg, val, jq->item_size);
    }
    jl_queue_slot_release(jq, index);
    if (size) {
        *size = jq->item_size;
    }
    return 0;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    if (!queue || !queue->hdl) return 0;
    jl_q_t *jq = (jl_q_t *)queue->hdl;
    return os_q_valid(&jq->q) ? 1 : 0;
}

bool_t qm_queue_is_empty(qm_queue_t *queue)
{
    if (!queue || !queue->hdl) return 1;
    jl_q_t *jq = (jl_q_t *)queue->hdl;
    /* os_api.h exposes no "queue empty" query: os_q_query() only reports
     * whether other tasks are blocked waiting on the queue, not the message
     * count. Derive the answer from the wrapper's slot accounting instead —
     * all slots free means the underlying os_q holds no pending message. */
    return (jq->free_count == jq->queue_len) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Task                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    void (*fn)(void *);
    void *arg;
} jl_task_ctx_t;

#define JL_TASK_POOL_SIZE 64
#define JL_TASK_NAME_MAX  32
/* qm_task_t::hdl is opaque. Encode the reusable pool slot and a monotonically
 * increasing generation so a completed task handle can never name a later
 * task that reuses the same slot. */
#define JL_TASK_HANDLE_SLOT_BITS 6U
#define JL_TASK_HANDLE_SLOT_MASK ((uintptr_t)JL_TASK_POOL_SIZE - 1U)
#define JL_TASK_HANDLE_GENERATION_MAX (UINTPTR_MAX >> JL_TASK_HANDLE_SLOT_BITS)

#if JL_TASK_POOL_SIZE != (1U << JL_TASK_HANDLE_SLOT_BITS)
#error "JL_TASK_POOL_SIZE must match JL_TASK_HANDLE_SLOT_BITS"
#endif

typedef struct {
    int used;
    int closing;
    int creator_pending;
    int exit_pending;
    int pid;
    uintptr_t generation;
    char requested[JL_TASK_NAME_MAX];
    char actual[JL_TASK_NAME_MAX];
} jl_task_slot_t;

static jl_task_slot_t g_task_slots[JL_TASK_POOL_SIZE];
static OS_MUTEX g_task_slots_mutex_obj;
static OS_MUTEX *g_task_slots_mutex;
static uintptr_t g_task_generation;

static int jl_task_pool_lock(void)
{
    if (!g_task_slots_mutex) return -1;
    return os_mutex_pend(g_task_slots_mutex, 0) == 0 ? 0 : -1;
}

static void jl_task_pool_unlock(void)
{
    if (g_task_slots_mutex) {
        os_mutex_post(g_task_slots_mutex);
    }
}

static jl_task_slot_t *jl_task_slot_find(const char *name)
{
    unsigned int i;
    if (!name) return NULL;
    for (i = 0; i < JL_TASK_POOL_SIZE; i++) {
        if (g_task_slots[i].used && strcmp(g_task_slots[i].actual, name) == 0) {
            return &g_task_slots[i];
        }
    }
    return NULL;
}

static uintptr_t jl_task_next_generation_locked(void)
{
    if (g_task_generation >= JL_TASK_HANDLE_GENERATION_MAX) {
        g_task_generation = 1U;
    } else {
        g_task_generation++;
    }
    return g_task_generation;
}

static void *jl_task_handle_make(const jl_task_slot_t *slot)
{
    uintptr_t index;
    uintptr_t token;

    if (slot == NULL || slot->generation == 0U) {
        return NULL;
    }
    index = (uintptr_t)(slot - g_task_slots);
    if (index >= JL_TASK_POOL_SIZE) {
        return NULL;
    }
    token = (slot->generation << JL_TASK_HANDLE_SLOT_BITS) | index;
    return token != 0U ? (void *)token : NULL;
}

/* Caller holds g_task_slots_mutex. The returned slot is valid only while the
 * lock remains held. */
static jl_task_slot_t *jl_task_slot_from_handle_locked(void *handle)
{
    uintptr_t token = (uintptr_t)handle;
    uintptr_t index;
    uintptr_t generation;
    jl_task_slot_t *slot;

    if (token == 0U) {
        return NULL;
    }
    index = token & JL_TASK_HANDLE_SLOT_MASK;
    generation = token >> JL_TASK_HANDLE_SLOT_BITS;
    if (generation == 0U || index >= JL_TASK_POOL_SIZE) {
        return NULL;
    }
    slot = &g_task_slots[index];
    return slot->used && slot->generation == generation ? slot : NULL;
}

/* Returns 1 for a live task, 0 for a completed/stale token, and -1 when the
 * pool cannot be inspected. */
static int jl_task_handle_is_active(void *handle)
{
    int active;

    if (jl_task_pool_lock() != 0) {
        return -1;
    }
    active = jl_task_slot_from_handle_locked(handle) != NULL ? 1 : 0;
    jl_task_pool_unlock();
    return active;
}

static jl_task_slot_t *jl_task_slot_alloc(const char *name)
{
    unsigned int i, serial;
    if (!name || !*name || jl_task_pool_lock() != 0) return NULL;
    for (i = 0; i < JL_TASK_POOL_SIZE; i++) {
        if (!g_task_slots[i].used) break;
    }
    if (i == JL_TASK_POOL_SIZE) {
        jl_task_pool_unlock();
        return NULL;
    }
    jl_task_slot_t *slot = &g_task_slots[i];
    memset(slot, 0, sizeof(*slot));
    slot->generation = jl_task_next_generation_locked();
    strncpy(slot->requested, name, sizeof(slot->requested) - 1);
    for (serial = 0; serial < JL_TASK_POOL_SIZE * 2; serial++) {
        if (serial == 0) {
            qm_snprintf(slot->actual, sizeof(slot->actual), "%s", slot->requested);
        } else {
            qm_snprintf(slot->actual, sizeof(slot->actual), "%.*s_%u",
                        (int)(sizeof(slot->actual) - 8), slot->requested, serial);
        }
        if (!jl_task_slot_find(slot->actual) || jl_task_slot_find(slot->actual) == slot) {
            slot->used = 1;
            slot->creator_pending = 1;
            jl_task_pool_unlock();
            return slot;
        }
    }
    memset(slot, 0, sizeof(*slot));
    jl_task_pool_unlock();
    return NULL;
}

/* This is used only before thread_rpc() succeeds, so no completion callback
 * can still reference the slot. */
static void jl_task_slot_discard(jl_task_slot_t *slot)
{
    if (jl_task_pool_lock() != 0) return;
    if (slot != NULL && slot->used && slot->creator_pending) {
        memset(slot, 0, sizeof(*slot));
    }
    jl_task_pool_unlock();
}

static int jl_task_start_immediately(void)
{
    return 1;
}

static void jl_task_complete(void *arg)
{
    jl_task_slot_t *slot;

    if (jl_task_pool_lock() != 0) {
        return;
    }
    slot = jl_task_slot_from_handle_locked(arg);
    if (slot != NULL && !slot->closing) {
        if (slot->creator_pending) {
            slot->exit_pending = 1;
        } else {
            memset(slot, 0, sizeof(*slot));
        }
    }
    jl_task_pool_unlock();
}

static int jl_task_is_current(void *handle, const char *current_name)
{
    jl_task_slot_t *slot;
    int is_current = 0;

    if (handle == NULL || jl_task_pool_lock() != 0) {
        return 0;
    }
    slot = jl_task_slot_from_handle_locked(handle);
    /* QUMI task names are unique. Native SDK tasks such as lcd_init do not
     * have thread_rpc/thread_fork metadata, so get_cur_thread_pid() can yield
     * an invalid pointer when this query is called from those tasks. */
    if (slot != NULL && current_name != NULL &&
        strcmp(slot->actual, current_name) == 0) {
        is_current = 1;
    }
    jl_task_pool_unlock();
    return is_current;
}

static int jl_task_join(void *handle)
{
    jl_task_slot_t *slot;

    if (handle == NULL || jl_task_pool_lock() != 0) {
        return -1;
    }

    slot = jl_task_slot_from_handle_locked(handle);
    if (slot == NULL) {
        jl_task_pool_unlock();
        return 0;
    }
    if (slot->closing) {
        jl_task_pool_unlock();
        return -1;
    }
    /* Keep the slot and its pid stable while thread_kill() waits. */
    slot->closing = 1;
    jl_task_pool_unlock();

    thread_kill(&slot->pid, KILL_WAIT);

    if (jl_task_pool_lock() == 0) {
        if (jl_task_slot_from_handle_locked(handle) == slot) {
            memset(slot, 0, sizeof(*slot));
        }
        jl_task_pool_unlock();
    }
    return 0;
}

static void jl_task_wrapper(void *p)
{
    jl_task_ctx_t *ctx = (jl_task_ctx_t *)p;
    void (*fn)(void *) = NULL;
    void *arg = NULL;

    if (ctx) {
        fn = ctx->fn;
        arg = ctx->arg;
        free(ctx);
    }
    if (fn) {
        fn(arg);
    }
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                int stack_size, int prio)
{
    int task_active;

    if (!name || !fn || stack_size < 4) return -1;
    if (task != NULL && task->hdl != NULL) {
        task_active = jl_task_handle_is_active(task->hdl);
        if (task_active != 0) {
            return -1;
        }
        task->hdl = NULL;
    }

    jl_task_slot_t *slot = jl_task_slot_alloc(name);
    if (!slot) return -1;
    void *handle = jl_task_handle_make(slot);
    if (handle == NULL) {
        jl_task_slot_discard(slot);
        return -1;
    }
    if (task) {
        task->hdl = handle;
    }

    jl_task_ctx_t *ctx = (jl_task_ctx_t *)malloc(sizeof(jl_task_ctx_t));
    if (!ctx) {
        jl_task_slot_discard(slot);
        if (task) task->hdl = NULL;
        return -1;
    }
    ctx->fn  = fn;
    ctx->arg = arg;
    /*
     * thread_rpc stack parameter is in WORDs (4 bytes).
     * sys_timer_add() dispatches callbacks by posting to the task that created
     * the timer, so QUMI tasks need a private task queue for delayed work.
     */
    enum { JL_QM_TASK_Q_SIZE = 32 };
    int ret = thread_rpc(jl_task_start_immediately, slot->actual, prio,
                         stack_size / 4, JL_QM_TASK_Q_SIZE, &slot->pid,
                         jl_task_wrapper, ctx, jl_task_complete, handle);
    if (ret != 0) {
        free(ctx);
        jl_task_slot_discard(slot);
        if (task) task->hdl = NULL;
        return -1;
    }
    if (jl_task_pool_lock() == 0) {
        if (jl_task_slot_from_handle_locked(handle) == slot) {
            slot->creator_pending = 0;
            if (slot->exit_pending) {
                memset(slot, 0, sizeof(*slot));
            }
        }
        jl_task_pool_unlock();
    }
    return 0;
}

int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                        int stack_size, int prio, int core_id)
{
    /* Single-core contract: core 0 and no-affinity values use the normal
     * creator; a request for another core cannot be honored by this SDK. */
    if (core_id > 0) return -1;
    return qm_task_new(task, name, fn, arg, stack_size, prio);
}

qm_bool qm_task_is_current(const qm_task_t *task)
{
    return task != NULL && jl_task_is_current(task->hdl, os_current_task()) ?
           QM_TRUE : QM_FALSE;
}

int qm_task_exit(qm_task_t *task)
{
    if (task == NULL) {
        int *pid = get_cur_thread_pid();

        if (pid == NULL) {
            return -1;
        }
        /* thread_fork_kill performs the force-delete from another thread. */
        thread_kill(pid, KILL_FORCE);
        for (;;) {
            os_time_dly(1);
        }
    }

    void *handle = task->hdl;
    if (handle == NULL) {
        return 0;
    }
    if (jl_task_is_current(handle, os_current_task())) {
        task->hdl = NULL;
        return qm_task_exit(NULL);
    }

    int ret = jl_task_join(handle);
    if (ret == 0) task->hdl = NULL;
    return ret;
}

/* ------------------------------------------------------------------ */
/* Timer                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    qm_timer_cb_t  cb;
    void          *arg;
    qm_timer_t    *qm_timer;
    u32            timer_id;
    int            is_repeat;
    int            ms;
    int            is_used;
    int            callback_running;
    int            pending_free;
    volatile int   isr_pending;
} jl_timer_ctx_t;

#define QM_TIMER_MAX_NUM 24
#define QM_TIMER_CMD_QUEUE_LEN 8
static jl_timer_ctx_t g_timer_pool[QM_TIMER_MAX_NUM] = {0};
static OS_MUTEX g_timer_pool_mutex_obj;
static OS_MUTEX *g_timer_pool_mutex = NULL;
static OS_QUEUE g_timer_cmd_queue;
static qm_task_t g_timer_cmd_task;
static int g_timer_cmd_queue_ready;
static int g_timer_cmd_ready;

int qm_timer_start(qm_timer_t *timer);

static int jl_timer_pool_lock(void)
{
    if (!g_timer_pool_mutex) return -1;
    return os_mutex_pend(g_timer_pool_mutex, 0) == 0 ? 0 : -1;
}

static void jl_timer_pool_unlock(void)
{
    if (g_timer_pool_mutex) os_mutex_post(g_timer_pool_mutex);
}

static void jl_timer_cmd_task_entry(void *arg)
{
    (void)arg;
    for (;;) {
        jl_timer_ctx_t *ctx = NULL;
        qm_timer_t *timer = NULL;
        int valid = 0;

        if (os_q_pend(&g_timer_cmd_queue, 0, &ctx) != 0 || !ctx) {
            continue;
        }
        if (jl_timer_pool_lock() == 0) {
            if (ctx->is_used && ctx->isr_pending) {
                if (ctx->pending_free) {
                    ctx->isr_pending = 0;
                    memset(ctx, 0, sizeof(*ctx));
                } else {
                    timer = ctx->qm_timer;
                    valid = timer != NULL;
                }
            }
            jl_timer_pool_unlock();
        }
        if (valid) {
            (void)qm_timer_start(timer);
            if (jl_timer_pool_lock() == 0) {
                ctx->isr_pending = 0;
                if (ctx->pending_free) {
                    memset(ctx, 0, sizeof(*ctx));
                }
                jl_timer_pool_unlock();
            }
        }
    }
}

static void jl_timer_wrapper(void *priv)
{
    jl_timer_ctx_t *ctx = (jl_timer_ctx_t *)priv;
    qm_timer_cb_t cb;
    qm_timer_t *timer;
    void *arg;
    u16 timer_id;
    int repeat;
    if (!ctx || jl_timer_pool_lock() != 0) return;
    if (!ctx->is_used || !ctx->cb || !ctx->timer_id) {
        jl_timer_pool_unlock();
        return;
    }
    cb = ctx->cb;
    timer = ctx->qm_timer;
    arg = ctx->arg;
    timer_id = (u16)ctx->timer_id;
    repeat = ctx->is_repeat;
    ctx->callback_running = 1;
    if (!repeat) ctx->timer_id = 0;
    jl_timer_pool_unlock();

    if (!repeat) sys_timer_del(timer_id);
    cb(timer, arg);

    if (jl_timer_pool_lock() == 0) {
        ctx->callback_running = 0;
        if (ctx->pending_free) {
            memset(ctx, 0, sizeof(*ctx));
        }
        jl_timer_pool_unlock();
    }
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{
    if (!timer || !cb || ms <= 0) return -1;
    jl_timer_ctx_t *ctx = NULL;
    if (jl_timer_pool_lock() != 0) return -1;
    for (int i = 0; i < QM_TIMER_MAX_NUM; i++) {
        if (!g_timer_pool[i].is_used) {
            ctx = &g_timer_pool[i];
            break;
        }
    }
    if (!ctx) {
        jl_timer_pool_unlock();
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_used = 1;
    ctx->cb       = cb;
    ctx->arg      = arg;
    ctx->qm_timer = timer;
    ctx->is_repeat = repeat;
    ctx->ms       = ms;
    ctx->timer_id = 0;
    timer->hdl    = ctx;
    jl_timer_pool_unlock();
    return 0;
}

int qm_timer_start(qm_timer_t *timer)
{
    if (!timer || !timer->hdl) return -1;
    jl_timer_ctx_t *ctx = (jl_timer_ctx_t *)timer->hdl;

    if (jl_timer_pool_lock() != 0) return -1;
    if (!ctx->is_used || !ctx->cb || ctx->ms <= 0) {
        jl_timer_pool_unlock();
        return -1;
    }
    if (ctx->timer_id == 0) {
        /*
         * sys_timer_add() dispatches the callback to the task that creates the
         * timer. QUMI tasks wait on their own OS queues instead of the JieLi
         * task queue, and some callers are short-lived recovery tasks, so such
         * callbacks may never run or may be deleted with the creator task.
         * Dispatch all QUMI timers through the persistent sys_timer task.
         */
        ctx->timer_id = sys_timer_add_to_task("sys_timer", ctx,
                                              jl_timer_wrapper,
                                              (u32)ctx->ms);
    } else {
        sys_timer_modify(ctx->timer_id, (u32)ctx->ms);
    }
    int ret = (ctx->timer_id != 0) ? 0 : -1;
    jl_timer_pool_unlock();
    return ret;
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    jl_timer_ctx_t *ctx;
    unsigned int flags = 0;

    if (!g_timer_cmd_ready || !timer || !timer->hdl) return -1;
    ctx = (jl_timer_ctx_t *)timer->hdl;
    local_irq_save(flags);
    if (ctx->isr_pending) {
        local_irq_restore(flags);
        return 0;
    }
    ctx->isr_pending = 1;
    local_irq_restore(flags);

    if (os_q_post(&g_timer_cmd_queue, ctx) != 0) {
        local_irq_save(flags);
        ctx->isr_pending = 0;
        local_irq_restore(flags);
        if (jl_timer_pool_lock() == 0) {
            if (ctx->pending_free && !ctx->callback_running) {
                memset(ctx, 0, sizeof(*ctx));
            }
            jl_timer_pool_unlock();
        }
        return -1;
    }
    return 0;
}

int qm_timer_stop(qm_timer_t *timer)
{
    if (!timer || !timer->hdl) return -1;
    jl_timer_ctx_t *ctx = (jl_timer_ctx_t *)timer->hdl;
    if (jl_timer_pool_lock() != 0) return -1;
    if (!ctx->is_used) {
        jl_timer_pool_unlock();
        return -1;
    }
    if (ctx->timer_id) {
        sys_timer_del((u16)ctx->timer_id);
        ctx->timer_id = 0;
    }
    jl_timer_pool_unlock();
    return 0;
}

int qm_timer_free(qm_timer_t *timer)
{
    qm_timer_stop(timer);
    if (timer && timer->hdl) {
        jl_timer_ctx_t *ctx = (jl_timer_ctx_t *)timer->hdl;
        if (jl_timer_pool_lock() == 0) {
            if (ctx->callback_running || ctx->isr_pending) {
                ctx->pending_free = 1;
            } else {
                memset(ctx, 0, sizeof(*ctx));
            }
            jl_timer_pool_unlock();
        }
        timer->hdl = NULL;
    }
    return 0;
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return (timer && timer->hdl) ? 1 : 0;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if (!timer || !timer->hdl || ms <= 0) return -1;
    jl_timer_ctx_t *ctx = (jl_timer_ctx_t *)timer->hdl;
    if (jl_timer_pool_lock() != 0) return -1;
    if (!ctx->is_used) {
        jl_timer_pool_unlock();
        return -1;
    }
    ctx->ms = ms;
    if (ctx->timer_id) {
        sys_timer_modify(ctx->timer_id, (u32)ctx->ms);
        sys_timer_re_run(ctx->timer_id);
    }
    jl_timer_pool_unlock();
    return 0;
}

int qm_timer_new_ext(qm_timer_t *timer, qm_timer_cb_t cb, void *arg,
                     int ms, int repeat, unsigned char auto_run)
{
    int ret = qm_timer_new(timer, cb, arg, ms, repeat);
    if (ret == 0 && auto_run) {
        ret = qm_timer_start(timer);
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* Core initialization stubs required by Qumi / JL SDK                */
/* ------------------------------------------------------------------ */
void qm_init(void)
{
    if (g_timer_cmd_ready) return;

    if (!g_task_slots_mutex) {
        if (os_mutex_create(&g_task_slots_mutex_obj) != 0) return;
        g_task_slots_mutex = &g_task_slots_mutex_obj;
    }
    if (!g_timer_pool_mutex) {
        if (os_mutex_create(&g_timer_pool_mutex_obj) != 0) return;
        g_timer_pool_mutex = &g_timer_pool_mutex_obj;
    }
    if (!g_timer_cmd_queue_ready) {
        if (os_q_create(&g_timer_cmd_queue, QM_TIMER_CMD_QUEUE_LEN) != 0) return;
        g_timer_cmd_queue_ready = 1;
    }

    if (!g_timer_cmd_task.hdl) {
        if (qm_task_new(&g_timer_cmd_task, "qm_timer_cmd", jl_timer_cmd_task_entry,
                        NULL, 1024, 24) != 0) {
            return;
        }
    }
    g_timer_cmd_ready = 1;
}

/* qm_rtc_init(rtc) is implemented in hal/soc/qm_rtc.c (HAL RTC) */

/* Common QUMI helpers belong to the OS port, rather than a network/BLE file. */
int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return (mutex && mutex->hdl &&
            os_mutex_valid((OS_MUTEX *)mutex->hdl)) ? 1 : 0;
}

extern int vsnprintf_(char *buffer, size_t count, const char *format, va_list va);

int qm_snprintf(char *str, const int len, const char *format, ...)
{
    va_list args;
    int ret;

    va_start(args, format);
    ret = vsnprintf_(str, (size_t)len, format, args);
    va_end(args);
    return ret;
}

int qm_vsnprintf(char *str, const int len, const char *format, va_list args)
{
    return vsnprintf_(str, (size_t)len, format, args);
}

static uint32_t g_random_seed = 1;

void qm_srandom(uint32_t seed)
{
    g_random_seed = seed;
}

uint32_t qm_random_get(uint32_t region)
{
    g_random_seed = g_random_seed * 1103515245u + 12345u;
    if (region == 0) {
        return 0;
    }
    return ((g_random_seed / 65536u) % 32768u) % region;
}
