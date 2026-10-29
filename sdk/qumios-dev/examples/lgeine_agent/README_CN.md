# lgeine_agent 示例

英文文档：[README.md](README.md)

`lgeine_agent_example.c` 演示初始化、自动 `hello` 握手、READY 后启动实时监听，
以及 STT/LLM/TTS/音频/错误事件处理。服务 URI、产品 ID、设备序列号和 access token 由
调用方传入，示例不包含凭据。

将示例源码加入产品应用并开启 `CONFIG_LGEINE_AGENT_SUPPORT`，然后调用：

```c
lgeine_agent_handle_t agent =
    lgeine_agent_example_start(platform_uri, product_id, device_sn, access_token);
```

产品音频管线应在 `LGEINE_AGENT_EVENT_AUDIO` 中消费下行音频，并通过
`lgeine_agent_send_audio()` 上传采集到的音频帧。
