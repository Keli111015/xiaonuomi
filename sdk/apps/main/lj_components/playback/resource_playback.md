# 应用层资源播放

本文档描述当前仓库 `apps/main/lj_components/playback` 的实际实现。该目录是一个应用层
包装，不是 QUMI 的通用资源播放组件。

当前代码提供两类能力：

- 使用 `qm_player` 播放 URL 或由应用层 source 适配器提供的本地文件；
- 使用 `qm_res_mgr` 通过当前配置的 MQTT 元数据通道查询资源，并把资源详情
  转换成 `qm_player` 播放请求。

资源播放和语音对话是两条不同的控制路径，但最终共享同一个 `qm_audio` 输出。
资源播放使用下面的链路：

```text
资源元数据 MQTT 查询
        |
        v
qm_res_mgr --获取资源 URL--> qm_player
                              |
                 +------------+------------+
                 |                         |
              HTTP URL                 本地文件
                 |                         |
              内置 HTTP source       应用 source_if
                 +------------+------------+
                              |
                           decoder_if
                              |
                           qm_audio
                              |
                         音频驱动/喇叭
```

当前工程的语音对话使用 `qm_agent` 和 Agora transport。日志中的
`dialog=speaking`、`downlink first frame`、TTS 和 Opus 解码属于
`qm_agent -> qm_audio` 路径，不会自动进入本文件的 `playback_*()` 接口；两条路径
在音频仲裁层竞争同一个输出资源。

## 当前工程状态

当 `CONFIG_LJYD_PLAYBACK_SUPPORT=1` 时，`apps/main/CMakeLists.txt` 才会把播放器
和应用层 PCM 解码器加入编译。`apps/main/main.c` 的启动顺序是：

```text
voice_start()
  -> playback_init(voice_get_audio(), resource_pcm_decoder_get_if(), NULL)
  -> ljyd_cloud_init()
  -> ljyd_provision_init()
  -> agent_app_init()
```

当 `CONFIG_LJYD_PLAYBACK_SUPPORT=0` 时，`playback.c`、
`resource_pcm_decoder.c` 不加入应用编译，`main.c` 不调用初始化，应用层也不注册
`player/play` 路由或在唤醒时调用资源播放器的停止接口。该宏只控制应用层接入，
不会自动关闭 QUMI 的 `CONFIG_QM_PLAYER_SUPPORT` 或 `CONFIG_QM_RES_MGR_SUPPORT`。

启动时由应用层提供 `resource_pcm_decoder`，它把 HTTP 返回的 16 kHz、单声道、
16-bit 原始 PCM 按 20 ms 编码为 Opus frame，再送入共享 `qm_audio`。当前云控
资源链路只接受 `audioType=pcm`；MP3/WAV/AAC/FLAC 的元数据仍可查询，但没有在
本应用层实现对应解码器。

资源播放器未初始化或没有当前 operation 时，控制接口会返回 `-QM_EINIT` 或
`-QM_ENOENT`。

## 配置边界

当前 `apps/main/qm_config/qm_config.h` 的相关配置为：

```c
#define CONFIG_LJYD_PLAYBACK_SUPPORT 1

#define CONFIG_QM_PLAYER_SUPPORT 1
#define CONFIG_QM_PLAYER_DECODER_SUPPORT 1
#define CONFIG_QM_PLAYER_CLOUD_SUPPORT 0

#define CONFIG_QM_RES_MGR_SUPPORT 1
#define CONFIG_QM_RES_MGR_HTTP_SUPPORT 0
#define CONFIG_QM_RES_MGR_MQTT_SUPPORT 1
#define CONFIG_QM_RES_MGR_PAGE_SIZE 10
#define CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT 15000
```

含义如下：

- `CONFIG_LJYD_PLAYBACK_SUPPORT` 控制本目录的应用层资源播放组件；设为 `0`
  时，CMake 不编译组件源文件，`main` 不初始化组件，应用层也不注册
  `player/play` 路由或执行唤醒停止调用；
