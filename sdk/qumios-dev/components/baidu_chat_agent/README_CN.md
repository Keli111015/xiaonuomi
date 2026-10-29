# baidu_chat_agent

`baidu_chat_agent` 是一个面向百度大模型实时互动接入的精简 QUMI 组件方案。当前版本聚焦在百度 RTC 文档里的 WebSocket 接入主链路、文本/音频收发和关键消息前缀分发，方便上层尽快把链路跑通。

## 架构

音频发送的零超时现表示立即尝试入队，不再使用默认网络超时。准入繁忙或队列满
返回 `-QM_EBUSY` / `-QM_EFULL`；成功表示 WebSocket 已复制输入数据，由现有
任务发送。采集回调不得重试。停止、销毁须与外部发送调用串行；其他控制命令
的默认超时语义不变。

```text
业务应用或 qm_agent transport
    | 配置、文本/音频、事件回调
    v
baidu_chat_agent
    | URI 校验、协议前缀、授权激活
    v
qm_websocket_client
    v
QUMI TCP/TLS transport
    v
百度实时服务
```

组件负责实时协议和会话生命周期；凭据、音频采集/播放，以及 HTTP 侧的实例或 token 准备流程仍由业务层负责。

## 依赖边界

`baidu_chat_agent` 的接口和实现只依赖 QUMI 组件能力：

- QUMI 基础 OS 能力
- QUMI 内存能力
- QUMI 日志能力
- QUMI 网络传输能力

组件不直接暴露或依赖平台侧 `socket`、`RTOS`、`TLS`、`ESP-IDF` 或其它厂商 SDK 细节。

## 文件结构

- `include/baidu_chat_agent.h`: 对外 API、配置、事件和消息类型定义。
- `include/baidu_chat_agent_log.h`: 组件内部分类日志宏。
- `src/baidu_chat_agent.c`: WebSocket 会话管理、发送接口和基础消息解析实现。
- `qm_kconfig`: 组件开关和默认 WebSocket 参数。

## 日志配置

`BAIDU_CHAT_AGENT_LOG_CONFIG` 是日志分类默认值的 Kconfig 总开关。关闭时默认只输出错误和生命周期日志；打开后可分别选择 `BAIDU_CHAT_AGENT_LOG_ERROR`、`BAIDU_CHAT_AGENT_LOG_LIFECYCLE`、`BAIDU_CHAT_AGENT_LOG_CONTROL`、`BAIDU_CHAT_AGENT_LOG_UPLINK`、`BAIDU_CHAT_AGENT_LOG_DOWNLINK`、`BAIDU_CHAT_AGENT_LOG_PAYLOAD` 和 `BAIDU_CHAT_AGENT_LOG_STATS`。

运行时日志掩码是组件全局配置，对所有会话生效：

```c
baidu_chat_agent_set_log_flags(BAIDU_CHAT_AGENT_LOG_ERROR |
                               BAIDU_CHAT_AGENT_LOG_LIFECYCLE |
                               BAIDU_CHAT_AGENT_LOG_DOWNLINK);
```

调用 `baidu_chat_agent_set_log_flags(BAIDU_CHAT_AGENT_LOG_NONE)` 可关闭全部组件日志，不会改变 `qm_websocket_client` 或 QUMI 全局日志等级。

## 当前能力

- 创建、启动、停止、销毁 `baidu_chat_agent` 会话。
- 启动时按文档字段自动拼接 WebSocket URI，支持两种鉴权模式：
  - `a + id + t + ac + [ptime] + [plen]`
  - `a + ak + sk + ac + [ptime] + [plen] + [cfg]`
- 组件内部固定使用文档连接地址：`wss://rtc-aiotgw.exp.bcelive.com/v1/realtime`
- 自动对查询字段做 URL 编码。
- 对 `auth_mode / ac / ptime / plen / cfg` 做结构化文档一致性校验：
  - 两种鉴权字段互斥，不允许混传
  - 实例 `id+t` 模式下不允许传 `cfg`
  - `ac` 仅允许 `raw/raw16k/pcma/pcmu/g722/opus`
  - `ptime` 仅允许 `20/40/60`
  - 非 `opus` 模式下不允许传 `ptime` 和 `plen`
  - `cfg` 必须是合法 JSON 对象
  - 若 `cfg.audiocodec` 存在，则必须与 `ac` 一致
  - 若 `cfg.language` 存在，则仅允许 `zh/en`
  - 若 `cfg.samplerate` 存在，则仅允许 `8000/16000`
  - 若 `cfg.channels` 存在，则仅允许 `1/2`
  - 若 `cfg.dfda` 存在，则必须是布尔值
  - 若 `cfg.aopt` 存在，则仅允许 `20/40/60`
