# lgeine_agent Example

Chinese documentation: [README_CN.md](README_CN.md)

`lgeine_agent_example.c` shows initialization, the automatic `hello` handshake,
starting realtime listen mode after READY, and handling STT/LLM/TTS/audio/error
events. The caller supplies the platform URI, product ID, device serial number,
and access token; the example contains no credentials.

Add the example source to the product application, enable
`CONFIG_LGEINE_AGENT_SUPPORT`, and call:

```c
lgeine_agent_handle_t agent =
    lgeine_agent_example_start(platform_uri, product_id, device_sn, access_token);
```

The product audio pipeline should consume audio during
`LGEINE_AGENT_EVENT_AUDIO` and upload captured frames with
`lgeine_agent_send_audio()`.
