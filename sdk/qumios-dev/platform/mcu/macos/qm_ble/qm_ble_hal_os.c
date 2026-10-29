#include "qm_ble_hal_os.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_kv.h"

#include <stdlib.h>

typedef struct {
    qm_timer_t timer;
    qm_ble_timer_cb_t callback;
    void *arg;
} mac_ble_timer_t;

static void timer_bridge(qm_timer_t *timer, void *arg)
{
    mac_ble_timer_t *ctx = (mac_ble_timer_t *)arg;
    (void)timer;
    if (ctx != NULL && ctx->callback != NULL) {
        ctx->callback(ctx->arg);
    }
}

int qm_ble_timer_new(qm_ble_timer_t *timer, qm_ble_timer_cb_t callback,
                     void *arg, int ms, int repeat)
{
    mac_ble_timer_t *ctx;
    int rc;

    if (timer == NULL || callback == NULL || ms < 0) {
        return -QM_EINVAL;
    }
    ctx = (mac_ble_timer_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->callback = callback;
    ctx->arg = arg;
    ctx->timer.hdl = NULL;
    rc = qm_timer_new(&ctx->timer, timer_bridge, ctx, ms, repeat);
    if (rc != QM_EOK) {
        qm_free(ctx);
        return rc;
    }
    timer->hdl = ctx;
    return QM_EOK;
}

int qm_ble_timer_start(qm_ble_timer_t *timer)
{
    mac_ble_timer_t *ctx = timer == NULL ? NULL : (mac_ble_timer_t *)timer->hdl;
    return ctx == NULL ? -QM_EINVAL : qm_timer_start(&ctx->timer);
}

int qm_ble_timer_stop(qm_ble_timer_t *timer)
{
    mac_ble_timer_t *ctx = timer == NULL ? NULL : (mac_ble_timer_t *)timer->hdl;
    return ctx == NULL ? -QM_EINVAL : qm_timer_stop(&ctx->timer);
}

void qm_ble_timer_free(qm_ble_timer_t *timer)
{
    mac_ble_timer_t *ctx;
    if (timer == NULL || timer->hdl == NULL) {
        return;
    }
    ctx = (mac_ble_timer_t *)timer->hdl;
    qm_timer_free(&ctx->timer);
    qm_free(ctx);
    timer->hdl = NULL;
}

int qm_ble_mutex_new(qm_ble_mutex_t *mutex) { return qm_mutex_new((qm_mutex_t *)mutex); }
void qm_ble_mutex_free(qm_ble_mutex_t *mutex) { (void)qm_mutex_free((qm_mutex_t *)mutex); }
int qm_ble_mutex_lock(qm_ble_mutex_t *mutex, unsigned int timeout) { return qm_mutex_lock((qm_mutex_t *)mutex, timeout); }
int qm_ble_mutex_unlock(qm_ble_mutex_t *mutex) { return qm_mutex_unlock((qm_mutex_t *)mutex); }
void *qm_ble_malloc(unsigned int size) { return qm_malloc(size); }
void qm_ble_free(void *mem) { qm_free(mem); }
void qm_ble_reboot(void) { qm_reboot(); }
void qm_ble_msleep(int ms) { (void)qm_msleep(ms); }
long long qm_ble_now_ms(void) { return (long long)qm_now_ms(); }
int qm_ble_kv_set(const char *key, const void *value, int len, int sync) { return qm_kv_set(key, value, len, sync); }
int qm_ble_kv_get(const char *key, void *buffer, int *buffer_len) { return qm_kv_get(key, buffer, buffer_len); }
int qm_ble_kv_del(const char *key) { return qm_kv_del(key); }
int qm_ble_rand(void) { return (int)qm_random_get(0x7fffffffU); }

