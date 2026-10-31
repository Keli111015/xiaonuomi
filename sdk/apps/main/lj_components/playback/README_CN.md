# 应用层资源播放

2026-10-31 按当前 `playback.h/.c` 静态整理。本轮未编译或测试。旧文档中的 `playback_init(audio, decoder_if, source_if)` 已不符合当前接口。

## 当前实现

- `playback_init(void)` 创建 `qm_audio_control`，订阅事件并启动云控命令 worker；从 `main.c` 在 voice 启动后调用。
- URL 和本地文件分别构造 `QM_AUDIO_SOURCE_URI` / `QM_AUDIO_SOURCE_FILE` 请求，提交给统一 audio control。
- 输入格式为 16 kHz、单声道、S16 原始 PCM，由当前音频层路径处理。不能把仓库保留的旧 `resource_pcm_decoder` 文件等同于当前应用初始化方式。
- `player/play` 由应用路由复制请求到 worker，先处理可选 `guide_words`，再播放资源；缺省 audio_type 为 pcm，非 pcm 拒绝。
- 与 Agent 共享音频路径，资源阶段通过 resource mode 调整采集/上行，结束后恢复；提交成功不等于已经出声。
- `qm_res_mgr` 用于元数据查询和下载，查询可能同步等待，应放 worker，不能阻塞 MQTT 回调。
- 操作 ID 的发布、控制和释放由应用锁串行化；释放失败保留 ID，worker 每 100ms 检查终态并重试，覆盖 play 返回前已完成的操作。退出时拒绝新命令，等待 worker 挂起、清理待处理命令，再释放操作和控制对象；超时可重试 deinit。上述并发路径本轮仅源码复核，未编译或实测。

## 接口与限制

接口分组、示例和配置见[接口说明](resource_playback.md)，真实签名以 `playback.h` 为准。`CONFIG_LJYD_PLAYBACK_SUPPORT` 当前为 1，控制应用接入；不等于所有底层能力、文件系统或格式均已验证。

本地文件 API 的存在不代表挂载了可用文件系统；需音频 source 后端支持对应路径。MP3 / WAV / AAC 等格式不属于当前云控 PCM 路径的支持承诺。

项目架构见[应用说明](../../../README_CN.md)，进度及问题见[当前交接](../../../../../docs/CURRENT_HANDOFF.md)。
