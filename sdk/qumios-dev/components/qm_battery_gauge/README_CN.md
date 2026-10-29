# qm_battery_gauge

`qm_battery_gauge` 是一个 1S 锂电池电量计组件，用电流、电压和可选温度估算电池
SOC。

组件定位是产品业务层可直接使用的电量计：

- 以库仑计积分作为主要 SOC 来源
- 用电压查表修正长期漂移
- 用内阻模型补偿负载压降：`V_ocv = V_terminal - I * R`
- 支持完整电流、仅充电电流和仅电压三种电流能力模式
- 不使用动态内存，handle 由调用方静态分配
- 默认表来自提供的 18650 5200mAh 测试数据

## 职责边界

```text
ADC / 电流监测芯片 / 充电状态
                | 采样
                v
        qm_battery_gauge
                | SOC 和快照
                v
           应用策略层
                | 存储、UI、关机
```

组件只根据调用方提供的采样估算电池状态；不配置传感器、不持有周期采样任务、默认不持久化快照，也不决定产品关机或充电策略。

## 电流方向

组件约定：

```text
current_ma > 0  表示电流流入电池，正在充电
current_ma < 0  表示电流流出电池，正在放电
```

这和测试数据一致：放电工步电流约为 `-1040mA`。

如果硬件只有充电电流，没有放电电流，可以使用：

```c
cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY;
```

此时充电阶段用 `current_ma` 积分，放电阶段不做放电库仑积分，而是用电压查 OCV 表估算
SOC。放电样本应设置 `current_valid = QM_FALSE`，避免把“未知电流”误当成真实 `0mA`。

## 最小接入

```c
#include "qm_battery_gauge.h"

static qm_battery_gauge_t g_battery_gauge;

void app_battery_init(void)
{
    qm_battery_gauge_config_t cfg;

    qm_battery_gauge_default_config(&cfg);
    cfg.design_capacity_mah = 5200.0f;
    cfg.usable_capacity_mah = 5300.0f;
    cfg.internal_resistance_ohm = 0.080f;
    cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_FULL;
    qm_battery_gauge_init(&g_battery_gauge, &cfg, NULL, NULL);
}

void app_battery_tick(qm_float voltage_v, qm_float current_ma, qm_bool charging)
{
    qm_battery_gauge_sample_t sample = {
        .voltage_v = voltage_v,
        .current_ma = current_ma,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .current_valid = QM_TRUE,
        .has_temperature = QM_FALSE,
        .charging = charging,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_update_sample(&g_battery_gauge, &sample);
}
```

## 硬件接入

`qm_battery_gauge` 不绑定具体采样芯片。外部驱动只需要把测到的电池端电压和电流
喂给 `qm_battery_gauge_update_sample()`。

推荐第一版硬件路径：

```text
电池 -> 采样电阻 -> 电流/电压检测芯片 -> MCU -> qm_battery_gauge
```

可选采样方案：

- INA226 / INA228：I2C 读取电流、电压，适合快速做原型
- ADC + 分流放大器：成本可控，但需要做好 ADC 和放大器校准
- 充电管理芯片满电信号：可接入 `charge_full`，用于更可靠地锚定 100%

对 5200mAh 电芯的采样电阻建议：

- 1A 左右负载：`10mOhm` 到 `20mOhm` 比较方便
- 真 3C 负载，约 15.6A：建议 `2mOhm` 到 `5mOhm`，并校核采样电阻功耗和温升

## 默认参数

默认配置基于提供的测试数据：

- 标称容量：`5200mAh`
- 可用容量：`5300mAh`
- 负载补偿内阻：`80mOhm`
- 低电 SOC 阈值：`10%`
- 低电压阈值：`3.3V`
- 空电锚点：`3.0V`
- 截止电压：`2.75V`
- 满电电压：`4.19V`
- 满电电流：`250mA`

测试数据中的完整放电工步：

- 放电电流约 `-1040.9mA`
- 放出容量约 `5295.9mAh`
- 端电压从 `4.122V` 到 `2.749V`
- 截止后静置电压回升到约 `3.088V`

## 换电池时怎么改

默认表只适合这次提供的 1S 18650 5200mAh 数据做 bring-up。如果实际项目换了电芯、
电池包、保护板、采样电阻、主负载电流，建议在应用初始化时覆盖配置和 OCV 表。

建议在应用初始化处覆盖配置和 OCV 表，不建议直接改组件默认表。

应用侧覆盖方式：

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

qm_battery_gauge_config_t cfg;

