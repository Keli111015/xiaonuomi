# 资源播放接口说明

2026-10-31 依据当前 `playback.h` / `playback.c` 静态整理。旧版 `qm_player` 注入 decoder / source 的长篇示例已移除，以免复制已失效签名。本轮未编译或播放验证。

## 控制链路

```text
player/play 下行 → 应用方法路由 → 命令队列 / worker
                                    ↓ 可选 guide_words（Agent）
                          resource mode 调整音频占用
                                    ↓
                      playback_play_url → qm_audio_control
                                    ↓
                           音频 source / 编码 / 输出
```

`qm_res_mgr` 负责资源列表、分类、详情和下载；返回资源 URL 后可提交播放。它不是对话 TTS 链路，元数据查到不代表媒体格式可播放。

## 初始化与播放

当前签名：

```c
int playback_init(void);
int playback_play_url(const char *url, uint32_t file_size, uint32_t duration_s);
int playback_play_file(const char *path, uint32_t file_size, uint32_t duration_s);
int playback_submit_cloud_play(const playback_cloud_play_request_t *request);
```

`main.c` 在 `voice_start()` 后调用 `playback_init()`，不传 audio、decoder 或 source 参数。初始化创建 control、锁、订阅、队列和 worker；重复初始化返回忙。

URI / FILE 请求都声明 16000 Hz、单声道、PCM S16 STREAM。`file_size` 为字节数，`duration_s` 为秒；文件是否能打开取决于实际 source 后端和挂载情况。

云控结构含 `guide_words`、`url`、`audio_type`、`title`、`file_size`、`duration_s`。提交函数会在返回前复制字符串；调用方只需保证调用期间参数有效。audio_type 缺省为 pcm，其他格式拒绝；队列、内存或后端错误仍可能导致失败。

## 生命周期

```c
int playback_pause(void);
int playback_resume(void);
int playback_stop(void);
int playback_get_status(qm_audio_control_snapshot_t *status);
int playback_deinit(void);
```

控制操作针对最近接受的 operation；无初始化或无 operation 时可能返回 `-QM_EINIT` / `-QM_ENOENT`。get_status 使用 control snapshot，不是旧 `qm_player_status_t`。提交 API 成功不表示喇叭已出声，需结合事件、状态与实际输出判断。

资源播放会与 Agent 的采集、上行及喇叭使用协调；完成/停止后释放 operation 并恢复音频。不要在 MQTT 或默认事件回调中同步等待资源下载、查询或播报结束。

## 元数据与下载

在 `CONFIG_QM_RES_MGR_SUPPORT` 开启时提供：

| 接口 | 用途 |
| --- | --- |
| `playback_set_resource_env` | HTTP 元数据模式选择环境；MQTT 模式不提供相同环境切换能力 |
| `playback_print_first_page` | 查询第一页并打印，再释放结果 |
| `playback_get_categories` | 一级分类，结果按 QUMI API 释放 |
| `playback_get_child_categories` | 子分类，结果按 QUMI API 释放 |
| `playback_play_id` | 查资源详情、检查 PCM 类型、提交 URL |
| `playback_download` | 使用调用方数据回调接收下载字节 |

`playback_metadata_t` 始终含 did；HTTP 配置分支另含 product_id / token，MQTT 分支含 iot_handle。参数指针在调用期间借用，结果所有权依 QUMI API 约定，不能把查询结果当永久全局引用。

## 配置与验收

当前 `CONFIG_LJYD_PLAYBACK_SUPPORT=1`，`CONFIG_QM_RES_MGR_SUPPORT=1`，元数据选择 MQTT；实际宏以 `main/qm_config/qm_config.h` 为准。不要用文档手动切回旧的初始化接口。

需要验收云控路由、guide words 结束边界、PCM 输出、停止/唤醒抢占、错误恢复、资源元数据超时以及语音恢复。历史记录不证明当前版本全部通过，项目状态见[交接](../../../../../docs/CURRENT_HANDOFF.md)。
