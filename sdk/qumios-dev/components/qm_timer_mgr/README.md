# qm_timer_mgr

`qm_timer_mgr` is a qumi countdown / stopwatch timer manager component. It only
depends on qumi kernel primitives and common types:

- `qm_kernel` for timer, mutex, and memory allocation
- `qm_errno` for return codes
- `qm_types` for fixed-width types and `qm_bool`

The component does not expose platform, IDF, FreeRTOS, or mbedTLS types in its
public API. Phase orchestration and platform interactions (UI, audio, motor,
cloud) belong to the upper layer, which consumes the event callback.

The name is `qm_timer_mgr` (not `qm_timer`) to avoid clashing with the kernel
timer type/functions `qm_timer_*` in `qm_kernel.h` and the SoC timer HAL
`include/hal/soc/qm_timer.h`.

`qm_timer_mgr` 是 qumi 的倒计时 / 秒表计时管理器组件，只依赖 qumi 的内核原语和通用类型：

- `qm_kernel`：定时器、互斥锁和内存分配
- `qm_errno`：返回码
- `qm_types`：固定宽度类型和 `qm_bool`

它的公共 API 不暴露 platform、IDF、FreeRTOS 或 mbedTLS 类型。
阶段编排和平台交互（UI、音频、电机、云端）属于上层，由事件回调消费。

命名取 `qm_timer_mgr`（而非 `qm_timer`），是为了避开 `qm_kernel.h` 里内核定时器的
`qm_timer_*` 类型/函数，以及 `include/hal/soc/qm_timer.h` 的 SoC 定时器 HAL。

## API

- `qm_timer_mgr_create`
- `qm_timer_mgr_destroy`
- `qm_timer_mgr_start`
- `qm_timer_mgr_pause`
- `qm_timer_mgr_resume`
- `qm_timer_mgr_restart`
- `qm_timer_mgr_stop`
- `qm_timer_mgr_get_status`
- `qm_timer_mgr_peek_status`
- `qm_timer_mgr_is_active`
- `qm_timer_mgr_mode_str`
- `qm_timer_mgr_state_str`

All control APIs return `QM_EOK` on success or a negative `QM_E*` error code.
`qm_timer_mgr_create` returns `NULL` on failure.
`qm_timer_mgr_destroy` marks the handle as destroying, stops later callbacks,
waits for in-flight API/timer operations to leave, and then releases resources.
Do not call it from `event_cb`.

所有控制接口成功返回 `QM_EOK`，失败返回负值 `QM_E*` 错误码。
`qm_timer_mgr_create` 失败时返回 `NULL`。
`qm_timer_mgr_destroy` 会先把句柄标记为销毁中，停止后续回调，等待
正在执行的 API / timer 回调退出，再释放资源。不要在 `event_cb`
里调用它。

## Basic Usage

```c
#include "qm_timer_mgr.h"

static void timer_event(qm_timer_mgr_t timer, const qm_timer_mgr_event_data_t *event)
{
    (void)timer;

    if (event->event == QM_TIMER_MGR_EVENT_TICK) {
        /* event->status.remaining_sec / elapsed_sec contain the countdown. */
    } else if (event->event == QM_TIMER_MGR_EVENT_FINISHED) {
        /* Countdown reached zero. */
    }
}

void app_start_countdown(void)
{
    qm_timer_mgr_config_t config = {
        .mode = QM_TIMER_MGR_MODE_COUNTDOWN,
        .duration_sec = 5 * 60,
        .tick_ms = 1000,
        .event_cb = timer_event,
        .user_ctx = NULL,
    };
    qm_timer_mgr_t timer = qm_timer_mgr_create(&config);

    if (timer) {
        (void)qm_timer_mgr_start(timer, QM_TIMER_MGR_MODE_COUNTDOWN, 5 * 60);
    }
}
```

Set a configuration field to `0` to use its Kconfig default. `tick_ms` must be
at least 100 ms and does not need to divide phase durations. Time is advanced by
wall clock, so delayed kernel timer callbacks do not cause drift.

把配置字段设为 `0` 时，会使用对应的 Kconfig 默认值。
`tick_ms` 至少为 100 ms，且不需要整除各阶段时长。计时基于墙钟推进，
内核定时器回调被延迟也不会造成累计漂移。

## Semantics

- Countdown: `remaining_sec` counts down to zero, then
  `QM_TIMER_MGR_EVENT_FINISHED` fires and the timer enters
  `QM_TIMER_MGR_STATE_FINISHED`.
- Stopwatch: `elapsed_sec` counts up forever; it never finishes.
- `restart` reuses the last used mode and duration.
- `stop` enters `QM_TIMER_MGR_STATE_STOPPED`; a countdown keeps its duration so
  `restart` can reuse it.

## Optional Cloud Adapter

When `QM_TIMER_MGR_CLOUD_SUPPORT` is enabled, `qm_timer_mgr_cloud` owns the
generic `qm_iot` `timer` method: registration, queueing, ACK replies and update
reports. Applications provide `command_cb` only for product fields such as
`timerType`, duration and guide voice. `status_payload_cb` is optional and
returns cJSON-allocated JSON when a product type, such as pomodoro, cannot be
represented by the generic countdown/stopwatch status.

`qm_timer_mgr_cloud_attach()` can attach this adapter once to a timer created
before the cloud session. The supplied configuration is borrowed until timer
destroy.

启用 `QM_TIMER_MGR_CLOUD_SUPPORT` 后，`qm_timer_mgr_cloud` 负责通用 `timer`
协议的注册、排队、ACK 回复和状态上报。应用只在 `command_cb` 中解释
`timerType`、时长和引导语音等产品字段。当番茄钟等产品类型不能由通用
倒计时/秒表状态表达时，可通过 `status_payload_cb` 返回 cJSON 分配的 JSON。