qm_battery_gauge_default_config(&cfg);
cfg.design_capacity_mah = 5000.0f;
cfg.usable_capacity_mah = 4900.0f;
cfg.internal_resistance_ohm = 0.065f;
cfg.low_voltage_v = 3.30f;
cfg.empty_voltage_v = 3.00f;
cfg.cutoff_voltage_v = 2.75f;
cfg.full_voltage_v = 4.19f;
cfg.full_current_ma = 250.0f;
cfg.ocv_table = s_product_ocv_table;
cfg.ocv_table_count =
    sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);

qm_battery_gauge_init(&g_battery_gauge, &cfg, NULL, NULL);
```

OCV 表要求：

- 电压必须从低到高排列，SOC 必须从 `0%` 到 `100%` 递增
- 推荐至少有 `0/5/10/.../100%` 共 21 个点
- 表里的电压应尽量使用静置后的开路电压，而不是大电流负载下的端电压
- 如果只能拿到负载放电曲线，需要先用 `V_ocv = V_terminal - I * R` 做近似补偿
- `internal_resistance_ohm` 要用目标电芯、保护板和整机走线的实测值重新调

初始化规则：

- `qm_battery_gauge_init(&gauge, NULL, NULL, NULL)` 会使用组件默认配置和默认 OCV 表
- 显式传入 `cfg` 时，`cfg.ocv_table` 和 `cfg.ocv_table_count` 必须合法
- OCV 表非法时会返回 `-QM_EINVAL`，不会静默回退到默认测试表

## 功能宏

组件只保留总开关 `CONFIG_QM_BATTERY_GAUGE_SUPPORT`。快照、休眠恢复和断电恢复是组件
标准能力，不再单独拆宏。

如果需要组件直接提供快照持久化辅助接口，可以打开：

```text
CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT=1
```

打开后可使用：

```c
qm_battery_gauge_save_snapshot(&gauge, NULL, seq, now_s);
qm_battery_gauge_load_snapshot(NULL, &snapshot);
```

`key` 传 `NULL` 时使用默认 key：`QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY`。这个辅助接口
只做单 key 保存/读取；双 slot、保存频率、什么时候强制落盘，仍建议由应用层决定。

接口分层说明：

- `qm_battery_gauge_make_snapshot()` 是基础快照导出能力，用于应用层自己管理 RTC memory、
  双 slot、版本兼容或多份候选快照
- `qm_battery_gauge_save_snapshot()` / `qm_battery_gauge_load_snapshot()` 是单 key 快捷存储接口，
  适合快速接入，不替代应用层的存储策略管理

不建议再为充电效率、死区电流、静置确认时间、滤波时间常数等内部算法参数增加宏。
这些属于组件内部稳定策略；产品需要改的是容量、内阻、阈值、OCV 表和温度补偿。

## 换电池后的测试方法

换电芯后，建议至少测三类数据：容量、OCV-SOC 表、内阻/压降。测试环境尽量固定在
`25C` 左右，电池需要使用目标产品里的保护板、线材和采样路径，这样测到的数据才和
整机一致。

推荐设备：

- 电池测试仪，或电子负载加可编程电源/充电器
- 万用表，用于校准电压采样
- 电流表或高精度分流器，用于校准电流采样
- 温度计或温度传感器
- 温箱，可选，用于做低温/高温修正

推荐测试流程：

1. 校准采样链路

   先用万用表和电流表对比 MCU 读数，修正电压分压比、ADC 偏移、电流零点和电流比例。
   `qm_battery_gauge` 假设传入的 `voltage_v` 和 `current_ma` 已经是校准后的真实值。

2. 满充测试

   按目标产品的充电策略充到满电，例如 `4.2V` 恒压、充电电流降到 `C/20` 或充电芯片
   拉高满电信号。记录满电端电压、满电确认电流、是否有 `charge_full` 信号。静置
   `30min` 到 `60min` 后记录顶部开路电压。

3. 容量和截止测试

   用目标产品的典型负载电流放电到保护板关断或产品定义的截止电压。记录完整曲线：
   时间、电压、电流、温度、放出容量 `mAh`、放出能量 `mWh`。建议至少测 `3` 块新电池，
   取平均值作为 `usable_capacity_mah`，最差值用于评估低电保护余量。

4. OCV-SOC 表测试

   从满电静置后的 `100%` 开始，每放出 `5%` 容量就停止放电并静置 `30min` 到 `60min`，
   或静置到电压变化小于约 `1mV/10min`。记录静置后的电压，得到
   `100/95/90/.../0%` 的 OCV-SOC 表。这个表填到 `ocv_table`。

5. 内阻/负载压降测试

   在几个典型 SOC 点做负载阶跃，例如 `100%`、`80%`、`50%`、`20%`。记录负载打开前的
   静置电压、负载稳定后的端电压和负载电流。近似计算：

   ```text
   R = (V_rest - V_load) / I_load
   ```

   多个 SOC 点取一个保守平均值，填到 `internal_resistance_ohm`。

6. 充电回灌测试

   从低电开始按产品充电路径充电，记录充电电流、电压、累计充入容量、满电信号和满电
   时刻。这个测试用于确认 `full_voltage_v`、`full_current_ma` 和 `charge_full`
   是否合理。

需要拿到的数据和配置字段对应关系：

| 测试数据 | 用途 |
| --- | --- |
| 电芯标称容量 | `design_capacity_mah` |
| 完整放电容量平均值 | `usable_capacity_mah` |
| 静置 OCV-SOC 表 | `ocv_table` / `ocv_table_count` |
| 负载阶跃压降 | `internal_resistance_ohm` |
| 满电端电压 | `full_voltage_v` |
| 满电尾电流 | `full_current_ma` |
| 产品低电提示点 | `low_soc_percent` / `low_voltage_v` |
| 产品空电提示点 | `empty_voltage_v` |
| 保护板或产品截止电压 | `cutoff_voltage_v` |
| 充电满电信号 | `sample.charge_full` |
| 温度曲线，可选 | `temp_coeff_v_per_c` / `temp_comp_enable` |

如果时间有限，第一版至少要测完整放电容量、完整放电电压曲线、满电阈值和截止阈值。
后续再补静置 OCV 表和内阻阶跃数据，SOC 会更稳。

## 算法说明

每次更新时，组件会先做电流积分：

```text
delta_mah = current_ma * dt_s / 3600
```

放电时 `delta_mah` 为负，剩余容量下降；充电时 `delta_mah` 为正，组件内部会按固定的
充电效率系数折算后计入剩余容量。

然后组件用内阻补偿计算近似开路电压：

```text
V_ocv = V_terminal - I * R
```

因为放电电流为负，所以放电时补偿电压会高于实测端电压；充电电流为正，所以充电时
补偿电压会低于实测端电压。

最后根据补偿电压查 SOC 表，并按当前是否静置使用不同速度修正 SOC：

- 负载运行中：慢速修正，避免电压平台区跳变
- 小电流静置足够久：快速修正，降低库仑计长期漂移
- 实测端电压低于截止电压：直接进入空电状态
- 满电信号或满电电压/小电流满足确认时间：锚定到 100%

## 长时间休眠

`dt_s` 表示本次采样距离上一次更新过去的时间，单位是秒。组件会用 `dt_s` 做库仑计
积分，但也会用 `max_update_dt_s` 限制单次积分时间，防止异常时间戳让 SOC 突然跳变。

普通周期采样时，例如每 `1s` 更新一次：

```c
sample.dt_s = 1.0f;
qm_battery_gauge_update_sample(&gauge, &sample);
```

设备长时间深睡后，不建议把几小时直接填到 `dt_s`。推荐使用组件内置的“睡眠平均
电流模型 + 唤醒电压校正”。

如果唤醒后还是同一个 `qm_battery_gauge_t` 对象，只想修正 SOC，推荐下面这条轻休眠
恢复路径：

1. 睡前调用 `qm_battery_gauge_make_snapshot()` 生成快照
2. 应用层把快照保存到 RTC memory、NVS 或其他存储
3. 唤醒后读取快照、睡眠时长、平均睡眠电流和唤醒电压/电流
4. 调用 `qm_battery_gauge_estimate_restore_soc()` 得到恢复 SOC
5. 调用 `qm_battery_gauge_set_soc()` 写回组件
6. 用 `dt_s = 1.0f` 做一次正常短间隔采样更新

示例：

这些输入通常分别来自：快照存储、RTC/时间戳差值、实测休眠平均电流、唤醒后的 ADC
电压/电流采样，以及充电芯片状态脚。

```c
qm_battery_gauge_snapshot_t snapshot;
qm_battery_gauge_restore_config_t restore = { 0 };
qm_battery_gauge_restore_result_t restore_result;

