# QM AGENT 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下使用 `qm_agent` 会话组件。

## 文件结构

```text
qumi/examples/qm_agent/
├── README.md
├── README_CN.md
├── agora_cloud_example.c
├── joyinside_cloud_example.c
├── qm_agent_example.c
├── qm_agent_example_audio.h
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## 示例能力

- 创建、启动、停止和销毁 `qm_agent` session。
- 启动 STA WiFi，并在获取 IP 后启动 `qm_agent` session。
- 在默认 `qm_event` loop 注册 `QM_AGENT_EVENT_BASE`，打印连接、状态、ASR、TTS、
  智能体文本、智能体状态和错误事件。音频和视频帧不进入该事件 loop。
- 默认使用本地 custom transport，不需要云端 token 即可跑通 `send_text()` 和
  `request_tts()` 流程；也支持调用 `qm_agent_send_video()` 验证上行视频接口。
- 每个示例都会初始化进程级 `qm_audio` 服务，并为 Agent 会话创建独立的
  `qm_audio_control_t`。目标平台必须提供 `qm_audio_device_*` 后端，示例不会
  注入虚假的板级驱动。
- 可关闭 custom transport，改用 JoyInside 配置项接入真实后端。
- `agora_cloud_example.c` 展示了云鉴权 + 动态注册 + Agora transport 的完整链路。
- `joyinside_cloud_example.c` 展示了云鉴权 + 动态注册 + JoyInside transport 的完整链路。

## 常用配置

```text
CONFIG_QM_AGENT_SUPPORT=y
CONFIG_QM_AGENT_EXAMPLE_CONFIG=y
CONFIG_QM_AGENT_EXAMPLE_USE_CUSTOM_TRANSPORT=y
CONFIG_QM_AGENT_EXAMPLE_USE_WIFI=y
CONFIG_QM_AGENT_EXAMPLE_WIFI_SSID="your_ssid"
CONFIG_QM_AGENT_EXAMPLE_WIFI_PASSWORD="your_password"
CONFIG_QM_AGENT_EXAMPLE_START_TEXT="hello qm_agent"
CONFIG_QM_AGENT_EXAMPLE_TTS_TEXT="qm_agent example is ready"
```

## 运行入口

```c
qm_application_start();
qm_agent_example_start();
qm_agent_example_stop();
qm_agent_example_send_text("hello");
qm_agent_example_request_tts("test tts");
qm_agent_example_send_video(frame_data, frame_len, &video_format);
qm_agent_example_interrupt();
```

## 接入方式

示例位于 `qumi/examples/qm_agent/`。接入时将
`qumi/examples/qm_agent/qm_agent_example.c` 加入工程源码列表，并将
`qumi/examples/qm_agent/qm_config/qm_config.h` 中相关配置合并到项目配置头。
`QM_AGENT_EXAMPLE_CONFIG` 仅在 `QM_AGENT_SUPPORT` 开启时暴露。

云端链路示例也可以单独接入：

- `qumi/examples/qm_agent/agora_cloud_example.c`
- `qumi/examples/qm_agent/joyinside_cloud_example.c`

如果是直接合并示例里的配置头，请同时保留 `QM_AUDIO_SUPPORT` 和平台音频设备后端，
确保共享 `qm_audio` 服务可为 Agent 的采集与播放提供服务。
