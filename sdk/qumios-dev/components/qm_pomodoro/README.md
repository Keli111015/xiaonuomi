# qm_pomodoro

`qm_pomodoro` is an instance-based Pomodoro timer. It alternates focus, short
break, and long break phases and reports state and countdown events through an
optional callback.

## Lifecycle

```text
create -> idle -> focus -> short/long break -> focus
                |                  |
                +-- pause/resume --+
                +-- stop ----------> idle
                +-- target reached -> finished
```

The component owns its timer and state transitions. Applications own UI,
notifications, persistence, and callback work. Do not call
`qm_pomodoro_destroy()` from `event_cb`; destruction waits for active timer and
API operations to leave.

## Responsibility Boundary

The timer does not play sounds, persist sessions, or perform UI updates.

## Basic Usage

```c
#include "qm_pomodoro.h"

static void pomodoro_event(qm_pomodoro_t timer,
                           const qm_pomodoro_event_data_t *event)
{
    (void)timer;

    if (event->event == QM_POMODORO_EVENT_TICK) {
        /* event->status.remaining_sec contains the current countdown. */
    }
}

void app_start_pomodoro(void)
{
    qm_pomodoro_config_t config = {
        .focus_sec = 25 * 60,
        .short_break_sec = 5 * 60,
        .long_break_sec = 15 * 60,
        .long_break_interval = 4,
        .focus_target = 4,
        .tick_ms = 1000,
        .event_cb = pomodoro_event,
        .user_ctx = NULL,
    };
    qm_pomodoro_t timer = qm_pomodoro_create(&config);

    if (timer) {
        (void)qm_pomodoro_start(timer);
    }
}
```

Set a configuration field to `0` to use its Kconfig default. Set
`focus_target` to `QM_POMODORO_FOCUS_TARGET_FOREVER` for a never-ending
session. `tick_ms` must be at least 100 ms and does not need to evenly divide
phase durations. Remaining time is capped on the last tick, so phase completion
is reported on the first timer tick at or after the configured duration.

把配置字段设为 `0` 时，会使用对应的 Kconfig 默认值。
`focus_target` 设为 `QM_POMODORO_FOCUS_TARGET_FOREVER` 表示无限循环。
`tick_ms` 至少为 100 ms，且不需要整除各阶段时长。最后一个 tick 会按
剩余时间截断，所以阶段完成事件会在不早于配置时长的第一个定时器
tick 上触发。

## Single Round

Set `single_round` to `QM_TRUE` for a one-time two-phase workflow: one focus
phase (`focus_sec`) followed by one short break (`short_break_sec`), then
`QM_POMODORO_EVENT_FINISHED` and the timer stops. In this mode `focus_target`
and `long_break_*` are ignored.

将 `single_round` 设为 `QM_TRUE` 即可实现一次性两阶段流程：一个专注阶段
（`focus_sec`） + 一个短休息阶段（`short_break_sec`），随后触发
`QM_POMODORO_EVENT_FINISHED` 并停止。此模式下 `focus_target` 和
`long_break_*` 不生效。

```c
qm_pomodoro_config_t config = {
    .focus_sec = 15 * 60,
    .short_break_sec = 5 * 60,
    .single_round = QM_TRUE,
    .event_cb = pomodoro_event,
    .user_ctx = NULL,
};
```
