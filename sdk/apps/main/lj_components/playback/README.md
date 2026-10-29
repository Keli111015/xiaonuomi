# Application Playback

This directory lives at `apps/main/lj_components/playback` and wraps the QUMI
`qm_player` and `qm_res_mgr` APIs used by
the resource playback guide.

The component provides:

- player creation around an existing shared `qm_audio` handle;
- URL and injected-file playback;
- resource list/category/detail lookup through the configured MQTT metadata channel;
- resource download through an application-owned data callback;
- pause, resume, stop, status, and teardown helpers;
- application-owned `player/play` handling with guide-word TTS followed by PCM resource playback;
- capture/upload arbitration while a resource owns the shared audio path.

`playback_init()` requires a decoder implementation and an optional local-file
source adapter. This app supplies `resource_pcm_decoder`, which currently accepts
only cloud `audioType=pcm` (16 kHz, mono, signed 16-bit raw PCM). The component
does not create the shared audio handle. The current `main.c` passes `NULL` for
`source_if`, so the default integration only enables HTTP URI playback; local
file playback requires an application-provided source adapter.

`CONFIG_LJYD_PLAYBACK_SUPPORT` in `apps/main/qm_config/qm_config.h` controls the
application component. When it is `1`, `apps/main/CMakeLists.txt` builds both
playback sources and `main.c` calls `playback_init()` after `voice_start()`.
When it is `0`, the sources, cloud command route, wakeup stop hook, and startup
initialization are omitted.
This is an application wiring switch; it does not disable QUMI's
`CONFIG_QM_PLAYER_SUPPORT` or `CONFIG_QM_RES_MGR_SUPPORT` by itself.

Resource-manager list/detail calls are synchronous and must run from an
application worker, not an MQTT receive callback. Direct
`playback_play_url()`/`playback_play_file()` calls submit to `qm_player`, while
`playback_submit_cloud_play()` first queues a copied cloud request on this
component's worker. No return value means that audio is already audible; use
the player event log or status API to observe the actual lifecycle.

This app keeps `CONFIG_QM_PLAYER_CLOUD_SUPPORT` disabled. MQTT `player/play`
routing remains application-owned and runs in the playback worker; no QUMI cloud
source is modified or registered a second time. The current application decoder
accepts only `audioType=pcm` (16 kHz, mono, signed 16-bit raw PCM).