- `qm_player` 和解码器边界已启用，但具体解码器仍由应用层提供；
- `qm_player_cloud` 云控适配已关闭，QUMI 不会自动接管 `player` 方法；
- `qm_res_mgr` 的列表、分类、子分类和详情查询使用 MQTT 元数据通道；
- 资源文件本身仍由返回的 URL 通过 HTTP 下载；
- MQTT 元数据模式不使用 `qm_res_mgr_set_env()`，调用它会返回
  `-QM_ENOSYS`。`DEV/TEST/PROD` 环境切换只适用于 HTTP 元数据模式。

QUMI 播放器和资源管理器的固定容量、队列长度、超时等构建参数放在
`qm_config`/Kconfig；本应用 wrapper 的命令队列、worker 栈和 guide 等待时间仍由
`playback.c` 内部常量控制。DID、资源包名称、资源 ID、MQTT handle 和资源 URL
等运行时数据必须通过 API 参数传入。

## 播放器初始化

播放器由 `playback_init()` 创建：

```c
int playback_init(qm_audio_handle_t audio,
                  const qm_player_decoder_if_t *decoder_if,
                  const qm_player_source_if_t *source_if);
```

参数要求：

| 参数 | 要求 |
| --- | --- |
| `audio` | 必须是已经创建好的共享 `qm_audio` handle，当前工程可由 `voice_get_audio()` 获取 |
| `decoder_if` | 必须提供，负责压缩音频解码和 PCM frame 输出 |
| `source_if` | 播放 `QM_PLAYER_LOCATION_FILE` 时必须提供；只播放 URI 时可以为 `NULL` |

该函数创建 `qm_player`，并创建一个用于处理应用层 `player/play` 请求的队列和
worker；它不创建 `qm_audio`，也不创建解码器。调用方必须保证 `audio`、
`decoder_if` 和 `source_if` 在 `playback_deinit()` 返回前一直有效。

`qm_player_decoder_if_t` 的 `open()` 会收到一个带有 `read()`、输出格式和
frame sink 的解码会话；解码器应把 PCM frame 推送给 `frame_sink.push()`。
解码器停止时必须中断自己的读取和任务，`close()` 负责释放解码器会话。

当前仓库里的 `apps/main/agent/private/opus.c` 是 Agora 语音帧的 Opus 编解码
辅助代码，不是 `qm_player_decoder_if_t` 实现，不能直接作为
`playback_init()` 的第二个参数。

当前资源 PCM 解码器通过以下接口注入：

```c
#include "resource_pcm_decoder.h"

const qm_player_decoder_if_t *decoder = resource_pcm_decoder_get_if();
int ret = playback_init(voice_get_audio(), decoder, NULL);
```

该解码器接收 16 kHz、单声道、16-bit 原始 PCM，按 20 ms（640 个采样、1280
字节）编码为固定 160 字节的 Opus frame，再通过 `frame_sink.push()` 交给
`qm_player`。它不是通用 MP3/WAV/AAC/FLAC 解码器。

## 播放 URL

```c
#include "playback.h"

int ret = playback_play_url("https://example.com/song.pcm",
                            QM_PLAYER_RESOURCE_TYPE_PCM,
                            0U,  /* 文件大小未知 */
                            0U); /* 时长未知 */
```

`playback_play_url()` 只接受以 `http://` 或 `https://` 开头的 URI。类型枚举
虽然保留了 MP3/WAV/AAC/FLAC/PCM，但当前应用层解码链路实际只接受
`QM_PLAYER_RESOURCE_TYPE_PCM`（16 kHz、单声道、16-bit 原始 PCM）。URL 资源使用
`qm_player` 内置的 HTTP source；应用层不需要实现 `qm_player_source_if_t`。
该函数成功只表示请求已经提交给 `qm_player`，不表示 HTTP 已连接、解码已开始
或喇叭已经出声。

## 云控资源播放

