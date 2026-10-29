#include "qm.h"
#include "qm_work.h"
#include "qm_log.h"

#define LOG_TAG "qm_work"
#define EXAMPLE_PERIODIC_MAX_COUNT 5

static qm_work_t s_once_work = {0};
static qm_work_t s_replace_work = {0};
static qm_work_t s_cancel_work = {0};
static qm_work_t s_periodic_work = {0};

static uint32_t s_periodic_count = 0;

static void example_once_action(void *arg)
{
    QM_LOGD(LOG_TAG, "once action arg=%s", (char *)arg);
}

static void example_replace_action(void *arg)
{
    QM_LOGD(LOG_TAG, "replace action arg=%s", (char *)arg);
}

static void example_cancel_action(void *arg)
{
    QM_LOGD(LOG_TAG, "cancel action should not run arg=%s", (char *)arg);
}

static void example_periodic_action(void *arg)
{
    int32_t ret;

    s_periodic_count++;
    QM_LOGD(LOG_TAG, "periodic action count=%u arg=%s",
            (unsigned int)s_periodic_count,
            (char *)arg);

    if(s_periodic_count >= EXAMPLE_PERIODIC_MAX_COUNT){
        QM_LOGD(LOG_TAG, "periodic action done");
        return;
    }

    ret = qm_post_delayed_action(&s_periodic_work, example_periodic_action, arg, 1000);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "reschedule periodic action failed ret=%d", ret);
    }
}

void qm_application_start(void)
{
    int32_t ret;

    ret = qm_work_on_init();
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "qm_work_on_init failed ret=%d", ret);
        return;
    }

    ret = qm_post_delayed_action(&s_once_work, example_once_action, "once", 1000);
    QM_LOGD(LOG_TAG, "post once ret=%d", ret);

    ret = qm_post_delayed_action(&s_replace_work, example_replace_action, "old", 3000);
    QM_LOGD(LOG_TAG, "post replace old ret=%d", ret);
    ret = qm_post_delayed_action(&s_replace_work, example_replace_action, "new", 1500);
    QM_LOGD(LOG_TAG, "post replace new ret=%d", ret);

    ret = qm_post_delayed_action(&s_cancel_work, example_cancel_action, "cancel", 2000);
    QM_LOGD(LOG_TAG, "post cancel ret=%d", ret);
    ret = qm_cancel_delayed_action(&s_cancel_work);
    QM_LOGD(LOG_TAG, "cancel ret=%d", ret);

    s_periodic_count = 0;
    ret = qm_post_delayed_action(&s_periodic_work, example_periodic_action, "periodic", 500);
    QM_LOGD(LOG_TAG, "post periodic ret=%d", ret);

#if CONFIG_QM_WORKQUEUE_NO_TASK
    while(1){
        qm_workqueue_process();
        qm_msleep(10);
    }
#endif
}
