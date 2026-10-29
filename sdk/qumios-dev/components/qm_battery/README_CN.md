# qm_battery

`qm_battery` 是基于电压输入的 1S 锂电池 SOC/SOH 估算组件，核心算法使用 UKF。

## 能力

## 架构

```text
ADC / 电压源 / 充电状态
          | 采样
          v
       qm_battery
          | SOC、SOH、内阻
          v
       应用策略层
```

组件不配置 ADC、不持有电量计硬件、不负责状态持久化，也不决定 UI、充电和关机策略。

- 输入：电池电压、可选温度、充电状态、采样间隔
- 输出：SOC、SOH、等效内阻、静置状态
- 支持纯电压更新：`qm_battery_update_voltage()`
- 支持可选 ADC 采样：`qm_battery_update_adc()`
- 无动态内存、无 OS 依赖，handle 由调用方静态分配
- 支持用户替换 OCV-SOC 表
- OpenCPU 示例入口可通过 `APP_DEMO_SELECT == APP_DEMO_BATTERY` 启动

## 重要限制

没有电流采样时，动态负载下 SOC/SOH 不完全可观测。量产精度依赖：

- 目标电芯 OCV-SOC 实测表
- ADC 分压与参考电压校准
- 典型负载电压日志
- 新电池内阻基准

温度未知时，建议关闭温度补偿，组件默认使用 `default_temperature_c`。

## 最小使用

```c
static qm_battery_t g_battery;

void app_init(void)
{
    qm_battery_config_t cfg;

    qm_battery_default_config(&cfg);
    cfg.temp_comp_enable = QM_FALSE;
    cfg.default_temperature_c = 25.0f;
    qm_battery_init(&g_battery, &cfg);
}

void app_tick(qm_float voltage_v, qm_bool charging, qm_float dt_s)
{
    qm_battery_update_voltage(&g_battery,
                                  voltage_v,
                                  25.0f,
                                  QM_FALSE,
                                  charging,
                                  QM_FALSE,
                                  dt_s);
}
```

## ADC 使用

ADC 原始值到电池电压的换算由配置决定：

```text
battery_voltage = (adc_raw * adc_scale_v_per_count + adc_offset_v) * adc_divider_ratio
```

如果平台 ADC 返回电池采样点毫伏值，且电阻分压为 `VBAT -> Rtop -> ADC -> Rbottom -> GND`：

```text
adc_scale_v_per_count = 0.001
adc_divider_ratio = (Rtop + Rbottom) / Rbottom
```

若当前平台未启用 `CONFIG_QM_ADC_SUPPORT`，`qm_battery_update_adc()` 返回 `-QM_ENOSYS`。
示例代码默认使用纯电压 trace；在支持 `qm_adc` 的平台上可打开
`QM_BATTERY_EXAMPLE_USE_ADC` 并替换 ADC 端口/引脚后切换到真实采样。

## 充电芯片满电信号

如果硬件能提供充电完成信号，调用 `qm_battery_update_voltage()` 或
`qm_battery_update_adc()` 时传入 `charge_full`。`charging` 表示充电器接入或
充电通路有效，`charge_full` 表示充电芯片确认充满，两个状态不要混用。

满电信号生效条件：

- `charging = QM_TRUE`
- `charge_full = QM_TRUE`
- 电池端电压不低于 `charge_full_min_v`
- 信号持续时间达到 `charge_full_confirm_s`

确认后组件会直接把 SOC 锚定到 100%，并学习当前满电电压到
`learned_full_v`。同一次连续满电信号只学习一次；下一次重新进入确认满电后，
再按 `learned_full_blend` 慢速更新，默认每个确认满电周期移动 8%。这样老化电池
满电电压逐步降低时，组件会跟随变化，但不会被一次异常电压快速改写。

`charge_full_min_v` 只是满电信号的最低合理电压保护，不应配置成“最大容量电压”
或 OCV 表 100% 点。否则 ADC 偏差、电池老化、拔电回落都可能导致充电芯片已经
确认满电但组件无法显示 100%。如果充电芯片已经可靠确认满电，组件以该信号作为
100% 的最高置信度来源。

