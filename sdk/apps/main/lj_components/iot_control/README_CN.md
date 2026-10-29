# 物模型设备控制

这个目录位于 `apps/main/lj_components/iot_control`，放云端属性下发对应的设备控制。
云连接只负责收包。有云不一定挂物模型；挂了物模型就一定要先有云 handle。

当前提供：

- `iot_control.c`：一张 siid / piid 表。`iot_control_init()` 只把查表函数挂到云的属性回调上；
- `ctrl/`：具体控制项。音量的回调写在这张表上；

`apps/main/main.c` 在 `ljyd_cloud_init()` 成功后直接调用 `iot_control_init(ljyd_cloud_get_handle())`。
新增控制项在表里加一行。不用改 `main.c`。
云没有属性回调时只打日志。表里没有匹配项时直接返回，不回错误。

MQTT `method`（例如 `chat/quit`）仍在 `apps/main/cloud/ljyd_method.c`，不走这张表。

属性从云端到设备的路径是：

```text
云端属性下发
        |
        v
ljyd_cloud 收包（只处理 PROPERTY_SET）
        |
        v
iot_control_on_set() 按 siid / piid 查表
        |
        v
属性回调（现在只有音量）
        |
        +-- 写入 KV
        +-- 芯片就绪则写硬件
        +-- qm_iot_report() 上报影子
```

## 音量

对齐 `ljyd_ai_lamp` 的音量，不接静音。

| siid | piid | 含义 |
| --- | --- | --- |
| 2 | 1 | 绝对音量，10..100 |
| 2 | 2 | 1 增大，2 减小，3 最大，4 最小 |

宏在 `ctrl/iot_control_volume.h`：`IOT_CONTROL_VOLUME_SIID`、`IOT_CONTROL_VOLUME_PIID`、
`IOT_CONTROL_VOLUME_PIID_ADJ`。

增减命令到来时才读 KV。键是 `"volume"`，值和 `apps/main/voice/voice.c` 共用，范围 10..100，没有记录时用 80。
芯片寄存器是 0..31，换算是 `音量 * 31 / 100`。

芯片版本事件之后，`voice.c` 自己读同一条 KV 并调用
`QM_AUDIO_CTRL_SET_VOLUME`。所以开机音量由语音模块写到硬件，物模型初始化
不依赖 `qm_audio` 已经创建。

云端改音量时，`volume_commit()` 按这个顺序做：

```text
夹到 10..100
  -> 写入 KV "volume"
  -> voice_get_audio() 非空则写寄存器
  -> 用 siid=2 piid=1 上报当前绝对音量
```

`voice_get_audio()` 为空时跳过写寄存器，KV 和影子上报仍然进行。日志标签是
`volume`，可见 `kv load`、`VALUE set`、`ADJUST cmd`、`hw vol`、`shadow report`。

增减步长是 10。未知的调节命令只打日志，不改音量。影子只上报绝对音量，
不上报调节命令本身。

## 再加一个属性

音量就是现成的样子。新控制项放在 `ctrl/`，在表里加一行。

1. 在 `ctrl/` 新建模块，声明 siid / piid，并提供 `int xxx(qm_spec_property_t *property)`。
   音量对应的是 `iot_control_volume_set_value()` 和 `iot_control_volume_set_adjust()`。
2. 在 `iot_control.c` 的 `s_table` 加一行。
3. 把新的 `.c` 加进 `apps/main/CMakeLists.txt` 的 `COMPONENT_SRCS`。

音量表项现在是：

```c
static const iot_control_entry_t s_table[] = {
    {IOT_CONTROL_VOLUME_SIID, IOT_CONTROL_VOLUME_PIID, iot_control_volume_set_value},
    {IOT_CONTROL_VOLUME_SIID, IOT_CONTROL_VOLUME_PIID_ADJ, iot_control_volume_set_adjust},
};
```

分发函数匹配到第一行就返回，同一对 siid / piid 不要登记两次。回调的返回值目前被丢掉；参数不合法时音量回调返回 `-QM_EINVAL`，调用方看不到这个结果。
