# qm_pomodoro

`qm_pomodoro` is a qumi pomodoro timer component. It only depends on qumi
kernel primitives and common types:

- `qm_kernel` for timer, mutex, and memory allocation
- `qm_errno` for return codes
- `qm_types` for fixed-width types and `qm_bool`

The component does not expose platform, IDF, FreeRTOS, or mbedTLS types in its
public API.

`qm_pomodoro` 是 qumi 的番茄钟定时组件，只依赖 qumi 的内核原语和通用类型：

- `qm_kernel`：定时器、互斥锁和内存分配
- `qm_errno`：返回码
- `qm_types`：固定宽度类型和 `qm_bool`

它的公共 API 不暴露 platform、IDF、FreeRTOS 或 mbedTLS 类型。

## API

## 生命周期架构

```text
create -> idle -> focus -> short/long break -> focus
                |                  |
                +-- pause/resume --+
                +-- stop ----------> idle
                +-- target reached -> finished
```

组件负责定时器和状态切换；应用层负责 UI、通知、持久化和回调中的业务工作。

- `qm_pomodoro_create`
- `qm_pomodoro_destroy`
- `qm_pomodoro_start`
- `qm_pomodoro_pause`
- `qm_pomodoro_resume`
- `qm_pomodoro_stop`
- `qm_pomodoro_skip`
- `qm_pomodoro_get_status`

All control APIs return `QM_EOK` on success or a negative `QM_E*` error code.
`qm_pomodoro_create` returns `NULL` on failure.
`qm_pomodoro_destroy` marks the handle as destroying, stops later callbacks,
waits for in-flight API/timer operations to leave, and then releases resources.
Do not call it from `event_cb`.

所有控制接口成功返回 `QM_EOK`，失败返回负值 `QM_E*` 错误码。
`qm_pomodoro_create` 失败时返回 `NULL`。
`qm_pomodoro_destroy` 会先把句柄标记为销毁中，停止后续回调，等待
正在执行的 API / timer 回调退出，再释放资源。不要在 `event_cb`
里调用它。

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
