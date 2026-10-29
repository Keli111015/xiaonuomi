#define BOOL_DEFINE_CONFLICT

#include "qm_ble_hal_os.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_kv.h"

typedef struct {
    qm_timer_t timer;
    qm_ble_timer_cb_t callback;
    void *arg;
} qm_ble_timer_context_t;

static void qm_ble_timer_callback(qm_timer_t *timer, void *arg)
{
    qm_ble_timer_context_t *context = (qm_ble_timer_context_t *)arg;

    (void)timer;
    if (context != NULL && context->callback != NULL) {
        context->callback(context->arg);
    }
}

int qm_ble_mutex_new(qm_ble_mutex_t *mutex)
{
    return qm_mutex_new((qm_mutex_t *)mutex);
}

void qm_ble_mutex_free(qm_ble_mutex_t *mutex)
{
    qm_mutex_free((qm_mutex_t *)mutex);
}

int qm_ble_mutex_lock(qm_ble_mutex_t *mutex, unsigned int timeout)
{
    return qm_mutex_lock((qm_mutex_t *)mutex, timeout);
}

int qm_ble_mutex_unlock(qm_ble_mutex_t *mutex)
{
    return qm_mutex_unlock((qm_mutex_t *)mutex);
}

int qm_ble_timer_new(qm_ble_timer_t *timer, qm_ble_timer_cb_t callback,
                     void *arg, int ms, int repeat)
{
    qm_ble_timer_context_t *context;
    int ret;

    if (timer == NULL || callback == NULL) {
        return -QM_EINVAL;
    }

    context = (qm_ble_timer_context_t *)qm_malloc(sizeof(*context));
    if (context == NULL) {
        return -QM_ENOMEM;
    }

    context->timer.hdl = NULL;
    context->callback = callback;
    context->arg = arg;
    ret = qm_timer_new(&context->timer, qm_ble_timer_callback, context, ms, repeat);
    if (ret != QM_EOK) {
        qm_free(context);
        return ret;
    }

    timer->hdl = context;
    return QM_EOK;
}

int qm_ble_timer_start(qm_ble_timer_t *timer)
{
    qm_ble_timer_context_t *context;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    context = (qm_ble_timer_context_t *)timer->hdl;
    return qm_timer_start(&context->timer);
}

int qm_ble_timer_stop(qm_ble_timer_t *timer)
{
    qm_ble_timer_context_t *context;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    context = (qm_ble_timer_context_t *)timer->hdl;
    return qm_timer_stop(&context->timer);
}

void qm_ble_timer_free(qm_ble_timer_t *timer)
{
    qm_ble_timer_context_t *context;

    if (timer == NULL || timer->hdl == NULL) {
        return;
    }

    context = (qm_ble_timer_context_t *)timer->hdl;
    qm_timer_free(&context->timer);
    qm_free(context);
    timer->hdl = NULL;
}

void *qm_ble_malloc(unsigned int size)
{
    return qm_malloc(size);
}

void qm_ble_free(void *mem)
{
    qm_free(mem);
}

void qm_ble_reboot(void)
{
    qm_reboot();
}

void qm_ble_msleep(int ms)
{
    qm_msleep(ms);
}

long long qm_ble_now_ms(void)
{
    return (long long)qm_now_ms();
}

int qm_ble_kv_set(const char *key, const void *value, int len, int sync)
{
    return qm_kv_set(key, value, len, sync);
}

int qm_ble_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return qm_kv_get(key, buffer, buffer_len);
}

int qm_ble_kv_del(const char *key)
{
    return qm_kv_del(key);
}

int qm_ble_rand(void)
{
    return (int)qm_random_get(0x7fffffffU);
}
