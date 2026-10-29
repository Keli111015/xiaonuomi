# QM Alarm 示例

English documentation: [README.md](README.md)

这个示例把可复用的 `qm_alarm` 用法放在 `qumi/examples` 下。
可直接在 ESP OpenCPU 工程里运行的入口则独立实现于
`apps/opencpu/app/qm_alarm_example.c`。

## 文件

```text
qumi/examples/qm_alarm/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_alarm_example.c
├── qm_alarm_example.h
└── qm_config/
    └── qm_config.h
```

## 示例内容

- 演示 `qm_alarm` 的创建、启动、上报、停止和销毁。
- 演示在云端会话就绪后创建一个本地示例闹钟。
- 演示通过已有 `qm_iot` 句柄注册 `alarm` / `alarmReply` MQTT 方法。
- 演示通过回调观察响铃、关闭和稍后提醒动作。
- 启动流程对齐 `qm_player`：固定 Wi-Fi 或 BLE 配网 -> dynreg
  -> `qm_iot` -> `qm_alarm`。

## 入口函数

```c
if (qm_alarm_example_start(iot_handle) != QM_EOK) {
    /* handle startup error */
}
qm_alarm_example_stop();
```

## 说明

- `qm_alarm` 依赖已经启动完成的 `qm_iot` 句柄，因为组件会在设备 MQTT 会话上
  注册 `alarm` 和 `alarmReply` 方法。
- OpenCPU 侧可直接运行的入口在
  `apps/opencpu/app/qm_alarm_example.c`，它在一个自包含文件里完成
  Wi-Fi、dynreg、`qm_iot` 和本地 `qm_alarm` 的启动，不依赖
  `qumi/examples/qm_alarm/qm_alarm_example.c`。
- 当 `CONFIG_QM_ALARM_EXAMPLE_WIFI_SSID` 为空时，OpenCPU 包装入口会退回到
  BLE 配网流程，并支持从配网包里带入 dynreg host。
- 该示例可以通过 `CONFIG_QM_ALARM_EXAMPLE_USE_FIXED_TIME` 选择固定时间，
  也可以根据当前本地时间自动创建一个“几分钟后响铃”的近未来闹钟。
