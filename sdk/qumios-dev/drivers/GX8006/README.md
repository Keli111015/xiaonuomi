# GX8006 UART Protocol Driver

This directory owns the MCU-independent GX8006 UART protocol:

- `include/nc_audio.h` exposes message types and commands.
- `src/nc_audio.c` builds commands and incrementally validates module frames.
- `qm_kconfig` provides the protocol and ESP32-S3 binding configuration.

It does not implement `qm_audio` or select board pins. The ESP32-S3 binding is
at `platform/mcu/esp32s3/qm_audio/qm_audio_device_gx8006.c`; board power,
GPIO routing, and amplifier policy remain outside this driver.

## Native Audio Profile

The binding exposes one fixed full-duplex profile: Opus, 16 kHz, mono, S16
decoded geometry, 40 ms packets, and fixed 80-byte CBR payloads. PCM is not
sent to the module. `qm_audio` uses the PCM geometry only when a software
decoder or Opus encoder is needed before the packet reaches this driver.

Capture delivers complete Opus packets. Render accepts complete packets only;
the V1 protocol emits one SPK DATA command per packet. A packet with incomplete
or mismatched profile information is rejected instead of being split or guessed.

The ESP32-S3 binding uses PUSH capture. `protocol_event()` directly submits packets with
`qm_audio_capture_feed(&input, QM_EOK)`; the UART loop submits terminal source
failures with `qm_audio_capture_feed(NULL, error)`. The component supplies that
entry point. The binding has no capture-mode branches, read API, or capture ring.
A successful stop waits for the entire active capture callback and any fault
submission; normal stop is not reported as a source error.

Normal playback completion is a drain barrier: after the source finishes, the
binding sends SPK STOP and waits for the module's `0x12/0x03` completion state.
`0x12/0x02` only means the module is temporarily empty. STOP therefore drains
accepted audio and does not promise immediate silence. Playback position and
pause/resume remain unsupported. A failed MIC_OFF or playback stop keeps the
route live for retry; it is never reported as a safe close.

## Configuration And Use

Enable `qm audio` and choose `GX8006 UART Opus device (ESP32-S3)` under `qm
audio device`. That choice selects `QM_GX8006_SUPPORT` and exposes UART, GPIO,
gain and graceful-stop settings. Select PUSH in the generic `qm_audio`
capture-mode choice to match this binding. The OpenCPU JoyInside example uses
this native profile automatically when the device is selected.

The protocol driver may also be used without `qm_audio`; then the application
owns UART setup, receive-loop calls to `nc_audio_recv_push()`, and all device
lifecycle ordering. Do not use that standalone path and the `qm_audio` binding
against the same physical module concurrently.

UART baud rate, capture aggregation, module firmware behavior, and acoustic
completion timing still require target-board verification.
