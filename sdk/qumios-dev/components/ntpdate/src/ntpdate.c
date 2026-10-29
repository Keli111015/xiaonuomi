#include "ntpdate.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_epochtime.h"
#include "qm_time.h"
#include "qm_log.h"

#define LOG_TAG  "ntpdate"

#if CONFIG_NTPDATE_SUPPORT

static qm_task_t time_task = {0};
#if CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT
static qm_timer_t timer = {0};
static uint32_t notify_time = 0;
static void qm_timer_callback( qm_timer_t *timer, void *arg);
#endif
static ntpdate_complete_cb g_ntpdate_complete = NULL;

static void ntpdate_task(void *arg);

static int _ntpdate_start(void)
{
    qm_err_t ret = QM_EOK;
    ret = qm_task_new(&time_task, "ntpdate", ntpdate_task, NULL, CONFIG_NTPDATE_TASK_SIZE, CONFIG_NTPDATE_TASK_PRIO);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}
//start notify time synchronization form ntp server
int ntpdate_start(ntpdate_complete_cb cb)
{
    g_ntpdate_complete = cb;
    return _ntpdate_start();
}

static void ntpdate_task(void *arg)
{
    qm_err_t ret = QM_EOK;
    struct timeval_t time = {0};
    struct qm_timeval timeval = {0};

#if CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT
    if(qm_timer_is_valid(&timer)){
        qm_timer_new(&timer, qm_timer_callback, NULL, 1000, 1);
    }
    qm_timer_stop(&timer);
#endif

    ret = qm_get_epoch_time_from_ntp(&time);
    if(ret != QM_EOK){
        if(g_ntpdate_complete){
            g_ntpdate_complete(NTPDATE_RES_FAIL);
        }
        qm_task_exit(&time_task);
    }
    timeval.tv_sec = (qm_time_t)time.tv_sec;
    qm_settimeofday(&timeval, NULL);

    if(g_ntpdate_complete){
        g_ntpdate_complete(NTPDATE_RES_SUCCESS);
    }

#if CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT
    qm_timer_start(&timer);
#endif
    qm_task_exit(&time_task);
}
#if CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT
static void qm_timer_callback( qm_timer_t *timer, void *arg)
{
    notify_time++;
    if(notify_time >= CONFIG_NTPDATE_PERIODIC_SYNC_INTERVAL){
        notify_time = 0;
        _ntpdate_start();
    }
}
#endif

#endif