qm_battery_gauge_make_snapshot(&gauge, &snapshot, seq, now_s);

restore.snapshot = &snapshot;
restore.offline_time_valid = QM_TRUE;
restore.offline_time_s = sleep_time_s;
restore.offline_current_ma = sleep_current_ma;
restore.wake_voltage_v = wake_voltage_v;
restore.wake_current_ma = wake_current_ma;
restore.wake_temperature_c = wake_temperature_c;
restore.has_temperature = has_temperature;
restore.charging = charger_online;
restore.charge_full = charge_full;

qm_battery_gauge_estimate_restore_soc(&cfg, &restore, &restore_result);
qm_battery_gauge_set_soc(&gauge, restore_result.final_soc_percent);

sample.voltage_v = wake_voltage_v;
sample.current_ma = wake_current_ma;
sample.dt_s = 1.0f;
sample.charging = charger_online;
sample.charge_full = charge_full;
qm_battery_gauge_update_sample(&gauge, &sample);
```

如果是断电重启、深睡丢 RAM、或者需要重新构造 gauge 对象，推荐直接调用：

```c
qm_battery_gauge_init(&gauge, &cfg, &restore, &restore_result);
```

组件内部会按平均睡眠电流扣减容量：

```text
offline_drain_mah = offline_current_ma * offline_time_s / 3600
```

然后用唤醒电压查 OCV 表并按离线时间自动分配权重。离线时间越长，保存 SOC 的可信度
越低，唤醒电压的权重越高。如果唤醒时 `charge_full` 有效且电压达到满电阈值，恢复
结果会直接锚定到 `100%`。

## 断电关机恢复

断电关机分两种情况：

- 可控关机：系统能收到关机事件，有时间保存最新 SOC
- 突然断电：系统没有机会保存，只能使用最近一次周期快照

组件不绑定 NVS、RTC memory 或文件系统。组件提供标准快照结构
`qm_battery_gauge_snapshot_t`、字段合法性检查和恢复算法。存储完整性可以交给 qm kv
等存储层处理，应用层负责存储、磨损均衡以及选择最新有效快照。

如果工程启用了 `CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT`，也可以直接使用组件的
`qm_battery_gauge_save_snapshot()` / `qm_battery_gauge_load_snapshot()` 保存/读取单份快照；
这适合先快速接入。量产如果需要双备份 slot，仍建议应用层管理 key 和保存策略。

建议使用双备份 slot 保存，例如 `snapshot_a` / `snapshot_b`。每次写入递增 `seq`，
开机时先按 `magic/version/范围` 过滤候选快照，再选择 `seq` 最新的一份。

保存策略：

- 正常运行时每隔一段时间调用 `qm_battery_gauge_make_snapshot()` 保存一次，例如 SOC 变化
  超过 `1%`、电压变化明显、或每 `5min` 保存一次
- 可控关机时立即调用 `qm_battery_gauge_make_snapshot()` 保存一次最新快照
- 避免每秒写 Flash，防止磨损；频繁快照可以先放 RTC memory，低频同步到 NVS

开机恢复流程：

1. 读取最近一次有效快照
2. 读取开机电压、电流、充电器在线状态和满电信号
3. 填充 `qm_battery_gauge_restore_config_t`
4. 调用 `qm_battery_gauge_init()` 并传入 `restore` 初始化组件
5. 用 `dt_s = 1.0f` 做一次正常采样更新

示例：

```c
qm_battery_gauge_restore_config_t restore = { 0 };
qm_battery_gauge_restore_result_t restore_result;

