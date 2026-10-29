#include "qm.h"
#include "qm_log.h"

#include "qm_rtc.h"

#define LOG_TAG "TEST"

qm_rtc_dev_t rtc_dev = {
    .port = 1,
    .config = { 
        .format = QM_RTC_FORMAT_DEC,
    },
};

void qm_application_start(void)
{
    int ret = QM_EOK;
    uint32_t timestamp = 0;
    qm_rtc_time_t time = {0};

    ret = qm_rtc_init(&rtc_dev);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "rtc init error %d", ret);
        return;
    }

    qm_rtc_get_time(&rtc_dev, &time);
    QM_LOGD(LOG_TAG, "%04d/%02d/%02d-%02d:%02d:%02d-%02d\n",
            time.year,
            time.month,
            time.date,
            time.hr,
            time.min,
            time.sec,
            time.weekday);

    time.year = 2021;
    qm_rtc_set_time(&rtc_dev, &time);

    qm_rtc_get_time(&rtc_dev, &time);
    QM_LOGD(LOG_TAG, "%04d/%02d/%02d-%02d:%02d:%02d-%02d\n",
            time.year,
            time.month,
            time.date,
            time.hr,
            time.min,
            time.sec,
            time.weekday);

    qm_rtc_get_timestamp(&rtc_dev, &timestamp);
    QM_LOGD(LOG_TAG, "timestamp %04d\n");

    timestamp += 1000;
    qm_rtc_set_timestamp(&rtc_dev, timestamp);
    
    qm_rtc_get_timestamp(&rtc_dev, &timestamp);
    QM_LOGD(LOG_TAG, "timestamp %04d\n");

    ret = qm_rtc_deinit(&rtc_dev);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "rtc deinit error %d", ret);
    }
}