- 发送二进制音频。
- 发送 `"[T]:"` 文本、`"[TTS]:"` 文本和 `"[E]:"` 事件。
- 发送 `"[B]:"` 打断消息。
- 通过 helper API 直接发送常用协议命令，避免上层手拼字符串：
  - `baidu_chat_agent_send_asr_start_longtext_rec()`
  - `baidu_chat_agent_send_asr_stop_longtext_rec()`
  - `baidu_chat_agent_set_asr_realtime_enabled()`
  - `baidu_chat_agent_send_remote_player_control()`
- 自动处理文档里的授权激活链路：
  - 接收 `"[E]:[LIC]:[MUST]:XXXX"`
  - 自动发送 `"[E]:[LIC]:[ACTIVE]:{\"devId\":\"...\",\"uId\":\"...\",\"licKey\":\"...\"}"`
  - 识别 `"[E]:[LIC]:[RES]:[PASS]:"` 与 `"[E]:[LIC]:[RES]:[FAILED]:"`
  - 通过专用 `BAIDU_CHAT_AGENT_EVENT_LICENSE` 事件上报 `MUST / ACTIVE_SENT / PASS / FAILED`
- 复用现有运行时身份字段完成授权激活：
  - `device_id -> devId`
  - `user_id -> uId`
  - `license_key -> licKey`
- OpenCPU example 的运行时身份策略更偏保守默认值：
  - 当配置里的 `device_id` 为空时，优先使用 STA MAC 作为 `device_id`
  - 当配置里的 `user_id` 为空时，默认复用解析后的 `device_id`
  - `license_key` 默认保持为空，需要由接入方填写实际购买的 licKey
- 基于百度实时互动消息前缀分发下行文本：
  - `"[Q]:"`
  - `"[Q]:[M]:"`
  - `"[Q]:[C]:"`
  - `"[Q]:[M]:[C]:"`
  - `"[A]:"`
  - `"[A]:[M]:"`
  - `"[A]:[H]:"`
  - `"[B]:"`
  - `"[F]:"`
  - `"[E]:"`
  - `"[SET]:"`
- 对已知 `"[E]:"` / `"[SET]:"` 消息增加结构化 `semantic` 语义分类，当前覆盖：
  - `TTS_BEGIN_SPEAKING / TTS_END_SPEAKING`
  - `VOICE_COMING / VOICE_DISAPPEAR`
  - `MEDIA_READY`
  - `REMOTE_PLAYER_*`
  - `ASR_*`
  - `MEDIA_GENERATE_MODE(FALSE)`
- 透传下行二进制音频帧。
- 对关键流程和关键消息打日志，敏感信息按脱敏方式打印，不直接输出 token、秘钥、完整地址或完整标识。
- `qm_agent` 的百度 transport 适配层已经实际消费部分结构化语义：
  - `TTS_BEGIN_SPEAKING -> speaking`
  - `TTS_END_SPEAKING -> silent`
  - `VOICE_COMING -> listening`
  - `VOICE_DISAPPEAR -> silent`
  - `MEDIA_READY -> thinking`
- 接入 `qm_agent + GX8006` 时，建议不要机械复用同一个 `frame_ms` 作为上下行节拍：
  - 上行 `input_audio.frame_ms` 决定 Baidu `ptime`
  - 下行 `output_audio.frame_ms` 更适合作为本地播放节拍配置，通常应比上行更保守

## 对外接口

当前只保留最小必要接口：

- `baidu_chat_agent_init`
- `baidu_chat_agent_start`
- `baidu_chat_agent_stop`
- `baidu_chat_agent_destroy`
- `baidu_chat_agent_send_audio`
- `baidu_chat_agent_send_text`
- `baidu_chat_agent_send_text_to_tts`
- `baidu_chat_agent_send_event`
- `baidu_chat_agent_send_break`

## 当前未覆盖

- 文档中的 HTTP 侧“创建实例 / 获取 token / 上下文准备”等前置流程。
- `qm_agent` 公共层尚未为这些百度 helper 命令提供独立通用 API，目前 helper 主要用于直接接入 `baidu_chat_agent` 的场景。
- 文档层面对音频分帧节奏的强约束控制，目前仅保留字段透传与基础校验。

## 直接接口使用说明

仓库里的 OpenCPU example 当前是通过 `qm_agent` 间接接入 `baidu_chat_agent`，所以不会直接持有 `baidu_chat_agent_handle_t` 并调用这些 helper。  
如果是直接使用 `baidu_chat_agent` 组件，推荐这样使用：

```c
baidu_chat_agent_set_asr_realtime_enabled(handle, QM_TRUE, 0U);
baidu_chat_agent_send_remote_player_control(handle,
                                            BAIDU_CHAT_AGENT_REMOTE_PLAYER_PAUSE,
                                            0U);
```
