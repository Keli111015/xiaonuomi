# 番茄钟示例

`apps/main/lj_components/pomodoro` 是平台 `qm_pomodoro` 组件的接入示例。

- `pomodoro_init()`：按 `qm_config.h` 的配置创建番茄钟；
- `pomodoro_on_connected()`：连云后注册 `method=pomodoro` 下行；
- 云端下发 `start` / `stop` / `pause` / `resume` 直接转给组件；
- 事件回调只打印剩余秒数。

## 接入

`main.c` 在闹钟示例下方调用，可用 `CONFIG_QM_POMODORO_SUPPORT` 关闭：

```c
#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
    pomodoro_init();
#endif
```

`ljyd_cloud.c` 在 `QM_IOT_EVENT_CLOUD_CONN` 时调用：

```c
(void)pomodoro_on_connected(iot_handle);
```

源文件在 `apps/main/CMakeLists.txt` 的 `COMPONENT_SRCS` 里。

## 配置

都在 `apps/main/qm_config/qm_config.h`：

```c
#define CONFIG_QM_POMODORO_FOCUS_SEC           25
#define CONFIG_QM_POMODORO_SHORT_BREAK_SEC     5
#define CONFIG_QM_POMODORO_LONG_BREAK_SEC      10
#define CONFIG_QM_POMODORO_LONG_BREAK_INTERVAL 2
#define CONFIG_QM_POMODORO_FOCUS_TARGET        4
#define CONFIG_QM_POMODORO_TICK_MS             1000
#define CONFIG_APP_POMODORO_AUTO_START         0
```

`CONFIG_APP_POMODORO_AUTO_START` 为 0 时上电不开始，等云端下发 `pomodoro/start`。

## 云端下行

```json
{"method": "pomodoro", "subMethod": "start"}
```

`subMethod` 也可以是 `stop`、`pause`、`resume`，同时兼容小写 `submethod`。
未知值只打日志。

## 日志

标签是 `pomodoro`：

```text
pomodoro: remain=1499
pomodoro: start ok
pomodoro: down registered: pomodoro
```
