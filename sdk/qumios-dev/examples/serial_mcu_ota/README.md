# 小匠串口 MCU OTA 主控示例

这个示例只演示 Qumi 模组端作为主控发送 MCU OTA 固件。主控侧使用 `components/serial_mcu_ota/serial_mcu_ota.*`，并通过 `xjiang_serial` 的 `g_qm_serial_cmd_table` 分发 `OTA_ACK` / `OTA_NOTIFY`。

## 文件结构

```text
qumi/examples/serial_mcu_ota/
├── README.md
└── serial_mcu_ota_example.c
```

## 示例能力

- Qumi 主控侧 `read_chunk` / `notify` 接法
- 从入参传入固件整包校验类型和校验值
- `OTA_BEGIN` / `OTA_DATA` / `OTA_END` 完整流程
- 先发送 `OTA_ENTER`，等待 MCU `READY` 通知后再发送 `OTA_BEGIN`
- MCU `OTA_NOTIFY` 异步通知上报
- 主控端进度、重试、完成、失败事件通知

## 示例文件

- `serial_mcu_ota_example.c`：真实 Qumi 主控侧接入示例，依赖 `serial_mcu_ota.*` 和 Qumi 串口命令表，不依赖 MCU 接收端组件。

## 运行逻辑

- 调用方提前计算 CRC32/MD5，并通过 `serial_mcu_ota_config_t::check_value` 传给组件。
- `qm_serial_mcu_ota_start()` 通过现有 `serial_generate_send()` 发送 OTA 命令。
- 模组端收到 MCU 的 `OTA_ACK` 或 `OTA_NOTIFY` 后，由 `g_qm_serial_cmd_table` 自动调用 `serial_mcu_ota_parse()`。
- 模组端定时任务中周期调用 `serial_mcu_ota_process()`，用于处理 ACK 超时后的自动重发。
- 需要取消时调用 `serial_mcu_ota_abort()`。

## 关键流程

1. 模组侧获取固件总长度。
2. 初始化 Qumi 串口组件。
3. 调用方提前计算固件 CRC32 或 MD5。
4. 填充 `serial_mcu_ota_config_t`，注册 `read_chunk` / `notify` / `check_value`。
5. 调用 `qm_serial_mcu_ota_start()` 开始传输。
6. 串口层收到 MCU `OTA_ACK` 或 `OTA_NOTIFY` 后，由命令表自动分发。
7. 定时调用 `serial_mcu_ota_process()`。
8. OTA 状态进入 `SERIAL_MCU_OTA_STATE_DONE` 后，认为传输完成。

## 接入真实工程

这个目录下的示例不会自动参与现有 app 构建。

接入时建议：

1. Qumi 主控侧加入 `components/serial_mcu_ota` 目录。
2. 开启 `SERIAL_MCU_OTA_SUPPORT`，会选择 Qumi `QM_UART_SUPPORT` 和 `QM_TIMER`。
3. 在模组侧实现 `read_chunk`，从 OTA 文件或固件缓冲区按 `offset` 读取数据。
4. 在模组侧提前计算校验值，并传入 `check_type` 和 `check_value`。
5. 串口层负责解析完整帧，并通过 `g_qm_serial_cmd_table` 将 `0x3A` / `0x3B` 分发给 `serial_mcu_ota_parse()`。
6. 主控侧不需要包含 MCU receiver 头文件路径，也不需要编译 MCU receiver 源文件。

真实 Qumi 主控侧最小调用关系：

```text
serial_mcu_ota_example_start(firmware, firmware_size, check_type, check_value)
串口收到 OTA_ACK 或 OTA_NOTIFY -> g_qm_serial_cmd_table -> serial_mcu_ota_parse()
定时任务 -> serial_mcu_ota_example_process()
需要取消 -> serial_mcu_ota_example_abort()
```

## 单机语法检查

主控侧示例：

```sh
clang -fsyntax-only -std=c99 \
  -Iapps/opencpu/qm_config \
  -Iqumi \
  -Iqumi/include \
  -Iqumi/include/qmos \
  -Iqumi/include/common \
  -Iqumi/include/hal/soc \
  -Iqumi/utility/list \
  -Iqumi/utility/tlv \
  -Iqumi/utility/timer \
  -Iqumi/components/xjiang_serial \
  -Iqumi/components/qm_time/include \
  -Iqumi/components/serial_mcu_ota \
  qumi/examples/serial_mcu_ota/serial_mcu_ota_example.c \
  qumi/components/serial_mcu_ota/serial_mcu_ota.c
```
