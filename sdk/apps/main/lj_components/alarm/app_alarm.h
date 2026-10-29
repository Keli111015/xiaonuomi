#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_alarm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 应用层闹钟（app_alarm）
 *
 * 平台 qm_alarm 组件之上的薄封装：初始化/逆初始化、事件搬运、条目与响铃操作转发。
 * 不负责铃声内容、振动和界面，示例用已内嵌的开机音效当提示音（见 app_alarm_behavior）。
 * 墙钟由组件内部用 qm_time 计算，应用层只需在对时后调 app_alarm_time_changed()。
 * 详细说明见同目录 README_CN.md。
 */

/* 一次通知的事件快照：值语义，回调返回后仍有效；extensions 是组件内借用指针，不转发 */
typedef struct {
    qm_alarm_notify_t notify;
    uint8_t alarm_id;
    char source[QM_ALARM_SOURCE_MAX_LEN];
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_mask;
    qm_bool enabled;
    qm_bool vibrate;
    qm_alarm_tone_t tone;
} app_alarm_event_t;

/* 产品层行为回调：跑在默认事件循环任务里，不要长时间阻塞 */
typedef void (*app_alarm_behavior_cb_t)(const app_alarm_event_t *evt, void *user);

/* 初始化（= 注册）：创建并启动平台组件，内部登记行为回调；iot_handle 传 NULL 只做本地闹钟 */
qm_err_t app_alarm_init(void *iot_handle);

/* 逆初始化：停止调度、注销云端方法、销毁组件实例并清空行为回调 */
void app_alarm_deinit(void);

/* 是否已初始化 */
qm_bool app_alarm_is_ready(void);

/* ---- 闹钟条目 ---- */

/* 填一份新增可用的默认条目（source=device、id=AUTO、enabled） */
void app_alarm_item_defaults(qm_alarm_item_t *item);

/* 新增一个闹钟；alarm_id 传 QM_ALARM_LOCAL_ID_AUTO 时自动分配 */
qm_err_t app_alarm_add(const qm_alarm_item_t *item);

/* 修改：item 必须是带既有 (source, alarm_id) 的完整快照 */
qm_err_t app_alarm_modify(const qm_alarm_item_t *item);

/* 删除指定闹钟 */
qm_err_t app_alarm_delete(const char *source, uint8_t alarm_id);

/* 启用/禁用指定闹钟 */
qm_err_t app_alarm_enable(const char *source, uint8_t alarm_id, qm_bool enabled);

/* ---- 响铃流程 ---- */

/* 关闭当前正在响铃的闹钟 */
qm_err_t app_alarm_close_ringing(void);

/* 进入稍后提醒（受该闹钟自身 snooze 配置限制） */
qm_err_t app_alarm_snooze(void);

/* 取消待响的稍后提醒 */
qm_err_t app_alarm_cancel_snooze(void);

/* ---- 查询（界面渲染/兜底，不是触发主路径）---- */

/* 下一个将要触发的闹钟 + 绝对触发时间戳 */
qm_err_t app_alarm_get_next(qm_alarm_item_t *out, qm_time_t *trigger_time);

/* 整份闹钟列表快照 */
qm_err_t app_alarm_get_list(qm_alarm_item_t *out, uint16_t capacity, uint16_t *count);

/* 当前运行态（IDLE/RINGING/SNOOZING）与关联闹钟 id */
qm_err_t app_alarm_get_state(qm_alarm_state_t *state, uint8_t *alarm_id);

/* ---- 其它 ---- */

/* 对时之后调用：让组件按新时钟重新排程 */
qm_err_t app_alarm_time_changed(void);

/* 主动整表上报云端（一般不需要，本地变更组件会自动上报） */
qm_err_t app_alarm_report(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_H */
