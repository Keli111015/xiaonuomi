#include "qm_ble_hal_os.h"
#include "qm_kernel.h"

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
    return qm_timer_new((qm_timer_t *)timer, (qm_timer_cb_t)cb, arg, ms, repeat);
}

int qm_ble_timer_start(qm_ble_timer_t *timer)
{
    return qm_timer_start((qm_timer_t *)timer);
}

int qm_ble_timer_stop(qm_ble_timer_t *timer)
{
    return qm_timer_stop((qm_timer_t *)timer);
}

void qm_ble_timer_free(qm_ble_timer_t *timer)
{
    qm_timer_free((qm_timer_t *)timer);
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
   qm_ble_msleep(ms);
}

long long qm_ble_now_ms(void)
{
    return qm_now_ms();
}

int qm_ble_kv_set(const char *key, const void *value, int len, int sync)
{
    return -1;
}

int qm_ble_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return -1;
}

int qm_ble_kv_del(const char *key)
{
    return -1;
}

int qm_ble_rand(void)
{
    return 0;
}
