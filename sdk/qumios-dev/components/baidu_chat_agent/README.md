# baidu_chat_agent

`baidu_chat_agent` is a compact QUMI component for Baidu realtime LLM interaction. The current version focuses on the main WebSocket path from the Baidu RTC documentation, plus text/audio transport and key prefix-based message dispatch so upper layers can bring up the interaction path quickly.

## Architecture

Audio send timeout zero now means immediate queue admission, not the configured
network timeout. Queue pressure returns `-QM_EBUSY` or `-QM_EFULL`. Accepted bytes
are copied by WebSocket before return and sent by its existing task. Capture
callbacks must not retry. Serialize stop/destroy with external sends. Other
command timeout defaults are unchanged.

```text
application or qm_agent transport
    | config, text/audio, event callback
    v
baidu_chat_agent
    | URI validation, protocol prefixes, license activation
    v
qm_websocket_client
    v
QUMI TCP/TLS transport
    v
Baidu realtime service
```

The component owns the realtime protocol and session lifecycle. Applications
own credentials, audio capture/playback, and the HTTP-side instance/token
preparation flow.

## File layout

- `include/baidu_chat_agent.h`: public API, config, events, and message types.
- `include/baidu_chat_agent_log.h`: internal category-aware logging macros.
- `src/baidu_chat_agent.c`: WebSocket session management, send APIs, and basic message parsing.
- `qm_kconfig`: component switch and default WebSocket settings.

## Logging

`BAIDU_CHAT_AGENT_LOG_CONFIG` is the master Kconfig switch for category
defaults. When disabled, only error and lifecycle logs are enabled. When
enabled, select `BAIDU_CHAT_AGENT_LOG_ERROR`,
`BAIDU_CHAT_AGENT_LOG_LIFECYCLE`, `BAIDU_CHAT_AGENT_LOG_CONTROL`,
`BAIDU_CHAT_AGENT_LOG_UPLINK`, `BAIDU_CHAT_AGENT_LOG_DOWNLINK`,
`BAIDU_CHAT_AGENT_LOG_PAYLOAD`, and `BAIDU_CHAT_AGENT_LOG_STATS`.

The runtime mask is component-wide and applies to every session:

```c
baidu_chat_agent_set_log_flags(BAIDU_CHAT_AGENT_LOG_ERROR |
                               BAIDU_CHAT_AGENT_LOG_LIFECYCLE |
                               BAIDU_CHAT_AGENT_LOG_DOWNLINK);
```

Use `baidu_chat_agent_set_log_flags(BAIDU_CHAT_AGENT_LOG_NONE)` to disable all
component logs without changing `qm_websocket_client` or the global QUMI log level.

## Current scope

- Create, start, stop, and destroy a `baidu_chat_agent` session.
- Build the WebSocket URI from documented query fields with two auth modes:
  - `a + id + t + ac + [ptime] + [plen]`
  - `a + ak + sk + ac + [ptime] + [plen] + [cfg]`
- The component internally uses the documented endpoint: `wss://rtc-aiotgw.exp.bcelive.com/v1/realtime`
- URL-encode query values before connection.
- Apply structured documented validation for `auth_mode / ac / ptime / plen / cfg`:
  - auth fields are mutually exclusive between the two modes
  - instance-token mode rejects `cfg`
  - `ac` only allows `raw/raw16k/pcma/pcmu/g722/opus`
  - `ptime` only allows `20/40/60`
  - non-`opus` modes reject `ptime` and `plen`
  - `cfg` must be a valid JSON object
  - if `cfg.audiocodec` exists, it must match `ac`
  - if `cfg.language` exists, it only allows `zh/en`
  - if `cfg.samplerate` exists, it only allows `8000/16000`
  - if `cfg.channels` exists, it only allows `1/2`
  - if `cfg.dfda` exists, it must be a boolean value
  - if `cfg.aopt` exists, it only allows `20/40/60`
- Send binary audio.
- Send `"[T]:"` text, `"[TTS]:"` text, and `"[E]:"` event messages.
- Send `"[B]:"` break messages.
- Send documented high-level helper commands without manual string assembly:
  - `baidu_chat_agent_send_asr_start_longtext_rec()`
  - `baidu_chat_agent_send_asr_stop_longtext_rec()`
  - `baidu_chat_agent_set_asr_realtime_enabled()`
  - `baidu_chat_agent_send_remote_player_control()`
- Dispatch downlink text by Baidu realtime interaction prefixes.
- Classify known `"[E]:"` and `"[SET]:"` payloads into structured `semantic` values on `baidu_chat_agent_text_t`, including:
  - `TTS_BEGIN_SPEAKING / TTS_END_SPEAKING`
  - `VOICE_COMING / VOICE_DISAPPEAR`
  - `MEDIA_READY`
  - `REMOTE_PLAYER_*`
  - `ASR_*`
  - `MEDIA_GENERATE_MODE(FALSE)`
- Auto-handle the documented license activation flow:
  - receive `"[E]:[LIC]:[MUST]:XXXX"`
  - send `"[E]:[LIC]:[ACTIVE]:{\"devId\":\"...\",\"uId\":\"...\",\"licKey\":\"...\"}"`
  - recognize `"[E]:[LIC]:[RES]:[PASS]:"` and `"[E]:[LIC]:[RES]:[FAILED]:"`
  - report a dedicated `BAIDU_CHAT_AGENT_EVENT_LICENSE` state for `MUST / ACTIVE_SENT / PASS / FAILED`
- Reuse runtime identity fields for license activation:
  - `device_id -> devId`
  - `user_id -> uId`
  - `license_key -> licKey`
- The OpenCPU example resolves runtime identity conservatively:
  - `device_id` prefers the STA MAC when the config field is empty
  - `user_id` falls back to the resolved `device_id` when the config field is empty
  - `license_key` stays empty by default and should be filled with a purchased licKey
- Forward downlink binary audio frames.
- The `qm_agent` transport wrapper already consumes part of the structured semantics:
  - `TTS_BEGIN_SPEAKING` -> speaking
  - `TTS_END_SPEAKING` -> silent
  - `VOICE_COMING` -> listening
  - `VOICE_DISAPPEAR` -> silent
  - `MEDIA_READY` -> thinking
- For `qm_agent + GX8006`, avoid blindly reusing one `frame_ms` value for both directions:
  - uplink `input_audio.frame_ms` drives Baidu `ptime`
  - downlink `output_audio.frame_ms` is better treated as local speaker pacing

## Not covered yet

- HTTP-side prepare flow such as create-instance and token acquisition.
- Dedicated public qm_agent APIs for direct access to the new baidu_chat_agent helper commands.
- Strict audio pacing enforcement beyond basic field validation.

## Direct API notes

The OpenCPU example in this repo is wired through `qm_agent`, so it does not directly call the new `baidu_chat_agent_*` helper APIs. Those helpers are meant for direct `baidu_chat_agent` integrations, for example:

```c
baidu_chat_agent_set_asr_realtime_enabled(handle, QM_TRUE, 0U);
baidu_chat_agent_send_remote_player_control(handle,
                                            BAIDU_CHAT_AGENT_REMOTE_PLAYER_PAUSE,
                                            0U);
```
