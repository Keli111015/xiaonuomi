# QM AGENT Example

中文文档：[README_CN.md](README_CN.md)

This example shows how to create and run a `qm_agent` session.

## Files

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

## What It Shows

- Create, start, stop, and destroy a `qm_agent` session.
- Start STA WiFi and create the `qm_agent` session after the device gets an IP.
- Register a default event-loop handler for connection, state, text, and error
  notifications. Audio and video frames remain on their respective data paths.
- Use a local custom transport by default so the example can run without cloud
  credentials.
- Call `qm_agent_send_video()` to verify the upstream video path.
- Each example initializes the process-wide `qm_audio` service and creates a
  dedicated `qm_audio_control_t` for the Agent session. The target must provide
  a platform `qm_audio_device_*` backend; the examples do not inject a fake
  board driver.
- Optionally switch to JoyInside by disabling the custom transport and filling
  the JoyInside configuration values.
- `agora_cloud_example.c` shows the cloud-auth + dynreg + Agora transport path.
- `joyinside_cloud_example.c` shows the cloud-auth + dynreg + JoyInside transport path.

## Entry Points

```c
qm_application_start();
qm_agent_example_start();
qm_agent_example_stop();
qm_agent_example_send_text("hello");
qm_agent_example_request_tts("test tts");
qm_agent_example_send_video(frame_data, frame_len, &video_format);
qm_agent_example_interrupt();
```

## Integration

Add `qumi/examples/qm_agent/qm_agent_example.c` to the project source list and merge
the relevant options from `qumi/examples/qm_agent/qm_config/qm_config.h` into the
target project's generated configuration. `QM_AGENT_EXAMPLE_CONFIG` is exposed
only when `QM_AGENT_SUPPORT` is enabled.

The cloud examples can also be added independently:

- `qumi/examples/qm_agent/agora_cloud_example.c`
- `qumi/examples/qm_agent/joyinside_cloud_example.c`

When merging the sample configuration directly, keep `QM_AUDIO_SUPPORT` and a
platform audio device backend enabled so the shared `qm_audio` service can back
Agent capture and playback.
