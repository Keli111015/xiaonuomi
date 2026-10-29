#include "time_sync.h"

#include "qm_errno.h"   /* QM_EOK；闹钟关掉时 app_alarm.h 不再带进来 */
#if defined(CONFIG_QM_ALARM_SUPPORT) && CONFIG_QM_ALARM_SUPPORT
#include "app_alarm.h"
#endif
#include "ntpdate.h"
#include "qm_time.h"
#include "qm_log.h"

#define LOG_TAG "time_sync"

/* ntpdate 回调：它内部已经 qm_settimeofday() 写过 RTC，这里只做日志 + 通知闹钟重排 */
static void on_ntpdate_done(ntpdate_res_t res)
{
    qm_time_t now;
    struct qm_tm tm;

    if (res != NTPDATE_RES_SUCCESS) {
        QM_LOGW(LOG_TAG, "ntp sync failed (network not ready?)");
        return;
    }

    now = qm_time(NULL);
    qm_localtime_r(&now, &tm);
    QM_LOGI(LOG_TAG, "ntp synced, now=%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

    /* 时钟基准已变：让闹钟按新时钟重新排程（否则对时前排的闹钟会按旧时钟误触发） */
#if defined(CONFIG_QM_ALARM_SUPPORT) && CONFIG_QM_ALARM_SUPPORT
    if (app_alarm_time_changed() != QM_EOK) {
        QM_LOGE(LOG_TAG, "alarm time_changed failed");
    }
#endif
}

void time_sync_start(void)
{
    int ret = ntpdate_start(on_ntpdate_done);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "ntpdate_start failed ret=%d", ret);
        return;
    }
    QM_LOGI(LOG_TAG, "ntp sync started");
}
