# lgeine_agent

Chinese documentation: [README_CN.md](README_CN.md)

`lgeine_agent` implements the device side of the lgeine voice WebSocket protocol
(v2). It owns JSON encoding/decoding, the `hello` handshake, `sessionId`
management, and audio frame routing while reusing `qm_websocket_client` for
WebSocket/TCP/TLS.

The full protocol is defined in
[LGEINE_PROTOCOL_V2_CN.md](LGEINE_PROTOCOL_V2_CN.md).

## Architecture

```text
application
    | events and session commands
    v
lgeine_agent
    |-- WebSocket handshake headers: pid, sn, Authorization
    |-- JSON uplink: hello / listen / speak / abort / welcome / goodbye
    |-- JSON downlink: helloReply / stt / llm / tts / abort
    `-- binary audio chunks
    v
qm_websocket_client
    v
QUMI TCP/TLS transport
```

The component does not capture or play audio, persist or refresh access tokens,
or select character encodings. Audio capture/playback and token acquisition and
early refresh belong to the application or another QUMI component; the agent
only connects, handshakes, routes control messages and audio frames by
`sessionId`, and surfaces them through event callbacks.

## Session Flow

```text
created > connected > HELLO_SENT > READY > ACTIVE > closed
```

1. Initialize with an event callback.
2. Start with the endpoint, product ID (`pid`), device serial number (`sn`),
   `access_token`, and the uplink audio format.
3. After the WebSocket connects (carrying `pid`, `sn`, and
   `Authorization: Bearer <token>`), the component sends `hello` automatically.
4. `LGEINE_AGENT_EVENT_READY` is emitted only after a `helloReply` with `code=0`,
   a `sessionId`, and `audioInfo`; the component then enters `READY`.
5. Send audio and session commands after `READY`; business sends return
   `-QM_EBUSY` before that.
6. Stop and destroy from an application task, not from an event callback.

### Handshake and disconnect

- If `helloReply` does not arrive within the default 10 seconds, the component
  reports `LGEINE_AGENT_ERROR_HELLO_TIMEOUT` and requests a reconnect when auto
  reconnect is enabled; otherwise it closes the connection.
- On a `helloReply` with `code != 0`, the component reports
  `LGEINE_AGENT_ERROR_REJECTED` (the server code is carried in `error.code`) and
  requests a reconnect when enabled, or closes the connection when disabled.
- A WebSocket disconnect invalidates the session; the component clears
  `sessionId`. Reconnects re-run `hello` and acquire a new `sessionId`, so stale
  session audio is never replayed.

### Downlink message rules

- Except for `helloReply`, every downlink JSON message must carry a `sessionId`
  matching the current session; a missing or mismatched `sessionId` is dropped as
  stale or invalid.
- After a successful handshake, invalid business messages are ignored: no error
  is reported and the WebSocket is not closed (protocol section 3.1).
- An unrecognized method or invalid business message is ignored after the
  handshake.
- On a server `abort` with `reason="session_end"`, the component closes the
  WebSocket after ending the current interaction.

## Minimal Use

```c
static void on_event(lgeine_agent_handle_t agent,
                     const lgeine_agent_event_t *event,
                     void *ctx)
{
    (void)ctx;
    if (event->id == LGEINE_AGENT_EVENT_READY) {
        /* The application may now start listening and upload audio. */
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

All string, JSON, and audio pointers in an event are borrowed and valid only
during the callback; binary audio is delivered in chunks without a second
full-frame copy.

## Deployment Environments

When `start_params.uri` is `NULL`, the component builds the endpoint from
`start_params.env`:

| Environment | Default endpoint |
| --- | --- |
| `LGEINE_AGENT_ENV_DEFAULT` (unset) | `wss://voice.ljyd.cn/agent-voicechat/ljyd/v1` (production) |
| `LGEINE_AGENT_ENV_DEV` | `wss://voice-dev.ljyd.cn/agent-voicechat/ljyd/v1` |
| `LGEINE_AGENT_ENV_TEST` | `wss://voice-test.ljyd.cn/agent-voicechat/ljyd/v1` |
| `LGEINE_AGENT_ENV_PROD` | `wss://voice.ljyd.cn/agent-voicechat/ljyd/v1` |

The environment is per connection: it is carried by each `start` call, so
different agents may use different environments. An explicit `start_params.uri`
always takes precedence over `start_params.env`. When `env` is left unset
(`LGEINE_AGENT_ENV_DEFAULT`), the component explicitly resolves it to the
production environment `LGEINE_AGENT_ENV_PROD`.

## API Overview

| Category | API |
| --- | --- |
| Lifecycle | `lgeine_agent_init` / `lgeine_agent_start` / `lgeine_agent_stop` / `lgeine_agent_destroy` |
| State | `lgeine_agent_is_ready` |
| Uplink audio | `lgeine_agent_send_audio` |
| Session commands | `lgeine_agent_send_listen` / `lgeine_agent_send_speak` / `lgeine_agent_send_abort` / `lgeine_agent_send_welcome` / `lgeine_agent_send_goodbye` |

`lgeine_agent_send_audio()` uploads one uplink audio frame. When the uplink
`audio_info.format` is PCM, the component requires the frame length to equal
`rate * bits / 8 * channel * frameDuration / 1000` (protocol 4.3); otherwise it
returns `-QM_EINVAL`. Opus frame length is encoder-defined and not fixed.

Audio timeout zero now means immediate queue admission, not the configured
network timeout. Protocol-lock contention returns `-QM_EBUSY`; a full queue
returns `-QM_EFULL`. Accepted bytes are copied before return and the existing
WebSocket task sends them. Capture callbacks must not retry. Serialize stop and
destroy with external send calls. Other command timeout defaults are unchanged.

### Events and errors

The event callback dispatches on `lgeine_agent_event_t::id`:

| Event | Payload |
| --- | --- |
| `TRANSPORT_CONNECTED` | none |
| `READY` | `ready.audio_info` (negotiated downlink audio) |
| `DISCONNECTED` | none |
| `AUDIO` | `audio` (chunked binary audio) |
| `STT` | `stt.text`, `stt.is_final` |
| `LLM` | `llm.emotion` |
| `TTS` | `tts.state`, `tts.type`, `tts.text` |
| `ABORT` | `abort.reason` (server reason) |
| `ERROR` | `error.type`, `error.code`, `error.message` |

## Configuration

Enable `CONFIG_LGEINE_AGENT_SUPPORT`. Important settings include the hello
timeout, maximum JSON message size, WebSocket receive buffer, network timeout,
task stack/priority, send queue size, and component log categories.

Dependencies are QMOS mutex/timer support, `qm_websocket_client`, `qm_string`,
and the repository's cJSON implementation. Platform build files must compile
`components/cjson/cJSON.c`.
