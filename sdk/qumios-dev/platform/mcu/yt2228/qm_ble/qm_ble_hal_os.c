#include "qm.h"
#include "qm_ble_hal_os.h"

int qm_ble_mutex_new(qm_ble_mutex_t *mutex)
{
    return QM_EOK;
}

void qm_ble_mutex_free(qm_ble_mutex_t *mutex)
{
    return ;
}

int qm_ble_mutex_lock(qm_ble_mutex_t *mutex, unsigned int ms)
{
    return QM_EOK;
}

int qm_ble_mutex_unlock(qm_ble_mutex_t *mutex)
{
    return QM_EOK;
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
