# lgeine_agent

英文文档：[README.md](README.md)

音频发送的零超时现表示立即尝试入队，不再使用默认网络超时。协议锁竞争返回
`-QM_EBUSY`，队列满返回 `-QM_EFULL`；接收成功后输入数据已复制，现有
WebSocket 任务负责发送。采集回调不得重试。停止、销毁须与外部发送调用串行；
其他控制命令的默认超时语义不变。

`lgeine_agent` 实现灵机一动语音 WebSocket 通讯协议（v2）的设备端 agent。组件负责
JSON 协议编解码、`hello` 握手、会话 id（`sessionId`）管理与音频帧分发，底层
WebSocket/TCP/TLS 复用 `qm_websocket_client`。

协议完整定义见 [LGEINE_PROTOCOL_V2_CN.md](LGEINE_PROTOCOL_V2_CN.md)。

## 架构

```text
应用层
    | 事件与会话命令
    v
lgeine_agent
    |-- WebSocket 建连头：pid、sn、Authorization
    |-- JSON：hello / listen / speak / abort / welcome / goodbye
    |                                   (上行)
    |-- JSON：helloReply / stt / llm / tts / abort
    |                                   (下行)
    `-- 二进制音频分块
    v
qm_websocket_client
    v
QUMI TCP/TLS transport
```

组件不负责采集或播放音频、保存或刷新 access token、选择文本编码。音频采集/播放、
token 获取与过期的提前刷新由应用层或其他 QUMI 组件承担；组件只负责建立连接、握手、
按 `sessionId` 路由控制消息与音频帧，并以事件回调上抛。

## 会话流程

```text
新建 > 连接 > HELLO_SENT > READY > ACTIVE > 关闭
```

1. 通过事件回调初始化组件。
2. 以服务地址、产品 ID（`pid`）、设备序列号（`sn`）、`access_token` 和上行音频格式启动。
3. WebSocket 建连（携带 `pid`、`sn`、`Authorization: Bearer <token>`）后，组件自动发送 `hello`。
4. 仅当收到 `code=0` 且含 `sessionId`、`audioInfo` 的 `helloReply` 后，上报
   `LGEINE_AGENT_EVENT_READY`，组件进入 `READY`。
5. `READY` 后才可发送音频与会话命令；未就绪时业务发送 API 返回 `-QM_EBUSY`。
6. 在应用任务中停止并销毁组件，不要在事件回调中执行生命周期操作。

### 握手与断线

- 默认 10 秒未收到 `helloReply` 时，上报 `LGEINE_AGENT_ERROR_HELLO_TIMEOUT`，并在
  自动重连开启时请求重连；关闭自动重连时关闭连接。
- 收到 `code != 0` 的 `helloReply` 时，上报 `LGEINE_AGENT_ERROR_REJECTED`（`code`
  字段携带服务端错误码），自动重连开启时请求重连，否则关闭连接。
- WebSocket 断开后 session 失效；组件清空 `sessionId`。重连会自动重新 `hello` 并
  获取新的 `sessionId`，不会重放旧会话音频。

### 下行消息规则

- 除 `helloReply` 外，所有下行 JSON 必须携带与当前会话一致的 `sessionId`；缺失或不
  匹配的消息按过期/无效消息直接丢弃。
- 握手成功后，非法业务消息直接忽略，不上报错误、不关闭连接（遵循协议 3.1 节）。
- 无法识别的 `method` 或非法业务消息直接忽略。
- 收到服务端 `abort` 且 `reason="session_end"` 时，组件在结束当前交互后关闭 WebSocket。

## 最小用法

```c
static void on_event(lgeine_agent_handle_t agent,
                     const lgeine_agent_event_t *event,
                     void *ctx)
{
    (void)ctx;
    if (event->id == LGEINE_AGENT_EVENT_READY) {
        /* 此时可开始监听并上传音频。 */
        (void)lgeine_agent_send_listen(agent,
                                       LGEINE_AGENT_LISTEN_STATE_START,
                                       LGEINE_AGENT_LISTEN_MODE_REALTIME,
                                       NULL, 5000U);
    }
}

