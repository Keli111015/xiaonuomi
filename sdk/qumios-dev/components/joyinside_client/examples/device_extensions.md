# 设备扩展接线示例 / Device extension examples

以下片段使用公开 API。`app_*` 表示产品已有的任务队列、持久化或硬件后端，不是组件提供的函数。事件回调仅复制、入队；耗时操作在应用工作线程执行。每次入队、上报和生命周期调用均需检查返回值。

## 语音配置与 VAD

保存应用目标配置，初始化和运行时更新复用同一个结构。更新不是局部补丁，可选字段省略也不等于云端恢复默认值。

```c
joyinside_client_voice_config_t voice = {
    .input_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM,
    .output_codec = JOYINSIDE_CLIENT_AUDIO_CODEC_MP3,
    .input_sample_rate = 16000,
    .output_sample_rate = 16000,
    .input_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64,
    .output_transport = JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY,
    .vad = QM_TRUE,
    .output_stream = QM_TRUE,
};
/* On a new speech segment, before its first audio frame. / 新语音段首帧前。 */
int ret = joyinside_client_vad_round_begin(client);
if (ret == QM_EOK) ret = joyinside_client_send_audio(client, frame, frame_bytes);
/* Handle EBUSY/EFULL; do not replay a frame into a later round. / 处理忙或满，勿跨轮重放。 */

/* From application control code. / 应用控制流程。 */
voice.chat_role_code = selected_role;
voice.voice_speed = 1.0f;
ret = joyinside_client_update_voice(client, &voice);
/* QM_EOK: wait for VOICE_UPDATED, or CONNECTED after reconnect. */
```

上行采样、分帧与节奏由采集/编码器负责；VAD 必须由产品明确检测段落边界。显式二进制上行无法携带已公开的 `content.mid` 字段，因此不允许和端侧 VAD 组合。

## 闹钟与普通设备指令

直接接入客户端时，从 `CALL_SKILL_EVENT` 中读取统一视图，先校验整组指令，再按顺序交付业务任务；需要跨回调保存时复制字符串和 `params`，不能浅拷贝结构。

```c
const joyinside_client_skill_t *skill = &event->payload.server_event.content.skill;
for (uint32_t i = 0; i < skill->instruction_count; ++i) {
    joyinside_client_instruction_t instruction;
    int ret = joyinside_client_skill_instruction(skill, i, &instruction);
    if (ret != QM_EOK) {
        /* Reject malformed batch before executing it. / 执行前拒绝非法整组指令。 */
        return;
    }
    /* app_validate_instruction() inspects borrowed fields; it does no I/O. */
    if (app_validate_instruction(skill->domain, &instruction) != QM_EOK) return;
}
/* Must deep-copy the entire batch before this callback returns.
 * 必须在回调返回前复制整组数据；入队失败由应用记录并处理。 */
int ret = app_enqueue_skill_copy(skill);
```

| 场景 | type | operation | params |
| --- | --- | --- | --- |
| 普通设备指令 | NULL | 原 `action` | 原 `args` 对象 |
| 闹钟 V2 | `normal_alarm` / `interval_reminder` | `create` / `delete` / `delete_all` | 原 `params` 对象 |

业务任务负责校验时间、容量、持久化和 RTC，完成后用 `joyinside_client_update_chat_context()` 上报全量闹钟列表。不要只报告本次增量，以免云端上下文与本地数据不一致。

通过 qm_agent 接入时，在模块 `on_event` 中处理 `QM_AGENT_MODULE_EVENT_COMMAND`：

```c
if (module_event->id == QM_AGENT_MODULE_EVENT_COMMAND) {
    const qm_agent_event_t *command = module_event->data;
    /* Full document, borrowed for this call. / 完整文档，仅本次回调借用。 */
    return app_enqueue_command_copy(command->data.command.domain,
                                    command->data.command.json);
}
```

该回调在 Agent 控制任务上运行。公共 `QM_AGENT_EVENT_COMMAND` 通知是有界快照，仅供观察；长 JSON 会标记 `truncated`，不可执行截断内容。

## 视觉上传

启用 `CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT`，建连时令 `params.need_asr_partial = QM_TRUE`。收到 ASR `IS_START` 后向应用视觉任务发出触发信号；应用可合并重复触发，避免并发堆积。

