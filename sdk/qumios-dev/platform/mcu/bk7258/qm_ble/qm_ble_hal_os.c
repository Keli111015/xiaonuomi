#include "qm_ble_hal_os.h"
#include "qm_hal_kv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

typedef struct {
    qm_timer_t timer;
    qm_ble_timer_cb_t cb;
    void *arg;
} qm_ble_timer_ctx_t;

static void qm_ble_timer_bridge(qm_timer_t *timer, void *arg)
{
    qm_ble_timer_ctx_t *ctx = (qm_ble_timer_ctx_t *)arg;

    (void)timer;

    if (ctx && ctx->cb) {
        ctx->cb(ctx->arg);
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

int qm_ble_mutex_lock(qm_ble_mutex_t *mutex, unsigned int ms)
{
    return qm_mutex_lock((qm_mutex_t *)mutex, ms);
}

int qm_ble_mutex_unlock(qm_ble_mutex_t *mutex)
{
    return qm_mutex_unlock((qm_mutex_t *)mutex);
}

int qm_ble_timer_new(qm_ble_timer_t *timer, qm_ble_timer_cb_t cb, void *arg, int ms, int repeat)
{
    qm_ble_timer_ctx_t *ctx;

    if (timer == NULL || cb == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ble_timer_ctx_t *)qm_malloc(sizeof(qm_ble_timer_ctx_t));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }

    ctx->cb = cb;
    ctx->arg = arg;
    memset(&ctx->timer, 0, sizeof(ctx->timer));

    if (qm_timer_new(&ctx->timer, qm_ble_timer_bridge, ctx, ms, repeat) != QM_EOK) {
        qm_free(ctx);
        return -QM_ERROR;
    }

    timer->hdl = ctx;
    return QM_EOK;
}

int qm_ble_timer_start(qm_ble_timer_t *timer)
{
    qm_ble_timer_ctx_t *ctx;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ble_timer_ctx_t *)timer->hdl;
    return qm_timer_start(&ctx->timer);
}

int qm_ble_timer_stop(qm_ble_timer_t *timer)
{
    qm_ble_timer_ctx_t *ctx;

    if (timer == NULL || timer->hdl == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ble_timer_ctx_t *)timer->hdl;
    return qm_timer_stop(&ctx->timer);
}

void qm_ble_timer_free(qm_ble_timer_t *timer)
{
    qm_ble_timer_ctx_t *ctx;

    if (timer == NULL || timer->hdl == NULL) {
        return;
    }

    ctx = (qm_ble_timer_ctx_t *)timer->hdl;
    qm_timer_free(&ctx->timer);
    qm_free(ctx);
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
    return qm_hal_kv_set(key, value, len, sync);
}

int qm_ble_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return qm_hal_kv_get(key, buffer, buffer_len);
}

int qm_ble_kv_del(const char *key)
{
    return qm_hal_kv_del(key);
}

int qm_ble_rand(void)
{
    return (int)qm_random_get(0x7fffffff);
}
