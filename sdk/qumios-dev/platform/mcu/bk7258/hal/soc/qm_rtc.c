#include "qm.h"
#include "qm_rtc.h"

/* Software RTC: base unix time + qm_now_ms() offset. Good enough for cloud timestamps. */
static uint32_t s_base_unix;
static uint32_t s_base_ms;

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    s_base_unix = 1700000000U; /* 2023-11-14 fallback until NTP/set */
    s_base_ms = qm_now_ms();
    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    uint32_t now_ms;

    (void)rtc;
    if (timestamp == NULL) {
        return -QM_EINVAL;
    }

    now_ms = qm_now_ms();
    *timestamp = s_base_unix + (now_ms - s_base_ms) / 1000U;
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    (void)rtc;
    s_base_unix = timestamp;
    s_base_ms = qm_now_ms();
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time)
{
    (void)rtc;
    (void)time;
    return -QM_ERROR;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    (void)rtc;
    (void)time;
    return -QM_ERROR;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    return QM_EOK;
}
