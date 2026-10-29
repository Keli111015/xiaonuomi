# qm_alarm

For the English version, see [README.md](README.md).

`qm_alarm` 是从 `qm_agent_alarm` 拆出的独立闹钟组件。
当前这一版保留了原实现中已经较成熟的存储、调度、snooze、JSON
和云端同步能力，但移除了对 `qm_agent` 运行时的直接依赖。

## 概述

`qm_alarm` 负责设备侧闹钟领域能力：

- 闹钟运行时内存状态管理
- 基于 KV 的持久化存储
- 基于定时器的调度与下一次闹钟计算
- 当前响铃闹钟的 dismiss / snooze 流程
- 可选的 `qm_iot` 云端同步

## 职责范围

`qm_alarm` 负责：

- 创建、修改、删除、启用/禁用闹钟
- 持久化闹钟数据并在启动时恢复
- 基于重复规则计算下一次生效闹钟
- 在响铃、关闭、稍后提醒时回调上层
- 接收并上报 `alarm` / `alarmReply` 云端协议

`qm_alarm` 不负责：

- 实际音频播放、振动等硬件控制
- UI 展示
- `qm_agent` 会话生命周期管理
- 通用云端连接创建

## 当前形态

- 对外 API 改为实例化句柄：`qm_alarm_t`
- 本地操作不再依赖 `qm_agent_handle_t`
- 云端同步仍沿用原有 `alarm` / `alarmReply` 协议
- 云端通道通过 `qm_alarm_config_t.iot_handle` 直接绑定
- 内部实现仍大量拷贝自 `qm_agent_alarm`，以降低迁移风险

## 运行时配置

运行时参数通过 `qm_alarm_config_t` 传入：

- `iot_handle`：已有的 `qm_iot` 会话句柄；传 `NULL` 时关闭云同步
- `max_alarms`：内存中允许保存的最大闹钟数；传 `0` 使用
  `CONFIG_QM_ALARM_MAX_ALARMS`，非 0 时不能超过它
- `notify_cb`：闹钟响铃、关闭、稍后提醒和闹钟变化时触发的回调
- `user_ctx`：透传给回调的用户上下文

固定默认值通过 Kconfig 配置：

- `CONFIG_QM_ALARM_SUPPORT`：启用独立 `qm_alarm` 组件
- `CONFIG_QM_ALARM_MAX_ALARMS`：编译期最大闹钟容量，同时也是默认运行时容量
- `CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN`：默认稍后提醒间隔（分钟）

## 日志配置

`QM_ALARM_LOG_CONFIG` 是日志分类的 Kconfig 总开关。关闭时默认只输出错误和
生命周期日志；打开后可分别选择 `QM_ALARM_LOG_ERROR`、
`QM_ALARM_LOG_LIFECYCLE`、`QM_ALARM_LOG_CONTROL`、
`QM_ALARM_LOG_SCHEDULER`、`QM_ALARM_LOG_STORAGE`、
`QM_ALARM_LOG_CLOUD` 和 `QM_ALARM_LOG_STATS`。

运行时日志掩码是组件全局配置，对所有 alarm 实例生效：

```c
qm_alarm_set_log_flags(QM_ALARM_LOG_ERROR |
                       QM_ALARM_LOG_LIFECYCLE |
                       QM_ALARM_LOG_CLOUD);
```

调用 `qm_alarm_set_log_flags(QM_ALARM_LOG_NONE)` 可关闭全部 `qm_alarm`
日志；它不会改变 QUMI 全局日志级别或其他组件的日志配置。

## 数据模型

`qm_alarm_item_t` 是核心闹钟快照结构，常用字段包括：

- `alarm_id`：`1..255` 范围内的数字闹钟 ID
- `source`：闹钟来源，例如 `device`、`app`
- `hour` / `minute`：本地触发时间
- `enabled`：当前是否参与调度
- `repeat_mask`：按 `QM_ALARM_REPEAT_*` 定义的星期重复位图
- `tone`：包含 `type` 和 `id` 的结构化铃声信息
- `snooze`：该闹钟自身的稍后提醒配置
- `extensions`：可选 JSON 字符串，会由组件复制并持久化

## 对外 API 摘要

```c
qm_alarm_t qm_alarm_create(const qm_alarm_config_t *config);
int qm_alarm_start(qm_alarm_t alarm);
int qm_alarm_stop(qm_alarm_t alarm);
int qm_alarm_create_item(qm_alarm_t alarm, const qm_alarm_item_t *item);
int qm_alarm_modify_item(qm_alarm_t alarm, const qm_alarm_item_t *item);
int qm_alarm_delete_item(qm_alarm_t alarm, const char *source, uint8_t alarm_id);
int qm_alarm_set_enabled(qm_alarm_t alarm, const char *source, uint8_t alarm_id, qm_bool enabled);
int qm_alarm_snooze(qm_alarm_t alarm);
int qm_alarm_cancel_snooze(qm_alarm_t alarm);
int qm_alarm_dismiss(qm_alarm_t alarm);
int qm_alarm_get_state(qm_alarm_t alarm, qm_alarm_state_t *state, uint8_t *alarm_id);
int qm_alarm_report(qm_alarm_t alarm);
int qm_alarm_list(qm_alarm_t alarm, qm_alarm_item_t *out, uint16_t capacity, uint16_t *count);
int qm_alarm_get_upcoming(qm_alarm_t alarm, qm_alarm_item_t *out, qm_time_t *trigger_time);
void qm_alarm_destroy(qm_alarm_t alarm);
```