确认满电后组件会启动 recent full guard。默认 `full_guard_s = 1800s`，
`full_guard_min_percent = 98%`；在保护时间内，拔电后的静置 OCV 校正或低电锚点
不会马上把 SOC 拉到 98% 以下。这个保护用于覆盖 ADC 偏移、电池老化和拔电后电压
回落造成的顶部误差。超过保护时间后，组件再允许 OCV 和电压锚点按正常逻辑慢速修正。

`learned_full_v` 不是非充电状态的满电判断标准；非充电状态仍不能仅靠端电压
判定已经充满。它只作为后续充电锚点和无满电信号场景的兜底电压参考。电压仍只
作为合理性校验，不代替充电芯片的满电事实。

## 标定建议

- OCV 表：按 0/5/10/20/.../100% SOC 静置测量，替换默认表
- `k1_soc_per_vps`：通过典型放电曲线调节电压斜率对 SOC 的影响
- `k2_current_per_vps`：通过负载阶跃日志调节动态压降模型
- `q_soc/q_soh/q_r`：越大响应越快，越小越稳定
- `rm_base_v2/rm_dynamic_scale`：ADC 噪声或负载动态越大，取值越大
- `charge_settle_s/charge_settle_rm_scale`：插电、拔电后端电压未稳定时，用于降低瞬态电压对 SOC/SOH 的影响；默认值来自 `CONFIG_QM_BATTERY_CHARGE_SETTLE_S` 和 `CONFIG_QM_BATTERY_CHARGE_SETTLE_RM_SCALE`
- `full_anchor_v/empty_anchor_v`：按电芯规格和整机保护阈值设置

## 量产需要人工测量的内容

- 单体或电池包的静置 OCV-SOC 曲线
- ADC 分压比和零点偏差
- 满电与低电锚点电压
- 典型负载下的电压跌落曲线
- 新电池的等效内阻基准
- 如果有温度传感器，再测温度补偿系数

## 标定辅助工具

组件提供离线脚本 `tools/qm_battery_calibrate.py`。脚本只依赖 Python 标准库，用于把实验室 CSV 数据转换成 OCV C 表、ADC 线性换算参数、内阻基准值、`qm_battery_calibration_t` 导入代码和部分滤波参数建议值。完整采集步骤、CSV 示例、执行命令和代码接入方式见下面的“量产标定流程”。

输入文件字段速查：

| 文件 | 字段 | 说明 |
|---|---|---|
| `ocv.csv` | `soc,voltage` | 静置 SOC 点和该点稳定后的电池端电压 |
| `adc.csv` | `raw,voltage` | 固件 ADC 原始值和同一时刻的电池端真实电压 |
| `resistance.csv` | `v_before,v_after,i_before,i_after` | 负载阶跃前后电压和电流 |
| `trace.csv` | `time_s,voltage,charging,charge_full` | 运行过程中的电池端电压、充电状态和充电芯片满电状态，状态字段用 `0/1` 表示 |

命令行参数速查：

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `--divider-ratio` | `1.0` | 电池端电压 / ADC 引脚电压 |
| `--charge-full-min-v` | 使用 `cfg` 默认值 | 接受充电芯片满电信号所需的最低电池端电压 |
| `--soc-points` | `0,5,10,20,30,40,50,60,70,80,90,100` | 输出 OCV 表的 SOC 采样点 |

### 策略配置参数（`qm_config`）