lgeine_agent_init_config_t init = {
    .event_cb = on_event,
};
lgeine_agent_handle_t agent = lgeine_agent_init(&init);
lgeine_agent_start_params_t start = {
    .uri = "wss://platform.example/ws",
    .pid = "YOUR_PRODUCT_ID",
    .sn = "DEVICE_UNIQUE_ID",
    .access_token = "<short-lived-access-token>",
    .audio_info = {
        .format = LGEINE_AGENT_AUDIO_FORMAT_OPUS,
        .rate = 16000,
        .bits = 16,
        .channel = 1,
        .frame_duration_ms = 60,
    },
};
int ret = lgeine_agent_start(agent, &start);
```

事件中的字符串、JSON 与音频指针均为借用数据，仅在当前回调期间有效；二进制音频按
块回调，不额外整帧复制。

## 环境配置

当 `start_params.uri` 为 `NULL` 时，组件按 `start_params.env` 构建默认地址：

| 环境 | 默认地址 |
| --- | --- |
| `LGEINE_AGENT_ENV_DEFAULT`（未设置） | `wss://voice.ljyd.cn/agent-voicechat/ljyd/v1`（生产） |
| `LGEINE_AGENT_ENV_DEV` | `wss://voice-dev.ljyd.cn/agent-voicechat/ljyd/v1` |
| `LGEINE_AGENT_ENV_TEST` | `wss://voice-test.ljyd.cn/agent-voicechat/ljyd/v1` |
| `LGEINE_AGENT_ENV_PROD` | `wss://voice.ljyd.cn/agent-voicechat/ljyd/v1` |

环境为按连接（按实例）配置：由每次 `start` 调用携带，不同 agent 可使用不同环境。
显式传入的 `start_params.uri` 始终优先于 `start_params.env`。`env` 未显式设置
（`LGEINE_AGENT_ENV_DEFAULT`）时，内部显式解析为生产环境
`LGEINE_AGENT_ENV_PROD`。

## API 概览

| 类别 | API |
| --- | --- |
| 生命周期 | `lgeine_agent_init` / `lgeine_agent_start` / `lgeine_agent_stop` / `lgeine_agent_destroy` |
| 状态 | `lgeine_agent_is_ready` |
| 上行音频 | `lgeine_agent_send_audio` |
| 会话命令 | `lgeine_agent_send_listen` / `lgeine_agent_send_speak` / `lgeine_agent_send_abort` / `lgeine_agent_send_welcome` / `lgeine_agent_send_goodbye` |

`lgeine_agent_send_audio()` 上传一帧上行音频。当上行 `audio_info.format` 为 PCM 时，
组件校验帧长度必须等于 `rate * bits / 8 * channel * frameDuration / 1000`（协议 4.3
节）；不符返回 `-QM_EINVAL`。Opus 帧长度由编码器决定，不要求固定字节数。

### 事件与错误

事件回调通过 `lgeine_agent_event_t::id` 区分：

| 事件 | 数据 |
| --- | --- |
| `TRANSPORT_CONNECTED` | 无 |
| `READY` | `ready.audio_info`（协商后的下行音频格式） |
| `DISCONNECTED` | 无 |
| `AUDIO` | `audio`（分块二进制音频） |
| `STT` | `stt.text`、`stt.is_final` |
| `LLM` | `llm.emotion` |
| `TTS` | `tts.state`、`tts.type`、`tts.text` |
| `ABORT` | `abort.reason`（服务端原因） |
| `ERROR` | `error.type`、`error.code`、`error.message` |

## 配置

通过 `CONFIG_LGEINE_AGENT_SUPPORT` 开启组件。主要配置包括 hello 超时、JSON 消息
上限、WebSocket 接收缓冲、网络超时、任务栈/优先级、发送队列和分类日志。

组件依赖 QMOS mutex/timer、`qm_websocket_client`、`qm_string` 以及仓库内 cJSON。
平台构建需要编译 `components/cjson/cJSON.c`。