## 生命周期

典型使用流程如下：

1. 调用 `qm_alarm_create()` 创建运行时实例。
2. 调用 `qm_alarm_start()` 启动调度，并按需注册云端处理逻辑。
3. 执行本地闹钟操作，或者接收云端下发操作。
4. 在 `notify_cb` 中处理响铃、关闭、稍后提醒和闹钟变化通知。
5. 如需停止运行，可调用 `qm_alarm_stop()`。
6. 最终调用 `qm_alarm_destroy()` 释放资源。

## 运行时行为

- 闹钟数据会持久化到 KV，并在下次 create/start 时重新加载。
- 本地 `device` 闹钟创建时，若 `alarm_id == 0`，会自动分配第一个可用 ID。
- 一次性闹钟在 `repeat_mask == 0` 时，响铃后会自动置为禁用。
- 自动禁用（含同一时刻的其他一次性闹钟）由组件统一持久化并合并为一次全量列表上报。
  应用层只处理变更通知，不需要额外上报列表或监听重连；贪睡再次响铃不会重复上报未变化的禁用标志。
- `snooze` 只会对当前正在响铃的闹钟生效，并使用该闹钟自身的 snooze 配置。
- 云端不可用时，本地修改不会失败，而是将运行时标记为 dirty。
- 云端重新连接后，会重新上报整份闹钟列表以进行对账。
- 列表序列化或提交失败时保留待同步标记，在下次云端连接、保留云会话时的网络恢复或列表上报时重试；
  这不代表定时重试，也不代表已经收到服务端确认。
- 运行状态变化通过 `alarm/stateChange` 上报，状态值为 `idle`、`ringing` 或
  `snoozing`；断网期间产生的状态会在恢复连接后补报。
- `qm_alarm_get_upcoming()` 返回“下一次将要触发的闹钟”。

## 云端协议

当提供 `iot_handle` 后，`qm_alarm_start()` 会直接向 `qm_iot` 注册：

- `alarm` 下行处理函数
- `alarmReply` 监听函数
- IOT 连接/断开事件处理函数

当前组件支持的云端子方法包括：

- `create`
- `modify`
- `delete`
- `enable`
- `close`，用于关闭当前正在响铃的闹钟
- `snooze`，用于让当前正在响铃的闹钟进入稍后提醒
- `stateChange`，用于设备上报运行状态（包含 `alarmId` 和 `state`）
- `update`，用于整表上报

云端 `snooze` 和 `close` 指令会先回复 ACK，ACK 成功后再上报对应的
`stateChange`。

## 持久化说明

组件在 KV 中采用极简两层存储：

- 一条紧凑的 `qmalarm.meta` 记录，只保存存储版本和 used bitmap
- 以 slot 为 key 的单条闹钟完整内容，内部直接包含可选 `extensions`
  JSON

位图只用于启动时快速定位哪些 slot 被占用，避免每次启动都扫描全部
slot。如果发现旧版 `qmalarm.meta` 快照，会直接清理旧 KV 数据，不做迁移。

## 基本用法

```c
#include "qm_alarm.h"

static void alarm_notify_cb(qm_alarm_t alarm,
                            qm_alarm_notify_t notify,
                            const qm_alarm_item_t *item,
                            void *user_ctx)
{
    (void)alarm;
    (void)notify;
    (void)item;
    (void)user_ctx;
}

void app_alarm_start(void *iot_handle)
{
    qm_alarm_config_t config = {0};
    qm_alarm_t alarm;

    config.iot_handle = iot_handle;
    config.max_alarms = 8;
    config.notify_cb = alarm_notify_cb;

    alarm = qm_alarm_create(&config);
    if (alarm == NULL) {
        return;
    }

    if (qm_alarm_start(alarm) != QM_EOK) {
        qm_alarm_destroy(alarm);
        return;
    }
}
```

## 迁移说明

- `qm_alarm` 是当前面向独立组件的目标形态。
- `qm_agent_alarm` 目前仍然存在，尚未收敛成薄适配层。
- 这一版迁移策略是“先拷贝拆出”，而不是立即做一次彻底的内部重构。

## 下一步建议

如果独立组件在接入和联调后验证稳定，下一步建议将
`qm_agent_alarm` 收敛为基于 `qm_alarm` 的薄 adapter，
而不是长期维护两套独立的运行时实现。
