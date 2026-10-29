# QM Battery 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下调用 `qm_battery` 电量估算组件。

## 文件结构

```text
qumi/examples/qm_battery/
├── README.md
├── README_CN.md
├── qm_battery_example.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## 示例能力

- 默认使用模拟电压曲线运行，不依赖真实 ADC
- 可通过 `CONFIG_QM_BATTERY_EXAMPLE_USE_ADC` 切换到 ADC 采样路径
- 演示 `qm_battery_default_config()`、校准参数导入和 `qm_battery_init()`
- 演示开机恢复已保存 SOC、离线耗电修正和初始 OCV 下修
- 演示充满信号确认、学习满电电压、拔电后的满电保护区间
- 日志会输出 SOC、SOH、电压变化率、学习满电值和 guard 状态

## 主要配置

示例相关宏位于 `qumi/examples/qm_battery/qm_battery_example.c` 和
`qumi/examples/qm_battery/qm_config/qm_config.h`。

常用示例宏：

```text
CONFIG_QM_BATTERY_EXAMPLE_USE_ADC=0
CONFIG_QM_BATTERY_EXAMPLE_ADC_SCALE_V_PER_COUNT=0.001f
CONFIG_QM_BATTERY_EXAMPLE_ADC_OFFSET_V=0.0f
CONFIG_QM_BATTERY_EXAMPLE_ADC_DIVIDER_RATIO=1.0f
CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM=60
CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV=4180
CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV=3300
CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV=4050
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_ENABLE=1
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_PERCENT=72
CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_ENABLE=1
CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV=4120
```

## 运行入口

工程入口：

```c
qm_application_start();
```

也可以直接调用示例函数：

```c
qm_battery_example_start();
```

## 接入方式

这个目录下的示例文件不会自动参与工程构建。接入时可将
`qumi/examples/qm_battery/qm_battery_example.c` 加入工程源码列表，并将
`qumi/examples/qm_battery/qm_config/qm_config.h` 中相关配置合并到项目配置头。
