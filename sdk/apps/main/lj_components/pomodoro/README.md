# 番茄钟应用接入

本目录将官方 `qm_pomodoro` 计时器接入 MorphCube 学习任务、Agent、摄像头和云端方法。2026-10-31 按源码静态更新，本轮未编译或测试。

## 初始化和控制

- `main.c` 按 `CONFIG_QM_POMODORO_SUPPORT` 调用 `pomodoro_init()`。
- 创建官方计时器后调用 `morphcube_study_runtime_init(g_pomodoro)`，保持唯一计时时钟。
- 连云后 `pomodoro_on_connected(iot_handle)` 注册 `method=pomodoro` 下行。
- 云端支持 `start` / `stop` / `pause` / `resume`，兼容 `subMethod` 和 `submethod`。当前处理函数没有暴露 `skip` 或 `distracted` 下行，不能沿用早期适配说明。

```json
{"method":"pomodoro","subMethod":"start"}
```

成功日志只表示对应 API 返回成功，Agent 的口头回复更不能代替设备计时状态核对。

## 当前配置

来源：`main/qm_config/qm_config.h`；时长宏单位为秒。

| 配置 | 值 |
| --- | --- |
| FOCUS_SEC | 1500（25 分钟） |
| SHORT_BREAK_SEC | 300（5 分钟） |
| LONG_BREAK_SEC | 900（15 分钟） |
| LONG_BREAK_INTERVAL / FOCUS_TARGET | 4 / 4 |
| TICK_MS | 1000 毫秒 |
| APP_POMODORO_AUTO_START | 0，上电不自动开始 |

## 事件回调

`on_pomodoro_event()` 将生命周期事件非阻塞投递给学习任务，同时：

- Tick 检查启动提示音、记录剩余时间。
- 专注未暂停时每 10 秒请求一次姿态检测；相机不可用或语音忙时跳过并记日志。
- 恢复、停止、结束等事件调整 Agent 静音状态；离开专注或暂停时停止摄像头流。
- 专注开始通知 Agent 处理启动提示音时序。

HP、耗尽暂停和专注结束摆头由学习任务处理，而非直接在计时回调中等待动作。完整规则见[学习规则](../../../../../docs/STUDY_RUNTIME.md)，当前问题见[交接](../../../../../docs/CURRENT_HANDOFF.md)。