由于 `CONFIG_QM_PLAYER_CLOUD_SUPPORT` 保持为 0，`apps/main/cloud/ljyd_method.c`
自行注册 `method=player`、`subMethod=play`。MQTT 回调只解析并复制请求，然后投递
到 playback worker，不在收包回调里等待 HTTP、TTS 或音频播放。

应用层接受的主要参数如下，`fileSize`、`totalDuration` 可以是 JSON 数字，也可以
是数字字符串：

```json
{
  "method": "player",
  "subMethod": "play",
  "params": {
    "url": "https://example.com/song.pcm",
    "audioType": "pcm",
    "guideWords": "收到，马上播放",
    "title": "示例",
    "fileSize": "2333884",
    "totalDuration": "72"
  }
}
```

应用层等价的入队接口是：

```c
playback_cloud_play_request_t request = {
    .guide_words = "收到，马上播放",
    .url = "https://example.com/song.pcm",
    .audio_type = "pcm",
    .title = "示例",
    .file_size = 0U,
    .duration_s = 0U,
};
int ret = playback_submit_cloud_play(&request);
```

该接口会复制字符串，调用返回后调用方可以释放或复用原字符串。它只表示请求
进入应用层队列；队列满时返回 `-QM_EBUSY`，非 PCM 类型返回 `-QM_ENOSYS`。

一次云控播放的应用层顺序是：

```text
MQTT player/play
  -> 复制 guideWords、url、audioType 和元数据
  -> 停止上一条资源/语音输出
  -> HTTP sendSpeakRequest 播放 guideWords（若有）
  -> 等待 QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE
  -> 暂停采集并关闭 agent 上行
  -> HTTP 下载 PCM URL
  -> 应用层 PCM -> Opus 解码器 -> qm_player -> qm_audio
  -> 播放结束/停止/抢占/错误后恢复采集
```

`guideWords` 使用当前 agent 的缓存鉴权信息发送，`Authorization` 使用服务端要求
的原始 token。guide TTS 完成后才会提交资源播放；guide 请求失败或 TTS 等待超时会
放弃本次资源播放并恢复音频路径。资源下载、解码和输出错误通过播放器终态事件
报告。唤醒词会先停止资源播放器；如果正在进行语音
对话或 guide TTS，应用层也会清空本地输出并停止上行。Agora transport 是否支持
远端中断由其后端决定，不影响本地音频抢占。

## 播放本地文件

```c
int ret = playback_play_file("/data/song.pcm",
                             QM_PLAYER_RESOURCE_TYPE_PCM,
                             file_size,
                             duration_s);
```

本地文件需要在 `playback_init()` 时传入应用自己的 `qm_player_source_if_t`。
source 适配器只负责 `open/read/close`，缓冲、解码、PCM 输出和音频抢占仍由
`qm_player`、解码器和 `qm_audio` 负责。

## 播放控制和状态

```c
int playback_pause(void);
int playback_resume(void);
int playback_stop(void);
int playback_get_status(qm_player_status_t *status);
int playback_deinit(void);
```

`playback.c` 保存最近一次成功提交的 operation ID，并用它执行暂停、恢复和
停止。控制请求会提交给 `qm_player` 的 worker：

- `playback_pause()` 保留 source、解码器会话和已经缓存的 PCM；
- `playback_resume()` 恢复同一个 operation；
- `playback_stop()` 请求停止当前 operation；
- `playback_deinit()` 销毁 player，调用方仍负责管理共享 `qm_audio`；
- 没有播放器或没有当前 operation 时，暂停、恢复和停止会返回
  `-QM_ENOENT`。

`playback_get_status()` 返回当前状态、operation ID 和毫秒级位置；常见状态是
`STOPPED`、`BUFFERING`、`PLAYING`、`PAUSED` 和 `ERROR`。自然结束、停止、被抢占
或错误会通过事件回调记录为 `QM_PLAYER_EVENT_FINISHED`、`STOPPED`、`PREEMPTED`
或 `ERROR`，资源播放组件在这些终态事件中恢复采集和 agent 上行。

