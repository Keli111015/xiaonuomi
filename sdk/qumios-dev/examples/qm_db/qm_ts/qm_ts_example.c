#include "qm.h"
#include "qm_ts.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "qm_ts"

static void *ts1_db = NULL;
static void *ts2_db = NULL;

static uint32_t ts_get_time(void)
{
    return 123;
}

static bool_t ts1_cb(qm_ts_t *ts, void *arg)
{
    char test[16] = {0};
    qm_err_t ret = QM_EOK;
    ret = qm_ts_read(ts1_db, ts, test, 16);
    if(ret < 0){
        QM_LOGE(LOG_TAG, "ts1 read error");
        return true;
    }else{
        QM_LOGD(LOG_TAG, "ts1 read success: %s", test);
        QM_LOGD(LOG_TAG, "time: %d", ts->time);
        return false;
    }
}

static bool_t ts2_cb(qm_ts_t *ts, void *arg)
{
    char test[16] = {0};
    qm_err_t ret = QM_EOK;
    ret = qm_ts_read(ts2_db, ts, test, 16);
    if(ret < 0){
        QM_LOGE(LOG_TAG, "ts2 read error");
        return true;
    }else{
        QM_LOGD(LOG_TAG, "ts2 read success: %s", test);
        QM_LOGD(LOG_TAG, "time: %d", ts->time);
        qm_ts_set_status(ts2_db, ts, QM_TS_USER_STATUS1);
        return false;
    }
}

void qm_application_start(void)
{
    int count = 0;
    qm_err_t ret = QM_EOK;
    char *test = "hello";
    char *test1 = "world";

    ts1_db = qm_ts_init("ts1", ts_get_time, 128);
    if(ts1_db == NULL){
        QM_LOGE(LOG_TAG, "qm ts1 init error");
        return;
    }
    
    QM_LOGE(LOG_TAG, "qm ts1 init success");

    ts2_db = qm_ts_init("ts2", ts_get_time, 128);
    if(ts2_db == NULL){
        QM_LOGE(LOG_TAG, "qm ts2 init error");
        return;
    }

    QM_LOGD(LOG_TAG, "qm ts2 init success");

    ret = qm_ts_append(ts1_db, test, strlen(test));
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "ts1 append error");
        return;
    }
    QM_LOGD(LOG_TAG, "ts1 append success");

    ret = qm_ts_append(ts2_db, test1, strlen(test1));
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "ts2 append error");
        return;
    }
    QM_LOGD(LOG_TAG, "ts2 append success");

    count = qm_ts_query_count(ts1_db, 0, 999, QM_TS_WRITE);
    QM_LOGD(LOG_TAG, "ts1 write count: %d", count);

    QM_LOGD(LOG_TAG, "ts1 iter");
    qm_ts_iter(ts1_db, ts1_cb, NULL);

    QM_LOGD(LOG_TAG, "ts1 iter by status");
    qm_ts_iter_by_status(ts1_db, QM_TS_WRITE, ts1_cb, NULL);

    QM_LOGD(LOG_TAG, "ts1 iter by time");
    qm_ts_iter_by_time(ts1_db, 0, 999, ts1_cb, NULL);

    QM_LOGD(LOG_TAG, "ts1 iter by time and status");
    qm_ts_iter_by_time_and_status(ts1_db, 0, 999, QM_TS_WRITE, ts1_cb, NULL);


    QM_LOGD(LOG_TAG, "ts2 iter");
    qm_ts_iter(ts2_db, ts2_cb, NULL);

    QM_LOGD(LOG_TAG, "ts2 iter by status");
    qm_ts_iter_by_status(ts2_db, QM_TS_WRITE, ts2_cb, NULL);

}


