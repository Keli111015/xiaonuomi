# macOS CoreAudio PCM 驱动

该目录通过 `qm_audio_driver_if_t` 把 macOS AudioQueue 和 VoiceProcessingIO 接入 QUMI
音频服务。公共接口和实时数据路径使用 C；只有麦克风授权检查使用
Objective-C/AVFoundation。

## 数据流

```mermaid
flowchart LR
    mic["默认麦克风"] --> input["AudioQueue Input\nPCM callback"]
    input --> ring["有界采集 ring buffer\n满时丢最旧帧"]
    ring --> read["driver read()"]
    read --> agent["qm_audio / qm_agent / JoyInside"]
    agent --> write["driver write()"]
    write --> pool["固定播放 buffer pool\n无空闲时背压等待"]
    pool --> output["AudioQueue Output"]
    output --> speaker["默认扬声器"]
```

## 格式与控制

- 输入和输出仅接受交错、有符号、native-endian linear PCM。
- JoyInside macOS app 当前使用 16 kHz、单声道、16 bit；上行 40 ms（1280 字节），
  下行 60 ms（1920 字节）。
- `read()` 按配置帧长返回完整 PCM 帧，支持 `0`、有限毫秒数和无限等待超时语义。
- `write()` 可接收一个或多个 PCM 帧，并拆分到固定 AudioQueue buffer 池。
- 音量范围为 `0..100`；支持静音、立即停止播放，以及录音 `AUTO/KEEP/PAUSE`。
- 采集 ring buffer 满时丢弃最旧帧，避免把过期语音继续上传。

## 权限与验证

App bundle 必须包含 `NSMicrophoneUsageDescription`。首次使用会请求麦克风授权；拒绝、
超时或没有输入/输出设备时，驱动返回明确错误。

```sh
cmake --build /tmp/qumi-macos-host-build --target qm_macos_audio_smoke
QM_MACOS_AUDIO_SMOKE_SECONDS=3 \
/tmp/qumi-macos-host-build/qm-macos-audio-smoke.app/Contents/MacOS/qm-macos-audio-smoke
```

默认测试采集和静音播放路径；设置 `QM_MACOS_AUDIO_LOOPBACK=1` 可把采集 PCM 回放，
测试时应使用耳机避免声学反馈。

JoyInside 使用 VoiceProcessingIO 驱动。播放云端音频时保持麦克风采集，并保持 Apple
语音处理开启，由系统提供回声消除、降噪和自动增益。该驱动在播放会话之间保持音频单元
运行，后续 TTS 不需要重新启动 AudioQueue。

## AudioQueue 路径边界

独立的 AudioQueue 测试驱动不包含 Opus 编解码、回声消除、降噪、自动增益或多设备选择。
VoiceProcessingIO 同样使用系统默认设备。