restore.snapshot = best_snapshot_or_NULL;
restore.offline_time_valid = has_time;
restore.offline_time_s = off_time_s;
restore.offline_current_ma = off_current_ma;
restore.wake_voltage_v = boot_voltage_v;
restore.wake_current_ma = boot_current_ma;
restore.wake_temperature_c = boot_temperature_c;
restore.has_temperature = has_temperature;
restore.charging = charger_online;
restore.charge_full = charge_full;

qm_battery_gauge_init(&gauge, &cfg, &restore, &restore_result);
```

如果无有效快照，组件会直接使用唤醒电压查 OCV 表得到初始 SOC。如果快照很旧，组件会
自动提高唤醒电压的权重；如果关机时间未知，会使用保存 SOC 和唤醒电压各占一半的保守
融合结果。

如果开机时 `charge_full` 已经有效，并且电压达到满电阈值，可以让组件的满电锚定逻辑把
SOC 校准到 `100%`。

## 标定建议

默认表可以用于 bring-up，但量产建议替换为目标电芯和整机电池包的实测数据。

推荐标定项：

- 静置 OCV-SOC 表，至少覆盖 `0/5/10/.../100%`
- 电流采样零点和比例
- 电压采样分压比和 ADC 偏移
- 新电池等效内阻
- 典型负载下的电压跌落曲线
- 满电电流阈值和低电保护阈值

如果有温度传感器，可以给 OCV 表补充 `temp_coeff_v_per_c`，并打开
`temp_comp_enable`。
