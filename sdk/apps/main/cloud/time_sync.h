#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 联网对时（应用层）
 *
 * 依赖平台组件：ntpdate（SNTP） + epochtime（公网 NTP 取 UTC）。
 * 在 qm_iot 上报 QM_IOT_EVENT_CLOUD_CONN（网络确实可用）之后调用一次：
 *   1) ntpdate 取时间并写入 RTC（qm_settimeofday）；
 *   2) 成功后通知闹钟组件按新时钟重新排程（qm_alarm_notify_time_changed）。
 *
 * 未对时时 qm_time() 拿到的是 RTC 的 0 基准（约等于开机秒数），
 * 因此闹钟/时刻显示都会失真，必须先对时再排闹钟。
 */
void time_sync_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYNC_H */
