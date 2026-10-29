# QM Pomodoro 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下使用 `qm_pomodoro` 番茄钟组件。

## 文件结构

```text
qumi/examples/qm_pomodoro/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_pomodoro_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- 创建和销毁番茄钟实例
- 演示 start、pause、resume、skip、stop 流程
- 演示专注、短休息、长休息、完成等状态
- 通过事件回调打印状态变化
- 查询剩余时间、已运行时间和已完成专注次数

## 示例配置

```text
CONFIG_QM_POMODORO_EXAMPLE_FOCUS_SEC=4
CONFIG_QM_POMODORO_EXAMPLE_SHORT_BREAK_SEC=2
CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_SEC=3
CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_INTERVAL=2
CONFIG_QM_POMODORO_EXAMPLE_FOCUS_TARGET=3
CONFIG_QM_POMODORO_EXAMPLE_TICK_MS=700
```

## 运行入口

```c
qm_application_start();
qm_pomodoro_example_start();
qm_pomodoro_example_stop();
```

`qm_application_start()` 会调用 `qm_pomodoro_example_start()`。

## 接入方式

示例位于 `qumi/examples/qm_pomodoro/`。接入时将
`qumi/examples/qm_pomodoro/qm_pomodoro_example.c` 加入工程源码列表，并将
`qumi/examples/qm_pomodoro/qm_config/qm_config.h` 中相关配置合并到项目配置头。