```c
#include "joyinside_client_vision.h"

/* Application worker; JPEG remains valid until the call returns.
 * 应用工作线程，JPEG 内存在调用返回前保持有效。 */
joyinside_client_image_t image = {
    .app_id = vision_app_id,
    .jpeg = captured_jpeg,
    .jpeg_len = captured_bytes,
    .timestamp_ms = capture_unix_ms,
    .timeout_ms = 5000,
};
int ret = joyinside_client_report_image(client, &image);
app_release_camera_frame(captured_jpeg);
```

组件复制当前 WS 的 Token、botId、sessionId，生成本次 HTTP requestId，并流式发送 Base64。调用入口与 stop/destroy 由应用串行；返回后再释放相机帧。上传失败可丢弃过期帧，由下一次触发上传新帧，避免重复占用语音带宽。产品需开通视觉服务及对应 appId。

## 有声书 / 音乐

启用 `CONFIG_JOYINSIDE_CLIENT_MEDIA_SUPPORT`，在 client start 前创建控制器，并设置建连 `feature="AUDIO_BOOK_V2"`。后端负责完整 URL 播放、绝对位置 seek 和进度；操作函数只提交请求。

```c
joyinside_client_media_config_t media_config = {
    .client = client,
    .backend = {
        .start = app_player_start,
        .stop = app_player_stop,
        .wait_bridge = app_wait_tts_drained,
        .context = player_context,
    },
};
joyinside_client_media_t *media = NULL;
int ret = joyinside_client_media_create(&media_config, &media);
```

后端保存 generation 和自有 URL 副本；`start()` 的 `play` 及字符串仅借用至返回。只有实际观察到对应状态后才调用：

```c
int ret = joyinside_client_media_notify(media, generation, observation, progress_sec);
/* EFULL: retain and retry a terminal observation while media is alive.
 * 终态入队满时保留并重试；媒体销毁成功后不得再次调用。 */
```

| observation | 后端触发条件 |
| --- | --- |
| `BRIDGE_DRAINED` | 服务端 TTS 完成且本地扬声器已排空 |
| `STARTED` | 实际开始播放 |
| `PROGRESS` | 更新资源绝对播放秒数 |
| `FINISHED` | 自然播完且该 generation 的后端已静止 |
| `STOPPED` / `FAILED` | 已停止或失败，且不再访问该 generation |

PLAY/STOP/PING 和完播上报由控制器负责。唤醒或闹钟抢占时调用 `joyinside_client_media_interrupt()`。停止顺序：停止外部请求 → `client_stop()` → `media_destroy()` → `client_destroy()`；任一步超时则保留对象，等待后端静止后重试。

## 打印

在 client start 前注册 `joyinside_client_print_register()`。下行回调深拷贝 `request->content` 后入队，可统一覆盖 START、STOP、CONFIG、IMAGE_DISPLAY 和 STATUS_QUERY。

```c
static int on_print(const joyinside_client_print_request_t *request, void *context)
{
    cJSON *content = cJSON_Duplicate(request->content, 1);
    if (!content) return -QM_ENOMEM;
    /* On success the application owns content and eventually deletes it.
     * 成功后队列接管 content，并由工作任务最终释放。 */
    int ret = app_print_enqueue(context, content);
    if (ret != QM_EOK) cJSON_Delete(content);
    return ret;
}
```

工作任务解析 `content.eventType/eventData`，操作真实硬件。任务 ID、图片 ID 和状态来自实际任务/硬件；以下 JSON 仅展示报文形状。

```c
/* Actual hardware snapshot after STATUS_QUERY or a state change. / 硬件状态快照。 */
ret = joyinside_client_print_send_event(client, JOYINSIDE_CLIENT_PRINT_EVENT_STATUS,
    "{\"battery\":85,\"printerStatus\":[\"normal\"]}", 0);
/* Only after printing finishes, not on queue admission. / 实际打印完成后。 */
ret = joyinside_client_print_send_event(client, JOYINSIDE_CLIENT_PRINT_EVENT_SUCCESS,
    "{\"printTaskId\":\"task-from-request\",\"printSuccess\":1}", 0);
/* Only after the requested image has been displayed. / 指定图片显示完成后。 */
ret = joyinside_client_print_send_event(client, JOYINSIDE_CLIENT_PRINT_EVENT_IMAGE,
    "{\"printTaskId\":\"task-from-request\",\"imageId\":\"image-from-request\"}", 0);
```

实际程序用 cJSON 构建这些字段，避免手工拼接未转义的任务 ID。缺纸、过热等异常不能同时报告 `normal`；失败的上报由应用按任务状态决定重试，组件不伪造硬件成功。