播放请求使用当前实现固定的音频仲裁策略：低优先级、队列策略、暂停采集、
可靠投递，申请音频 lease 最多等待 3000 ms。播放器事件由
`playback.c` 内部记录为：

```text
event=<type> operation=<id> error=<error> position=<ms>
```

请求被接受、开始播放、暂停、恢复、自然结束、停止、被抢占和错误是不同的
阶段，不能把 `playback_play_*()` 的返回值当成播放完成。

## 查询资源元数据

当前工程编译的是 MQTT 元数据模式，调用方需要构造：

```c
playback_metadata_t metadata = {
    .did = device_did,
    .iot_handle = ljyd_cloud_get_handle(),
};
```

`did` 和 `iot_handle` 必须在每次调用时有效。元数据查询是同步调用，应该在
应用 worker 中执行，不能在 MQTT 收包回调里等待资源服务回复。

### 打印第一页

```c
int ret = playback_print_first_page(&metadata, "resource-bundle-name");
```

该函数调用 `qm_res_mgr_get_list()`，请求第 1 页、每页最多 10 条资源，并在
内部打印每条资源的 ID、标题、业务类型和 `audioType`。示例为了只读取一页，
页回调返回 `-QM_EINTR`；如果该页业务码为 0，包装函数会把这个中断转换为
`QM_EOK`，最后释放列表内存。

该接口不会把列表返回给调用方。需要完整列表或自定义分页处理时，应直接
使用 `qm_res_mgr_get_list()`，并在结束后调用：

```c
qm_res_mgr_list_free(&list);
```

### 查询分类

```c
qm_res_mgr_category_list_t categories;
int ret = playback_get_categories(&metadata, &categories);
if (ret == QM_EOK) {
    /* 使用 categories.items[0..categories.count) */
}
qm_res_mgr_category_list_free(&categories);
```

查询子分类：

```c
qm_res_mgr_child_category_list_t children;
int ret = playback_get_child_categories(&metadata,
                                        category_id,
                                        "resource-bundle-name",
                                        &children);
if (ret == QM_EOK) {
    /* 使用 children.items[0..children.count) */
}
qm_res_mgr_child_category_list_free(&children);
```

分类结果及其中的字符串由 `qm_res_mgr` 动态分配，必须使用对应的释放 API。

### 根据资源 ID 播放

```c
int ret = playback_play_id(&metadata, resource_id);
```

该函数的实际顺序是：

```text
MQTT getResourceDetail
        -> 读取 detail.url、audioType、fileSize、totalDuration
        -> 映射为 qm_player_resource_t
        -> qm_player_play()
        -> 释放 detail 返回的动态字段
```

`audioType` 当前仍可映射为 `mp3`、`wav`、`aac`、`flac` 和 `pcm`，但本应用层
提交播放时只接受 `pcm`；其他类型会返回 `-QM_ENOSYS`，等待对应解码器实现。
`qm_player_play()` 会复制播放资源的字符串，因此 `playback_play_id()` 在提交
后可以安全释放 `qm_res_mgr_get_detail()` 返回的 detail。

## 直接下载资源

```c
int playback_download(const qm_res_mgr_resource_t *resource,
                      qm_res_mgr_download_data_cb_t on_data,
                      void *user,
                      qm_res_mgr_download_result_t *result);
```

该接口是同步下载，不会自动写文件，也不会自动提交给 `qm_player`。每收到一
段数据就调用一次 `on_data`：

```text
on_data(resource, data, len, offset, total_size, user)
```

应用层可以在回调里写文件、做缓存或送入自己的处理管线。回调返回
`QM_EOK` 继续，返回负的 `QM_E*` 会终止下载；正值会被资源管理器转成
`-QM_EIO`。下载结束后由应用检查 `qm_res_mgr_download_result_t`，并自行处理
临时文件、校验和缓存策略。

资源列表和详情中的动态字符串分别由以下 API 释放：

```c
qm_res_mgr_list_free(&list);
qm_res_mgr_resource_free(&detail);
```

## 唤醒打断

当资源播放开关打开时，当前 `apps/main/agent/agent_app.c` 的唤醒回调顺序是：