| 配置项 | 默认值 | 说明 |
|---|---:|---|
| `CONFIG_QM_BATTERY_DEFAULT_TEMP_C` | `25` | 无温度传感器时使用的默认电池温度 |
| `CONFIG_QM_BATTERY_TEMP_COMP_ENABLE` | `0` | 默认是否开启 OCV 温度补偿 |
| `CONFIG_QM_BATTERY_SOH_UPDATE_ENABLE` | `1` | 默认是否从内阻估算慢速更新 SOH |
| `CONFIG_QM_BATTERY_CHARGE_SETTLE_S` | `30` | 插电、拔电后等待端电压稳定的时间 |
| `CONFIG_QM_BATTERY_CHARGE_SETTLE_RM_SCALE` | `6` | 插拔电稳定窗口内的观测噪声放大倍数 |
| `CONFIG_QM_BATTERY_RELAX_CONFIRM_S` | `60` | 判定静置并允许 OCV 校正前需要稳定的时间 |
| `CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S` | `8` | 充电芯片满电信号需要连续有效的确认时间 |
| `CONFIG_QM_BATTERY_LEARNED_FULL_MIN_MV` | `4050` | 学习满电电压的下限 |
| `CONFIG_QM_BATTERY_LEARNED_FULL_MAX_MV` | `4200` | 学习满电电压的上限 |
| `CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE` | `80` | 每个确认满电周期更新学习满电电压的比例，80 表示 8% |
| `CONFIG_QM_BATTERY_FULL_GUARD_S` | `1800` | 确认满电后保护 SOC 不被立即下修的时间 |
| `CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT` | `98` | recent full guard 生效时允许的最低 SOC |
| `CONFIG_QM_BATTERY_INITIAL_SOC_MAX_AGE_H` | `24` | 保存 SOC 超过该时长后，首次非充电采样会用 OCV 复核 |
| `CONFIG_QM_BATTERY_STANDBY_DRAIN_PCT_PER_DAY` | `2` | 根据离线时长扣减保存 SOC 的默认百分比 |

### 实验室标定参数（`qm_battery_calibration_t`）

实验室标定结果不要固化到 `qm_config`，应通过 `qm_battery_calibration_t`
和 `qm_battery_apply_calibration()` 导入。

| 字段 | 说明 |
|---|---|
| `ocv_table` / `ocv_table_count` | OCV-SOC 表 |
| `adc_scale_v_per_count` | ADC 原始值到 ADC 引脚电压的线性比例 |
| `adc_offset_v` | ADC 引脚电压偏移 |
| `adc_divider_ratio` | ADC 引脚电压换算到电池端电压的分压比例 |
| `r_nominal_ohm` | 新电池或当前批次的等效内阻基准 |
| `full_anchor_v` | 满电锚点，电压高于该值且处于充电状态时向 100% 收敛 |
| `empty_anchor_v` | 低电锚点，电压低于该值且未充电时向 0% 收敛 |
| `charge_full_min_v` | 接受满电信号所需的最低合理电压，不是最大容量电压 |

`learned_full_v` 属于运行时持久化值，不归入实验室标定项。

### 其他策略字段（`qm_battery_config_t` 默认值）

`k1_soc_per_vps`、`rm_dynamic_scale`、`rm_slope_gain` 没有独立 `qm_config`
宏，默认值由 `qm_battery_default_config()` 提供；产品如需调优，可在
`qm_battery_init()` 前直接覆盖。

| 字段 | 默认值 | 说明 |
|---|---:|---|
| `k1_soc_per_vps` | `-4` | 电压斜率对 SOC 预测项的影响系数 |
| `rm_dynamic_scale` | `4` | 动态电压变化时观测噪声放大系数 |
| `rm_slope_gain` | `0.040` | 电压斜率对观测噪声的附加增益 |

## 量产标定流程

下面流程适用于“1S 锂电池、只有电压输入、没有电流检测”的量产场景。标定目标不是让动态负载下的 SOC/SOH 变成绝对物理真值，而是让同一批产品在真实使用场景下输出稳定、一致、无明显跳变的估算结果。

整体顺序：

- 实验室测电芯模型：OCV-SOC 表、满电/低电锚点、内阻基准、动态电压日志
- 产线测硬件误差：ADC 原始值到电池端电压的换算参数
- 用 `tools/qm_battery_calibrate.py` 生成标定导入代码和策略建议
- 写入产品代码，先通过 `qm_battery_apply_calibration()` 导入实验室标定值，再把策略值放进 `qm_config`
- 跑整机首件验证，微调滤波参数
- 固化批次记录和抽检标准

### 1. 准备设备和约定

推荐设备：

- 电池测试仪或可控充放电设备
- 电子负载
- 高精度万用表
- 可调电源，用于 ADC 标定
- 恒温箱或温度相对稳定的测试环境
- 串口日志工具，用于记录固件采样值
- 测试电池若干，至少区分新电池、典型批次、老化样品

统一约定：

