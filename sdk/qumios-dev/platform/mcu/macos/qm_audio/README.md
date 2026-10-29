# macOS CoreAudio PCM Drivers

This directory adapts macOS AudioQueue and VoiceProcessingIO to
`qm_audio_driver_if_t`. Public and real-time data paths remain C; only
microphone authorization uses an Objective-C AVFoundation bridge.

## Data Flow

```mermaid
flowchart LR
    mic["Default microphone"] --> input["AudioQueue Input\nPCM callback"]
    input --> ring["Bounded capture ring\ndrop oldest on full"]
    ring --> read["driver read()"]
    read --> agent["qm_audio / qm_agent / JoyInside"]
    agent --> write["driver write()"]
    write --> pool["Fixed playback buffer pool\nwait when exhausted"]
    pool --> output["AudioQueue Output"]
    output --> speaker["Default speaker"]
```

## Format And Controls

- Input and output accept interleaved signed native-endian linear PCM only.
- The JoyInside macOS app uses 16 kHz, mono, 16-bit PCM: 40 ms/1280-byte uplink
  frames and 60 ms/1920-byte downlink frames.
- `read()` returns one complete configured PCM frame and supports no-wait,
  finite millisecond, and infinite timeout semantics.
- `write()` accepts one or more frames and splits them across a fixed AudioQueue
  buffer pool.
- Volume is `0..100`; mute, immediate playback stop, and capture
  `AUTO/KEEP/PAUSE` are supported.
- When capture falls behind, the bounded ring drops the oldest frames so stale
  speech is not uploaded later.

## Permission And Validation

The application bundle must provide `NSMicrophoneUsageDescription`. First use
requests microphone access. Permission denial, timeout, or missing audio devices
is reported as an error.

```sh
cmake --build /tmp/qumi-macos-host-build --target qm_macos_audio_smoke
QM_MACOS_AUDIO_SMOKE_SECONDS=3 \
/tmp/qumi-macos-host-build/qm-macos-audio-smoke.app/Contents/MacOS/qm-macos-audio-smoke
```

The default smoke test covers capture and silent playback. Set
`QM_MACOS_AUDIO_LOOPBACK=1` to replay captured PCM; use headphones to avoid
acoustic feedback.

JoyInside uses the VoiceProcessingIO driver. It keeps microphone capture active
while transport audio is playing and leaves Apple's voice processing enabled,
which provides system echo cancellation, noise suppression, and gain control.
The driver keeps its audio unit running between playback sessions, so later TTS
chunks do not need to restart AudioQueue.

## AudioQueue Boundary

The standalone AudioQueue smoke driver does not provide Opus, acoustic echo
cancellation, noise suppression, automatic gain control, or explicit device
selection. VoiceProcessingIO follows the system default devices.
