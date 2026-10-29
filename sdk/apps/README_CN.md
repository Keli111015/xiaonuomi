# apps 说明

应用入口在 `main/`。底层能力来自 `qumios-dev`（qm_iot / qm_agent / qm_audio / VB701N 驱动等），本目录只做板级装配和业务编排。

## 目录

```
main/
├── main.c              # 启动顺序：外设 → 语音 → 云 → 配网 → agent
├── qm_config/          # 产品与功能配置（pid、栈大小、VOICE_BACKEND 等）
├── peripheral/         # 板级外设
│   ├── bsp_uart.*      # 语音芯片串口、vb701n_bind
│   └── bsp_io.*        # 流控脚、长按复位等 GPIO
├── voice/              # 语音会话（qm_audio 创建、唤醒回调；换芯片主要换驱动句柄）
├── cloud/
│   ├── ljyd_cloud.*    # IoT 初始化、动态注册、连云事件
│   ├── ljyd_method.*   # device/up 上报、device/down 分发
│   └── ljyd_provision.*# BLE 配网 / 已保存 WiFi 上电联网
└── agent/
    ├── agent_app.*     # 创建 qm_agent，云连上 + 唤醒词后才 start
    └── private/        # 二开一般不用改：PCM↔Opus、pcm_driver 包装层
```



## 软件架构（简要）

```
                    ┌─────────────┐
                    │   main.c    │
                    └──────┬──────┘
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
     peripheral         voice           cloud
      uart/io         qm_audio      ljyd_cloud
           │               │        ljyd_provision
           │               │               │
           │               ▼               ▼
           │          唤醒词事件      CLOUD_CONN
           │               │               │
           └───────────────┴───────┬───────┘
                                   ▼
                              agent_app
                           (qm_agent create
                            后，双条件 start)
                                   │
                    ┌──────────────┴──────────────┐
                    ▼                             ▼
              private/pcm_drv               Agora 传输
              (芯片 PCM 时编解码)            (qm_agent)
                    ▼
              VB701N 驱动 (qumios-dev)
```

要点：

1. **初始化顺序**：`bsp_uart` → `bsp_io` → `voice` → `ljyd_cloud` → `ljyd_provision` → `agent_app`。`agent` 创建需要 `iot_handle`，且应在 TLS 连云之前完成（避免内部堆碎片导致音频任务栈分配失败）。
2. **连云与配网分开**：配网负责 WiFi；拿到 IP 后通知 cloud 做 dynreg / MQTT。`CLOUD_CONN` 时注册 device/down，再把连云事件交给 agent。`DISCONN` 只交给 agent。
3. **agent 何时 start**：`qm_agent_create` 在 `agent_app_init` 里完成；`qm_agent_start` 仅在「已 CLOUD_CONN」且「语音芯片上报唤醒词」时调用。断云会 stop 并清状态。
4. **音频路径**：上报给 agent 的格式固定为 Opus 16 kHz / 20 ms。芯片侧若是 PCM，由 `agent/private` 做编解码包装；若芯片本身出 Opus，则直接挂 VB701N 驱动。

## device/up 与 device/down

云端业务报文不走属性 siid/piid，走 method。实现在 `cloud/ljyd_method.c`。`CLOUD_CONN` 时 `ljyd_cloud` 调用 `ljyd_method_on_connected()`，按 method 名注册下行。重连再注册一次，同名 method 是覆盖，不会叠回调。

### 上行

本地主动上报：

```c
ljyd_method_up("chat", "quit", NULL);
```

`params_json` 为 `NULL` 或空串时，params 用 `{"source":"local"}`。报文 id 由 SDK 自增。要带别的字段就传完整 JSON，并自己带上 `"source":"local"`。

### 下行

在 `s_table` 加一行：method、subMethod、处理函数、要不要 reply。

```c
{"chat", "quit", action_chat_quit, LJYD_METHOD_NEED_REPLY},
```

处理约定：

- `submethod` 为 `NULL` 表示这个 method 下任意 subMethod。精确匹配优先。
- 带 `LJYD_METHOD_NEED_REPLY` 时，处理函数的返回值作为 `code`，SDK 回 `xxxReply`（method 名自动加 `Reply`）。
- 名字已经是 `xxxReply` 的应答不要再 reply。
- `params.source` 为 `local` 是自己上行的回环。命令处理里丢掉，仍回 `code` 0。
- 回调里只做解析和分发，不要在里面阻塞等应答。

### 已接入的命令

`chat` / `quit`：`source` 不是 `local` 时调用 `agent_app_leave()`。`qm_agent_stop` 成功才清掉已 start 标记，下次唤醒词可以再 start。云连接不断。`params.speakText` 目前只打日志，不播报。