- 所有电压单位使用 `V`
- 电流单位使用 `A`
- 时间单位使用 `s`
- 温度未知时固定为 `25C`，并关闭 `temp_comp_enable`
- `adc.csv` 里的 `voltage` 必须填写电池端真实电压，不是 ADC 引脚电压
- ADC 分压比定义为 `电池端电压 / ADC 引脚电压`
- `ocv.csv` 只能使用非充电、充分静置后的点；充电 CV 阶段的端电压不能混入 OCV 表
- `adc.csv` 不需要区分充电和非充电，它只用于拟合 ADC raw 到电池端真实电压
- `trace.csv` 应保留充电状态，插电、拔电、非充电运行的数据不要混在一起解释

### 2. 测量 OCV-SOC 表

目标是得到静置电压和真实 SOC 的对应关系，用于替换默认 OCV 表。

操作步骤：

- 将电池充满到产品允许的满电状态
- 充满后静置，直到电压变化很小，例如 30 到 60 分钟内变化小于几 mV
- 记录 `100%` SOC 的静置电压
- 使用小电流放电，建议 C/20 或更慢
- 放到目标 SOC 点后停止放电并静置
- 等电压稳定后记录该点电压
- 按 `100/90/80/70/60/50/40/30/20/10/5/0` 或项目需要的点重复
- 建议至少采 7 点，更推荐 12 点左右；两端区域 `0%~20%` 和 `80%~100%` 建议采得更密
- 生成 `ocv.csv`

`ocv.csv` 示例：

```csv
soc,voltage
100,4.185
90,4.075
80,3.985
70,3.900
60,3.830
50,3.760
40,3.705
30,3.660
20,3.620
10,3.510
5,3.405
0,3.300
```

输出参数：

- OCV 表 `s_product_ocv_table[]`
- 满电锚点电压 `full_anchor_v`
- 低电锚点电压 `empty_anchor_v`

判定建议：

- 推荐点位为 `0/5/10/20/30/40/50/60/70/80/90/100`
- 最低可用点位为 `0/10/20/40/60/80/100`
- `100%` 电压会作为 `full_anchor_v` 的候选值
- `0%` 电压会作为 `empty_anchor_v` 的候选值
- 低电点不要取保护板彻底断电后的值，要取产品定义的可用容量终点
- OCV 曲线必须单调；如果局部点反常，应复测该 SOC 点

### 3. 标定 ADC 换算

目标是得到：

- `adc_scale_v_per_count`
- `adc_offset_v`
- `adc_divider_ratio`

电压换算公式：

```text
battery_voltage = (adc_raw * adc_scale_v_per_count + adc_offset_v) *
                  adc_divider_ratio
```

操作步骤：

- 确认硬件分压比例，例如 `VBAT -> Rtop -> ADC -> Rbottom -> GND`
- 计算初始分压比：`divider_ratio = (Rtop + Rbottom) / Rbottom`
- 用可调电源给电池采样端输入多个电压点
- 建议至少覆盖低/中/高 3 点，例如 3.30V、3.75V、4.20V
- 实验室或量产首件建议采 5 点，例如 3.30V、3.50V、3.70V、3.90V、4.20V
- 每个点同时记录固件读到的 ADC raw 和万用表测到的电池端真实电压
- 生成 `adc.csv`

`adc.csv` 示例：

```csv
raw,voltage
3000,3.300
3400,3.740
3600,3.960
3820,4.200
```

执行单项标定：

```bash
python3 components/qm_battery/tools/qm_battery_calibrate.py \
    --adc adc.csv \
    --divider-ratio 2.0
```

判定建议：

- 优先关注输出报告中的 `adc_max_abs_error` 和 `adc_rms_error`
- 如果满电附近误差大，增加高电压点重新拟合
- 量产每板校准时至少保留 2 到 3 个电压点
- 不建议只采 1 点或 2 点；2 点虽然能拟合直线，但无法判断误差和异常点
- 如果 ADC HAL 已经返回电池端毫伏值，可设置 `adc_scale_v_per_count = 0.001f`，`adc_divider_ratio = 1.0f`

### 4. 测量新电池内阻基准

目标是得到新电池或当前批次的 `r_nominal_ohm`。