```text
唤醒词
  -> playback_stop()
  -> qm_agent_interrupt()
```

因此，资源播放器已经通过 `playback_init()` 创建并有活动 operation 时，唤醒
会先请求停止资源播放；如果同时存在云端对话，应用还会清空本地 TTS 队列、停止
当前共享输出并关闭上行。资源播放结束或终态事件到达后，组件才恢复采集策略和
agent 上行。关闭资源播放开关时，唤醒流程仍可打断语音对话，但不会调用
`playback_stop()`。

当前 Agora transport 的后端中断发送接口仍返回 `-QM_ENOSYS`；这表示本地输出会
停止，但不能据此保证云端对话状态已经被远端取消。若需要远端同步打断，必须另外
接入云端 HTTP 打断接口。

## 云控边界

当前配置关闭了：

```c
#define CONFIG_QM_PLAYER_CLOUD_SUPPORT 0
```

所以本工程不会由 QUMI 自动注册 `method=player`、`playerReply` 处理器；应用层已经
完成 `player/play` 接收和异步投递。业务回复仍由现有 MQTT method 分发器统一处理。
如果扩展其他 `player` 子方法，继续在应用层增加表项和 worker 命令即可：

```text
MQTT 收包
  -> 应用层解析并投递到 worker
  -> 查询资源或读取 URL
  -> playback_play_id() / playback_play_url()
  -> 应用层发送业务回复
```

不能因为 `qm_player_cloud.c` 被加入编译列表，就认为云控已经接通；真正的
开关由 `CONFIG_QM_PLAYER_CLOUD_SUPPORT` 决定。

## 生命周期和线程要求

- `playback_init()` 成功后，保持传入的 audio、decoder 和 source 接口有效；
- `playback_play_url()`、`playback_play_file()`、暂停、恢复和停止只提交播放器
  命令；`playback_submit_cloud_play()` 先提交到应用层 worker，实际状态通过事件
  或 `playback_get_status()` 观察；
- `qm_res_mgr` 的列表、分类、详情和下载接口都是同步接口，应在应用 worker
  中执行；
- 列表、分类和详情返回的动态内存必须使用 QUMI 提供的释放函数；
- 先停止并销毁 `playback`，再销毁它依赖的共享 `qm_audio`；
- 不要让多个任务并发切换资源环境；当前 MQTT 模式本身不支持运行时环境切换。

## 常见返回值和日志

| 返回值 | 含义 |
| --- | --- |
| `QM_EOK` | 请求已接受或同步查询成功 |
| `-QM_EINVAL` | 参数为空、资源 ID 无效、URL 格式错误或元数据不完整 |
| `-QM_EINIT` | 播放器尚未通过 `playback_init()` 创建 |
| `-QM_ENOENT` | 没有可暂停、恢复或停止的当前 operation |
| `-QM_ENOSYS` | 当前构建不支持该功能，例如 MQTT 模式调用资源环境切换 |
| `-QM_EBUSY` | 应用层云控队列已满，或播放器当前无法接受该请求 |
| `-QM_ENOMEM` | 创建播放器、命令队列、worker 或复制云控参数时内存不足 |
| `-QM_ETIMEOUT` | 同步资源操作或 `playback_deinit()` 等待内部 worker 超时 |
| `-QM_EIO` | 同步资源服务/下载调用失败；异步下载或解码错误应通过播放器事件观察 |

排查时应先区分：

1. 是否调用过 `playback_init()`，以及 decoder/source 是否真实有效；
2. `playback_play_*()` 是否已经提交成功，后续是否有播放器事件；云控请求还要检查
   `playback_submit_cloud_play()` 是否因队列满返回 `-QM_EBUSY`；
3. 元数据查询是否使用了有效的 DID 和 `ljyd_cloud_get_handle()`；
4. 资源 URL 是否可访问、`audioType` 是否有对应解码器；
5. 日志是否来自 `qm_player` 资源链路，还是来自 `qm_agent` 的语音下行链路。
