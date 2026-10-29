# QM Battery Gauge 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下调用 `qm_battery_gauge` 电量计组件。

## 文件结构

```text
qumi/examples/qm_battery_gauge/
├── README.md
├── README_CN.md
├── qm_battery_gauge_example.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## 示例能力

- 默认使用模拟电压/电流曲线运行，不依赖真实采样芯片
- 曲线覆盖 1A 左右放电、截止、静置恢复、充电和满电确认
- 分别演示完整电流、仅充电电流、仅电压三种 `current_mode`
- 演示 `qm_battery_gauge_default_config()` 和 `qm_battery_gauge_init()`
- 演示休眠恢复保存的 SOC
- 演示掉电重启后的 SOC 恢复
- 演示未知离线时长时的保守恢复
- 日志输出 SOC、原始 SOC、补偿电压、剩余容量、累计充放电容量和状态变化

## 电流方向

```text
current_ma > 0  表示充电
current_ma < 0  表示放电
```

示例放电点使用 `-1040.9mA`，和提供的电池测试数据一致。

三种电流能力模式：

- `QM_BATTERY_GAUGE_CURRENT_FULL`：充放电电流都有效，完整库仑计模式
- `QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY`：只有充电电流有效，放电靠电压估算
- `QM_BATTERY_GAUGE_CURRENT_VOLTAGE_ONLY`：没有电流采样，SOC 主要跟随电压估算

## 主要配置

```text
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_DESIGN_CAPACITY_MAH=5200
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_USABLE_CAPACITY_MAH=5300
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_R_INTERNAL_MOHM=80
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_ENABLE=1
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_PERCENT=100
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE=1
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE=1
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE=1
CONFIG_QM_BATTERY_GAUGE_EXAMPLE_STEP_DELAY_MS=250
```

其中：

- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE` 控制是否运行休眠恢复演示
- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE` 控制是否运行掉电恢复演示
- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE` 控制是否运行未知离线时长恢复演示

这三个宏只控制 example 场景，不影响 `qm_battery_gauge` 组件本身的恢复能力。

如果不是这颗测试电池，请不要只改模拟曲线。实际产品至少要重新设置：

- `design_capacity_mah`：电芯标称容量
- `usable_capacity_mah`：整机可用容量，可用一次完整充放电实测得到
- `internal_resistance_ohm`：电芯、保护板和走线的等效内阻
- `low_voltage_v` / `empty_voltage_v` / `cutoff_voltage_v`：低电、空电和保护阈值
- `full_voltage_v` / `full_current_ma`：满电确认阈值
- `ocv_table` / `ocv_table_count`：目标电池的 OCV-SOC 表

在这个示例里，配置入口在：

```text
qumi/examples/qm_battery_gauge/qm_battery_gauge_example.c
```

OpenCPU 版本对应入口在：

```text
apps/opencpu/app/qm_battery_gauge_example.c
```

可以在 `qm_battery_gauge_default_config(&cfg);` 后面覆盖：

```c
static const qm_battery_gauge_ocv_point_t s_product_ocv_table[] = {
    {  0.0f, 3.050f, 0.0f },
    { 10.0f, 3.420f, 0.0f },
    { 20.0f, 3.520f, 0.0f },
    { 30.0f, 3.610f, 0.0f },
    { 40.0f, 3.680f, 0.0f },
    { 50.0f, 3.740f, 0.0f },
    { 60.0f, 3.800f, 0.0f },
    { 70.0f, 3.870f, 0.0f },
    { 80.0f, 3.950f, 0.0f },
    { 90.0f, 4.070f, 0.0f },
    {100.0f, 4.200f, 0.0f },
};

cfg.design_capacity_mah = 5000.0f;
cfg.usable_capacity_mah = 4900.0f;
cfg.internal_resistance_ohm = 0.065f;
cfg.ocv_table = s_product_ocv_table;
cfg.ocv_table_count =
    sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);
```

OpenCPU 工程的宏默认值在：

```text
apps/opencpu/qm_config/qm_config.h
```

组件内置兜底 OCV 表在：

```text
qumi/components/qm_battery_gauge/src/qm_battery_gauge.c
```

量产项目建议使用应用侧 OCV 表覆盖，保留组件默认表只作为 bring-up fallback。

换电池后的容量、OCV 表、内阻和阈值测试方法见组件文档：

```text
qumi/components/qm_battery_gauge/README_CN.md
```

## 运行入口

工程入口：

```c
qm_application_start();
```

也可以直接调用：

```c
qm_battery_gauge_example_start();
```