操作步骤：

- 将电池 SOC 调到 50% 附近
- 固定温度 25C
- 静置到电压稳定
- 电子负载从 0A 阶跃到目标负载，例如 0.5A
- 记录阶跃前电压 `v_before`
- 记录阶跃后约 1s 附近的电压 `v_after`
- 记录阶跃前后电流
- 至少采 3 组有效阶跃，推荐 5 到 10 组
- 如果要建立批次基准，建议每个电芯批次抽 3 到 5 颗电池，每颗测 3 次以上
- 重复多次，生成 `resistance.csv`

`resistance.csv` 示例：

```csv
v_before,v_after,i_before,i_after
3.820,3.790,0.00,0.50
3.818,3.786,0.00,0.50
3.815,3.784,0.00,0.50
```

脚本按以下公式计算均值：

```text
R = abs(v_before - v_after) / abs(i_after - i_before)
```

判定建议：

- 新电池和老化电池要分开建档
- 50% SOC 附近为必测点；如果时间允许，建议补充 20%~30% 和 80% 附近数据
- 如果批次内阻差异大，`r_nominal_ohm` 应按电芯批次管理
- 阶跃电流过小会放大测量误差，建议选择整机可承受且稳定的负载电流

### 5. 记录动态电压日志

目标是给 `k1_soc_per_vps`、`rm_dynamic_scale`、`rm_slope_gain` 提供初始建议。

操作步骤：

- 使用真实整机或接近真实整机的负载
- 从较高电量开始运行典型工况
- 固件每 1s 或固定周期输出电池端电压
- 覆盖空闲、启动、大负载、负载切换、低电等场景
- 如果按 1s 采样，最低建议约 300 点，推荐 1800 点以上
- 更好的日志应覆盖一次完整典型工作周期或完整放电片段
- 生成 `trace.csv`

`trace.csv` 示例：

```csv
time_s,voltage,charging,charge_full
0,4.100,0,0
1,4.096,0,0
2,4.093,0,0
3,4.091,0,0
4,4.140,1,0
5,4.136,1,0
6,4.118,1,1
7,4.116,1,1
```

注意：

- 动态日志只能给初始建议值，最终要靠整机验证微调
- `trace.csv` 的价值主要取决于工况覆盖，不只是点数
- 只有几十个点时脚本也能运行，但滤波参数建议值参考意义较弱
- 时间戳必须递增
- 电压应为换算后的电池端电压
- 应包含插电、拔电场景，并记录 `charging` 状态，用于验证 `charge_settle_s` 和 `charge_settle_rm_scale`
- 如果充电芯片有满电信号，应记录 `charge_full`，用于验证满电去抖、SOC 锚定到 100% 和 `learned_full_v`
- 充电点和非充电点的处理逻辑不同；充电时端电压包含充电器抬升和极化电压，不能按静置 OCV 点直接校正 SOC
- 典型工况比理想恒流工况更有价值

### 6. 生成标定建议

四个输入文件都准备好后，执行：

```bash
python3 components/qm_battery/tools/qm_battery_calibrate.py \
    --ocv ocv.csv \
    --adc adc.csv \
    --resistance resistance.csv \
    --trace trace.csv \
    --divider-ratio 2.0 \
    --charge-full-min-v 4.05
```

脚本输出包含：

- `s_product_ocv_table[]`
- `qm_battery_config_t` 覆盖项
- ADC 误差报告
- 内阻均值和范围
- 动态电压斜率统计

如果只做某一项标定，可以只传对应输入文件：

```bash
python3 components/qm_battery/tools/qm_battery_calibrate.py --ocv ocv.csv
python3 components/qm_battery/tools/qm_battery_calibrate.py --adc adc.csv --divider-ratio 2.0
python3 components/qm_battery/tools/qm_battery_calibrate.py --resistance resistance.csv
python3 components/qm_battery/tools/qm_battery_calibrate.py --trace trace.csv
```

### 7. 写入产品代码

将脚本输出的 OCV 表和标定结果写入产品初始化代码：

```c
static const qm_battery_ocv_point_t s_product_ocv_table[] = {
    {0.0f, 3.3000f, 0.0f},
    {5.0f, 3.4050f, 0.0f},
    {10.0f, 3.5100f, 0.0f},
    /* ... */
    {100.0f, 4.1850f, 0.0f},
};

qm_battery_config_t cfg;
qm_battery_calibration_t calibration;

qm_battery_default_config(&cfg);

calibration.ocv_table = s_product_ocv_table;
calibration.ocv_table_count = sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);
calibration.adc_scale_v_per_count = 0.000550000f;
calibration.adc_offset_v = 0.000000f;
calibration.adc_divider_ratio = 2.000000f;
calibration.r_nominal_ohm = 0.062000f;
calibration.full_anchor_v = 4.1850f;
calibration.empty_anchor_v = 3.3000f;
calibration.charge_full_min_v = 4.050f;
if (qm_battery_apply_calibration(&cfg, &calibration) != QM_EOK) {
    return;
}

if (app_has_saved_soc()) {
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = app_get_saved_soc();
    if (app_has_saved_soc_age()) {
        cfg.initial_soc_age_s = app_get_saved_soc_age_s();
    } else {
        cfg.initial_soc_age_s = cfg.initial_soc_max_age_s + 1.0f;
    }
}
if (app_has_learned_full_voltage()) {
    cfg.learned_full_valid = QM_TRUE;
    cfg.learned_full_v = app_get_learned_full_voltage();
}
qm_battery_init(&g_battery, &cfg);
```

接入要求：

- 先调用 `qm_battery_default_config()`，再用 `qm_battery_apply_calibration()` 导入标定项
- 没有温度传感器时，保持 `temp_comp_enable = QM_FALSE`
- OCV 表数组必须在电池组件使用期间保持有效
- 如果上层有掉电前 SOC 记录，启动时设置 `initial_soc_valid = QM_TRUE`、`initial_soc_percent` 和 `initial_soc_age_s`
- 组件会按 `standby_drain_percent_per_day` 先扣减离线耗电，保存值超过 `initial_soc_max_age_s` 后，首次非充电采样会用 OCV 作为上限复核
- 如果无法获取保存 SOC 的 age，不要填 `0`；应设置为 `initial_soc_max_age_s + 1` 将该保存值标记为过期，或直接设置 `initial_soc_valid = QM_FALSE`
- 如果设备开机时已经处于充电状态，组件不会用首个充电端电压按 OCV 表初始化或复核 SOC
- 调用 `qm_battery_update_voltage()` 或 `qm_battery_update_adc()` 时必须传入 `charge_full`；没有充电芯片满电信号的产品传 `QM_FALSE`
- `charge_full` 不是 `charging` 的替代字段；充满后仍应保持 `charging = QM_TRUE`，同时设置 `charge_full = QM_TRUE`
- 满电信号确认后，SOC 会直接锚到 100%，并在每个确认满电周期学习一次当前满电电压；上层应在 `result.learned_full_valid` 为真时持久化 `result.learned_full_v`
- 拔掉充电器后，recent full guard 会在 `full_guard_s` 时间内防止 OCV/电压回落把 SOC 立即拉到 `full_guard_min_percent` 以下
- 老化电池满电电压变低时，不需要立刻重标 OCV 表；先依赖充电芯片满电信号让 `learned_full_v` 多个满电周期慢速跟随，再评估低电和中段 SOC 是否仍满足要求
- 每次更换电芯型号、保护阈值、采样电阻或 ADC 参考，都要重新标定

### 8. 首件验证和调参

至少验证以下场景：

| 项目 | 操作 | 期望 |
|---|---|---|
| 满电 | 充满并保持充电状态 | SOC 能稳定靠近 100% |
| 低电 | 放到产品低电阈值 | SOC 能稳定靠近 0% |
| 静置 | 50% 附近静置 1 小时 | SOC 漂移小 |
| 负载阶跃 | 空闲和大负载切换 | SOC 不应明显跳变 |
| 重启恢复 | 不同电压点开机 | 初始 SOC 接近 OCV 表 |
| ADC 校准 | 对比万用表 | 电压误差满足产品要求 |

调参方向：

- SOC 抖动大：增大 `rm_base_v2` 或 `rm_dynamic_scale`，减小 `q_soc`
- SOC 反应太慢：增大 `q_soc`，适当减小 `rm_dynamic_scale`
- 负载切换时跳变：增大 `rm_dynamic_scale` 或 `rm_slope_gain`
- 插电、拔电后 SOC 跟着端电压跳变：增大 `charge_settle_s` 或 `charge_settle_rm_scale`
- 插电、拔电后 SOC 长时间不收敛：减小 `charge_settle_s` 或 `charge_settle_rm_scale`
- 静置收敛太慢：检查 `relax_delta_v`、`relax_dvdt_vps`、`relax_confirm_s`
- SOH 变化太快：减小 `q_soh`，或关闭 `soh_update_enable`
- 有充电芯片满电信号但 SOC 不到 100%：检查 `charge_full` 输入、`charge_full_confirm_s`、`charge_full_min_v` 和 `charging` 输入
- 拔掉充电器后 SOC 立刻从 100% 掉太多：增大 `full_guard_s`，或适当提高 `full_guard_min_percent`；同时复核 ADC 标定和 OCV 表 100% 点
- 老化电池满电电压变低：优先使用 `charge_full`，并持久化 `learned_full_v`；如果跟随太慢可适当增大 `learned_full_blend`，固定 `full_anchor_v` 只作为没有满电信号时的兜底
- 满电或低电无法锁定：优先复核 `full_anchor_v`、`empty_anchor_v`、`charge_full` 和充电状态输入
- 整体 SOC 偏差固定：优先复核 OCV 表和 ADC 换算

### 9. 批量生产抽检

建议抽检项：

- 每批首件做完整 OCV 和内阻测试
- 每批抽 3 到 5 台做 ADC 校准复核
- 每周抽 1 批做负载阶跃验证
- 每次变更电芯供应商、分压电阻、ADC 参考、低电保护阈值后重新跑首件验证

建议放行条件：

- OCV 曲线与基准曲线偏差在容差内
- ADC 误差满足整机精度要求
- 内阻离散度未超出批次阈值
- 估算结果无跳变、无发散
- 静置 SOC 漂移满足产品体验要求

### 10. 记录表模板

建议量产记录统一为以下字段：

```text
SN, 电芯型号, 电芯批次, PCB批次, 日期, 测试员, 环境温度,
Vfull, Vempty, R0, adc_scale, adc_offset, adc_divider_ratio,
ocv_version, k1, k2, q_soc, q_soh, q_r,
rm_base_v2, rm_dynamic_scale, rm_slope_gain,
charge_settle_s, charge_settle_rm_scale,
adc_max_abs_error, adc_rms_error, 结果, 备注
```

### 11. 现场使用规则

- 未知温度时，直接使用默认温度
- 没有电流采样时，不要强行做“绝对精确”的 SOH 解释
- 产线校准优先保证一致性和稳定性，再追求绝对值
- 任何阈值修改后，都要重新跑首件验证
- 先保证 ADC 电压准确，再保证 OCV 表可靠，最后再调滤波参数
- 插电、拔电后组件默认进入 30s 稳定窗口，窗口内不使用 OCV/满电/低电锚点强拉 SOC，并降低瞬时电压校正权重
- 设备可能带充电器开机时，上层应优先保存并恢复上次 SOC，同时保存记录时间；没有历史 SOC 时，首次充电端电压只能作为动态输入，不能当作静置 OCV 真值
- 长时间关机或深睡后，保存 SOC 可能已经过期；应按产品待机电流设置 `standby_drain_percent_per_day`，并让首次非充电电压对过期保存值做向下复核
- 无法知道离线时长时，只在保存值仍有产品意义时恢复；如果设备可能长时间闲置后插电开机，且保存 SOC 可能严重偏高，优先不恢复保存值或在上层先做保守封顶

## 文件

```text
components/qm_battery/
├── include/
│   ├── qm_battery.h
│   ├── qm_battery_filter.h
│   └── qm_battery_ocv.h
├── src/
│   ├── qm_battery.c
│   ├── qm_battery_filter.c
│   └── qm_battery_ocv.c
├── tools/
│   └── qm_battery_calibrate.py
├── qm_kconfig
└── README.md
```
