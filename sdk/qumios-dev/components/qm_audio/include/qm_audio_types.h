/**
 * @file qm_audio_types.h
 * @brief Shared media, playback, event, and capture type contracts. / 共享媒体、播放、事件与采集类型契约。
 *
 * This contract is shared by the four module implementations; it contains no
 * platform handles or product/cloud payloads. PCM is interleaved, signed
 * two's-complement, and little-endian. A PCM sample frame contains one sample
 * from every channel at the same sampling instant; a delivery block may
 * contain many sample frames. / 本契约由四个模块实现共享，不含平台句柄或产品/云
 * 负载。PCM 为交错、有符号二进制补码、小端格式；一个采样帧包含同一采样时刻每个
 * 声道的一个采样，一个交付块可包含多个采样帧。
 */
#ifndef QM_AUDIO_TYPES_H
#define QM_AUDIO_TYPES_H

#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identifier of one playback request. / 单个播放请求的标识符。
 *
 * It identifies one Control operation while callbacks or release can observe
 * it. This is not a cloud protocol ID. / 在回调或释放可观察期间标识一个 Control
 * 操作；不是云协议 ID。
 */
typedef uint32_t qm_audio_request_id_t;

/** @brief Sentinel for no associated request. / 无关联请求的哨兵值。 */
#define QM_AUDIO_INVALID_REQUEST_ID ((qm_audio_request_id_t)0)
/** @brief Sentinel for no associated execution generation. / 无关联执行代次的哨兵值。 */
#define QM_AUDIO_INVALID_GENERATION ((uint32_t)0)

/** @brief Opaque capture subscription released through unsubscribe. / 通过退订释放的不透明采集订阅句柄。 */
typedef struct qm_audio_capture_subscription qm_audio_capture_subscription_t;
/** @brief Opaque Control handle released through its lifecycle API. / 通过 Control 生命周期 API 释放的不透明句柄。 */
typedef struct qm_audio_control qm_audio_control_t;
/** @brief Opaque event subscription returned by qm_audio_control_subscribe(). / qm_audio_control_subscribe() 返回的不透明事件订阅句柄。 */
typedef struct qm_audio_subscription *qm_audio_subscription_t;

/** @brief Locator kind selected by a Pipeline source. / Pipeline 输入源选择的定位类型。 */
typedef enum {
    /** @brief Borrow caller-owned immutable bytes. / 借用调用方拥有的不可变字节。 */
    QM_AUDIO_SOURCE_MEMORY = 0,
    /** @brief Open a local file through qm_fs. / 通过 qm_fs 打开本地文件路径。 */
    QM_AUDIO_SOURCE_FILE,
    /** @brief Open a URL through the built-in HTTP source. / 通过内置 HTTP 源打开网络 URL。 */
    QM_AUDIO_SOURCE_URI,
    /** @brief Receive a bounded stream through Control stream writes. / 通过 Control 流写入接收有界字节流。 */
    QM_AUDIO_SOURCE_PUSH,
    /** @brief Use caller-provided task-context I/O callbacks. / 使用调用方提供的任务上下文 I/O 回调。 */
    QM_AUDIO_SOURCE_CUSTOM,
} qm_audio_source_type_t;

/**
 * @brief Custom source callback types. / 自定义输入源回调类型。
 *
 * open/read/initial close run serially on the source worker. After execution
 * exits, the lifecycle owner may retry a failed close. cancel runs on its
 * requesting task and may overlap source I/O. The caller owns ctx until
 * release succeeds. / open/read/首次 close 由输入源任务串行调用；执行退出后，
 * 生命周期所有者可重试失败的 close。cancel 在请求任务上执行，可能与源 I/O 并发；
 * release 成功前 ctx 归调用方持有。
 */
/**
 * @brief Open a custom input source. / 打开自定义输入源。
 * @param[in] ctx Caller-owned source context. / 调用方拥有的输入源上下文。
 * @param[in] timeout_ms Finite open budget, including connection setup. / 有限的打开等待预算，含建连。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 * @note A failed open unwinds its resources. A latched cancel must return
 * -QM_EINTR even if cancellation arrived before open. / 打开失败须回收本次资源；
 * 已锁存取消时返回 -QM_EINTR，即使取消早于 open 到达。
 */
typedef int (*qm_audio_source_open_fn)(void *ctx, uint32_t timeout_ms);
/**
 * @brief Read bytes from a custom input source. / 从自定义输入源读取字节。
 * @param[in] ctx Caller-owned source context. / 调用方拥有的输入源上下文。
 * @param[out] data Destination buffer. / 目标缓冲区。
 * @param[in] capacity Destination capacity in bytes. / 目标容量（字节）。
 * @param[in] timeout_ms Read wait budget in milliseconds. / 读取等待时长（毫秒）。
 * @param[out] read_size Bytes read on return. / 返回时已读取的字节数。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
typedef int (*qm_audio_source_read_fn)(void *ctx, void *data, uint32_t capacity, uint32_t timeout_ms, uint32_t *read_size);
/**
 * @brief Latch cancellation and wake blocked source I/O. / 锁存取消并唤醒阻塞的源 I/O。
 * @param[in] ctx Caller-owned context, valid before open and after close. / 调用方上下文，open 前和 close 后均须有效。
 * @note Required, thread-safe, nonblocking and idempotent. Never closes or
 * frees ctx. open/read must observe the latch and return -QM_EINTR. Reset only
 * before submitting a new operation after the previous operation is released.
 * / 必填、线程安全、不阻塞且幂等；不得关闭或释放 ctx。open/read 须检查锁存并返回
 * -QM_EINTR；仅可在旧操作释放后、提交新操作前复位。
 */
typedef void (*qm_audio_source_cancel_fn)(void *ctx);
/**
 * @brief Close a custom input source. / 关闭自定义输入源。
 * @param[in] ctx Caller-owned source context. / 调用方拥有的输入源上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 * @note Must have a finite bound. Failure retains resources for retry.
 * / 必须在有限时间内返回；失败时保留资源以便重试。
 */
typedef int (*qm_audio_source_close_fn)(void *ctx);

/**
 * @brief Input source for one playback operation. / 单个播放操作的输入源。
 *
 * File paths and URLs are copied. Memory and custom-source contexts remain
 * owned by the caller. / 文件路径和 URL 会被复制；内存与自定义源上下文仍归调用方。
 */
typedef struct {
    /** @brief Active member of `value`. / `value` 中的有效成员。 */
    qm_audio_source_type_t type;
    union {
        /** @brief Borrowed immutable memory buffer. / 借用的不可变内存缓冲区。 */
        struct { const void *data; uint32_t size; } memory;
        /** @brief Copied local file path. / 会被复制的本地文件路径。 */
        struct { const char *path; } file;
        /** @brief Copied HTTP resource URL. / 会被复制的 HTTP 资源 URL。 */
        struct {
            const char *url;
            /* Full identity-response size from metadata; zero is unknown. */
            uint32_t expected_size;
        } uri;
        /** @brief PUSH source context and ring capacity. / PUSH 输入源上下文与环形缓冲容量。 */
        struct { void *context; uint32_t buffer_bytes; } push;
        /** @brief Caller-owned custom source callbacks and context. / 调用方拥有的自定义源回调与上下文。 */
        struct {
            void *ctx;
            qm_audio_source_open_fn open;
            qm_audio_source_read_fn read;
            qm_audio_source_cancel_fn cancel;
            qm_audio_source_close_fn close;
        } custom;
    } value;
} qm_audio_source_t;

/**
 * @brief Codec resume-state capacity of qm_audio_resume_point_t. / qm_audio_resume_point_t 的编解码续播状态容量。
 *
 * The platform decoder's largest per-codec breakpoint is the APE container at
 * 2036 bytes; 2048 keeps a small margin. A breakpoint larger than this is
 * rejected rather than truncated, so the caller falls back to a full restart.
 * / 平台解码器最大的单编解码断点是 APE 容器的 2036 字节，2048 留有余量。超过该值的
 * 断点会被拒绝而非截断，使调用方回退为完整重启。
 */
#ifndef QM_AUDIO_RESUME_STATE_BYTES
#define QM_AUDIO_RESUME_STATE_BYTES 2048U
#endif

/**
 * @brief Source bytes fetched before a recorded compressed breakpoint. / 记录压缩断点之前额外抓取的源字节数。
 *
 * A decoder resuming a recorded breakpoint may still re-read a short span of
 * container context (frame headers, tags). The rebuilt compressed source starts
 * this many bytes before the breakpoint and the decoder clamps its virtual read
 * cursor floor to the same point, so both sides stay aligned. Raw PCM records
 * carry an exact consumed-byte offset and never apply this backoff.
 * / 解码器恢复记录的断点时仍可能回读一段容器上下文（帧头、标签）。重建的压缩源从断点
 * 前这些字节处开始，平台解码器也把其虚拟读游标下界钳到同一点，两侧保持对齐。裸 PCM
 * 记录携带的是精确已消费字节偏移，绝不应用此回退。
 */
#ifndef QM_AUDIO_RESUME_BACKOFF_BYTES
#define QM_AUDIO_RESUME_BACKOFF_BYTES 4096U
#endif

/**
 * @brief Bounded decode-resume record for reopening a remote stream. / 重开远端流的有界解码续播记录。
 * The record anchors a rebuilt remote source at a byte offset. For a compressed
 * stream the offset is the decoder read cursor together with codec state; for
 * raw PCM it is the plain consumed-byte offset and no state is carried. offset
 * zero means start at the stream head, and the new source starts exactly at the
 * offset (no leading backoff). A record produced by a decoder capture carries
 * codec state; a provider that cannot supply usable state reports failure so the
 * caller falls back to a full restart. The value is a plain copied record: it
 * owns no heap storage and every layer copies it by value, so no record outlives
 * the caller that supplied it.
 * / 记录把重建的远端源锚定到某个字节偏移。压缩流的偏移是解码器读游标并携带编解码状态；
 * 裸 PCM 的偏移则是已消费的纯字节偏移，不携带状态。offset 为零表示从流头开始，新源
 * 精确从该偏移开始（不向前多抓）。由解码器捕获产生的记录携带编解码状态；无法提供可用
 * 状态时提供方直接报错，由调用方回退为完整重启。该值是纯拷贝记录，不持有堆存储，各层
 * 按值拷贝，因此记录不会比提供它的调用方存活更久。
 */
typedef struct {
    /** @brief Stream byte offset to resume from; zero starts at the stream head. / 续播起始字节偏移；零表示从流头开始。 */
    uint32_t offset;
    /** @brief Media position in milliseconds at offset; zero means unknown. / 偏移处对应的媒体位置（毫秒）；零表示未知。 */
    uint32_t position_ms;
    /** @brief Codec state byte count; zero means the record carries no state. / 编解码状态字节数；零表示该记录不携带状态。 */
    uint32_t state_size;
    /** @brief Codec state bytes; only the first state_size bytes are meaningful. / 编解码状态字节；仅前 state_size 字节有效。 */
    uint8_t state[QM_AUDIO_RESUME_STATE_BYTES];
} qm_audio_resume_point_t;

/** @brief Storage representation of one interleaved signed PCM sample. / 一个交错有符号 PCM 采样的存储格式。 */
typedef enum {
    /** @brief Signed 16-bit little-endian sample. / 有符号 16 位小端采样。 */
    QM_AUDIO_PCM_S16 = 0,
    /** @brief Signed packed 24-bit little-endian sample. / 有符号紧凑 24 位小端采样。 */
    QM_AUDIO_PCM_S24,
    /** @brief Signed 32-bit little-endian sample. / 有符号 32 位小端采样。 */
    QM_AUDIO_PCM_S32,
} qm_audio_pcm_sample_t;

/**
 * @brief Fixed PCM stream format. / 固定 PCM 流格式。
 *
 * One sample frame occupies channels multiplied by the sample storage size.
 * The format does not describe planar PCM, floating-point PCM, or channel
 * layout labels. Channel order must agree between producer and consumer; do
 * not infer multichannel speaker positions from the channel count alone.
 * / 一个采样帧占用声道数乘以采样存储大小。本格式不描述 planar PCM、浮点 PCM 或
 * 声道布局标签。声道顺序须在生产与消费双方一致；不要仅凭声道数推断多声道扬声器
 * 位置。
 */
typedef struct {
    /** @brief Sample frames per second; nonzero for an active stream. / 每秒采样帧数；活跃流必须非零。 */
    uint32_t sample_rate_hz;
    /** @brief Interleaved samples per frame; must be nonzero. / 每帧交错采样数；必须非零。 */
    uint16_t channels;
    /** @brief Signed sample storage representation. / 有符号采样存储格式。 */
    qm_audio_pcm_sample_t sample;
} qm_audio_pcm_format_t;

/**
 * @brief Shared borrowed audio data or ordered stream marker. / 共享的借用音频数据或有序流标记。
 *
 * The receiving interface and its fixed stream format select a PCM block,
 * encoded byte chunk, or one complete encoded packet. PCM sizes align to
 * complete sample frames; byte-chunk boundaries have no codec-frame meaning.
 * / 接收接口及其固定流格式决定 PCM 块、编码字节块或一个完整编码包；PCM 按完整
 * 采样帧对齐，编码字节块边界不表示编解码帧边界。
 *
 * This descriptor never transfers ownership. Each interface specifies the
 * borrowed data lifetime; retaining bytes beyond it requires a copy. An
 * interface may reject metadata or END_OF_STREAM even though this type can
 * represent them. Unused metadata fields are zero; undefined flag bits are
 * invalid. / 本描述符不转移所有权，借用期限由各接口规定，超期保留必须复制。
 * 接口可拒绝元数据或 END_OF_STREAM；未使用的元数据字段置零，未定义标志位无效。
 *
 * Write validity belongs to the receiving session or stream, not to this
 * descriptor. / 写入权限由接收会话或流管理，不由数据描述符授予。
 */
typedef struct {
    /** @brief Payload bytes; non-NULL when size is nonzero. / 数据字节；大小非零时不能为空。 */
    const void *data;
    /** @brief Valid byte count; zero is allowed only by APIs accepting marker-only blocks. / 有效字节数；仅允许纯标记块的接口可接收零长度。 */
    uint32_t size;
    /**
     * @brief Timestamp in milliseconds, valid only with TIMESTAMP_VALID.
     * / 毫秒时间戳，仅在 TIMESTAMP_VALID 置位时有效。
     * @details Zero is a valid origin. Wraps modulo uint32_t; elapsed-time
     * subtraction assumes less than one full wrap. Clock origin is defined by
     * the producing interface; capture time is not automatically playback
     * presentation time. / 零是有效起点，按 uint32_t 回绕；无符号差值计算要求
     * 间隔小于一次完整回绕。时钟起点由生产接口定义，采集时间不自动等同播放呈现
     * 时间。
     */
    uint32_t timestamp_ms;
    /** @brief Media duration in milliseconds when DURATION_VALID is set. / DURATION_VALID 置位时的媒体时长（毫秒）。 */
    uint32_t duration_ms;
    /** @brief Producer sequence modulo uint32_t; capture gaps identify missing blocks. / 按 uint32_t 回绕的生产者序号；采集序号间隙表示丢块。 */
    uint32_t sequence;
    /** @brief Bitwise QM_AUDIO_DATA_* flags. / QM_AUDIO_DATA_* 标志的按位组合。 */
    uint32_t flags;
} qm_audio_data_t;

/** @brief Shared media validity and stream-marker flags. / 共享媒体有效性及流标记标志。 */
enum {
    /** @brief No valid optional metadata or stream marker. / 无有效可选元数据或流标记。 */
    QM_AUDIO_DATA_NONE = 0U,
    /** @brief timestamp_ms is valid, including zero. / timestamp_ms 有效，包含零值。 */
    QM_AUDIO_DATA_TIMESTAMP_VALID = (1U << 0),
    /** @brief duration_ms is valid. / duration_ms 有效。 */
    QM_AUDIO_DATA_DURATION_VALID = (1U << 1),
    /** @brief Continuity breaks before this unit, or a new source epoch begins. / 本单元前发生断续或开始新的源时期。 */
    QM_AUDIO_DATA_DISCONTINUITY = (1U << 2),
    /**
     * @brief Input ends after this block; valid only for APIs accepting EOS.
     * Does not prove device drain or playback completion. / 本块之后输入结束，
     * 仅适用于接收 EOS 的接口；不表示设备排空或播放完成。
     */
    QM_AUDIO_DATA_END_OF_STREAM = (1U << 3),
};

/** @brief Relative Service admission priority, not an OS priority. / 相对 Service 准入优先级，不是操作系统任务优先级。 */
typedef enum {
    /** @brief Lowest scheduling priority. / 最低调度优先级。 */
    QM_AUDIO_PRIORITY_LOW = 0,
    /** @brief Ordinary scheduling priority. / 普通调度优先级。 */
    QM_AUDIO_PRIORITY_NORMAL,
    /** @brief Elevated scheduling priority. / 较高调度优先级。 */
    QM_AUDIO_PRIORITY_HIGH,
    /** @brief Highest priority; does not bypass safe device stop. / 最高优先级；不会绕过安全设备停止流程。 */
    QM_AUDIO_PRIORITY_CRITICAL,
} qm_audio_priority_t;

/** @brief Requested behavior while the Service output route is occupied. / Service 输出路由被占用时请求的行为。 */
typedef enum {
    /** @brief Wait in the bounded Service queue. / 在有界 Service 队列中等待。 */
    QM_AUDIO_ADMISSION_QUEUE = 0,
    /** @brief Reject when the output route is occupied. / 输出路由被占用时拒绝请求。 */
    QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
    /** @brief Preempt only a lower-priority operation. / 仅抢占更低优先级的操作。 */
    QM_AUDIO_ADMISSION_PREEMPT_LOWER,
    /**
     * @brief Request preemption irrespective of relative priority, subject to
     * Service policy and confirmed release of the current output owner.
     * / 无视相对优先级请求抢占，受 Service 策略及当前输出所有者确认释放的约束。
     */
    QM_AUDIO_ADMISSION_PREEMPT_CURRENT,
} qm_audio_admission_policy_t;

/** @brief Encoded input accepted by qm_audio_control_play(). / qm_audio_control_play() 接受的编码输入类型。 */
typedef enum {
    /** @brief Raw PCM in the output format. / 与输出格式一致的原始 PCM。 */
    QM_AUDIO_CODEC_PCM = 0,
    /** @brief MPEG Layer III stream. / MPEG Layer III 音频流。 */
    QM_AUDIO_CODEC_MP3,
    /** @brief Opus stream. / Opus 音频流。 */
    QM_AUDIO_CODEC_OPUS,
    /** @brief G.711 A-law stream. / G.711 A 律音频流。 */
    QM_AUDIO_CODEC_G711A,
    /** @brief G.711 mu-law stream. / G.711 mu 律音频流。 */
    QM_AUDIO_CODEC_G711U,
    /** @brief G.722 stream. / G.722 音频流。 */
    QM_AUDIO_CODEC_G722,
    /** @brief Ask the platform decoder to identify unknown encoding. / 请求平台解码器识别未知编码。 */
    QM_AUDIO_CODEC_AUTO,
    /** @brief Product-assigned opaque codecs start here; passthrough needs no codec backend.
     * / 私有编码标识起点；原样透传不需要编解码实现。 */
    QM_AUDIO_CODEC_PRIVATE_BASE = 0x10000,
} qm_audio_codec_t;

/** @brief Source framing, independent of codec. / 输入封装，与编码类型独立。 */
typedef enum {
    QM_AUDIO_CODEC_FRAMING_STREAM = 0,
    QM_AUDIO_CODEC_FRAMING_OPUS_OGG,
    QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER,
    QM_AUDIO_CODEC_FRAMING_OPUS_CBR,
    /** Complete units submitted through packet write; never serialized wire headers. */
    QM_AUDIO_CODEC_FRAMING_PACKET,
} qm_audio_codec_framing_t;

/**
 * @brief Fixed device media format. / 固定设备媒体格式。
 *
 * pcm describes actual samples for PCM and decoded geometry for a codec.
 * Encoded payload bytes are never PCM samples. For encoded media,
 * max_packet_bytes > 0 selects complete packets; zero selects a byte stream.
 * PCM geometry may be all zero for opaque encoded passthrough. All-zero PCM
 * disables a direction. Formats remain fixed until the device is closed.
 * / pcm 描述 PCM 的实际采样和编码解码后的几何。编码负载字节绝不是 PCM 采样。
 * 编码媒体下 max_packet_bytes > 0 选择完整包，零值选择字节流。透明编码透传的
 * PCM 几何可为全零；全零 PCM 禁用该方向。格式在设备关闭前保持固定。
 */
typedef struct {
    qm_audio_pcm_format_t pcm;
    qm_audio_codec_t codec;
    /** @brief Encoded packet duration; zero for PCM. / 编码包时长，PCM 为零。 */
    uint32_t packet_duration_ms;
    /** @brief Fixed CBR bytes, or zero for variable packets. / CBR 定长字节数，变长为零。 */
    uint32_t packet_bytes;
    /** @brief Complete packet limit; zero for PCM or encoded byte streams.
     * / 完整包上限；PCM 和编码字节流为零。 */
    uint32_t max_packet_bytes;
    /** @brief Byte-stream framing; ignored for packet delivery (use STREAM/zero).
     * / 字节流封装；完整包交付时保持 STREAM/零。 */
    qm_audio_codec_framing_t framing;
} qm_audio_stream_format_t;

/**
 * @brief Capabilities of an initialized audio route. / 已初始化音频路由的能力。
 *
 * This application-facing projection describes the fixed route selected by
 * qm_audio_init(). It does not list every codec provider or imply that capture
 * and playback are supported simultaneously. / 该面向应用的投影描述 qm_audio_init()
 * 选择的固定路由；不列出每个 codec provider，也不表示采集与播放一定支持同时运行。
 */
typedef struct {
    /** @brief Actual capture stream format; zero when disabled. / 实际采集流格式；禁用时为全零。 */
    qm_audio_stream_format_t input_format;
    /** @brief Actual playback stream format; zero when disabled. / 实际播放流格式；禁用时为全零。 */
    qm_audio_stream_format_t output_format;
    /** @brief Bitwise QM_AUDIO_CAP_* values. / QM_AUDIO_CAP_* 的按位组合。 */
    uint32_t flags;
} qm_audio_caps_t;

/** @brief Capture path is available. / 可用采集路径。 */
#define QM_AUDIO_CAP_CAPTURE (1U << 0)
/** @brief Playback path is available. / 可用播放路径。 */
#define QM_AUDIO_CAP_PLAYBACK (1U << 1)
/** @brief Shared output volume is supported. / 支持共享播放音量。 */
#define QM_AUDIO_CAP_VOLUME (1U << 2)
/** @brief Shared output mute is supported. / 支持共享播放静音。 */
#define QM_AUDIO_CAP_MUTE (1U << 3)
/** @brief Playback drain can confirm physical output completion. / 播放 drain 可确认物理输出完成。 */
#define QM_AUDIO_CAP_DRAIN (1U << 4)
/** @brief Capture I/O can be aborted. / 支持中止采集 I/O。 */
#define QM_AUDIO_CAP_ABORT_CAPTURE (1U << 5)
/** @brief Playback I/O can be aborted. / 支持中止播放 I/O。 */
#define QM_AUDIO_CAP_ABORT_PLAYBACK (1U << 6)
/** @brief Playback position can be queried. / 支持查询播放位置。 */
#define QM_AUDIO_CAP_POSITION (1U << 7)
/** @brief Stop drains accepted media instead of discarding it. / 停止会排空已接收媒体而非丢弃。 */
#define QM_AUDIO_CAP_STOP_DRAINS (1U << 8)

/**
 * @brief Decoder input details. / 解码器输入详情。
 *
 * A zero value is raw PCM in the audio output format. AUTO uses the platform
 * decoder's probe path and only accepts STREAM framing. / 全零表示音频输出格式的
 * 原始 PCM。AUTO 使用平台解码器的探测路径，且仅接受 STREAM 封装。
 */
typedef struct {
    /** @brief Input codec. / 输入编解码类型。 */
    qm_audio_codec_t codec;
    /** @brief Encoded input framing. / 编码输入封包格式。 */
    qm_audio_codec_framing_t framing;
    /** @brief Required size for Opus CBR or a fixed native PACKET profile. / Opus CBR 或固定原生 PACKET profile 所需的包大小。 */
    uint32_t packet_bytes;
    /** @brief Optional source geometry for decode/transcode; zero uses target PCM geometry.
     * Direct native routing requires an explicit profile equal to the target.
     * / 解码/转码的可选源格式；全零沿用目标 PCM 参数。原生直通必须显式匹配目标 profile。 */
    qm_audio_pcm_format_t pcm;
    /** @brief Known Opus duration for processed routes, or native PACKET profile duration.
     * Zero permits inspection/variable packets; direct native routing still
     * requires an explicit target-matching value. / 处理路线的已知 Opus 时长，或原生
     * PACKET profile 时长；零表示需解析/可变包，原生直通仍须显式匹配目标值。 */
    uint32_t packet_duration_ms;
} qm_audio_codec_config_t;

/** @brief PCM delivery policy. / PCM 数据交付策略。 */
typedef enum {
    /** @brief Preserve accepted media without intentional drops. / 保留已接收媒体顺序，不主动因延迟丢弃。 */
    QM_AUDIO_DELIVERY_RELIABLE = 0,
    /**
     * @brief Permit documented latency-driven drops; no drop threshold or
     * maximum latency is configured by this enumeration alone.
     * / 允许文档规定的延迟驱动丢弃；仅凭本枚举不配置丢弃阈值或最大延迟。
     */
    QM_AUDIO_DELIVERY_REALTIME,
} qm_audio_delivery_t;

/** @brief 32-bit Control operation ID equal to its Service request ID. / 等同其 Service 请求 ID 的 32 位 Control 操作 ID。 */
typedef uint32_t qm_audio_operation_id_t;

/** @brief Aggregated state exposed by the Control facade. / Control 门面暴露的聚合状态。 */
typedef enum {
    /** @brief Operation slot is allocated but not submitted. / 已分配操作槽位但尚未提交。 */
    QM_AUDIO_CONTROL_NEW = 0,
    /** @brief Service accepted the request and is waiting for output. / Service 已接受请求，正在等待输出路由。 */
    QM_AUDIO_CONTROL_WAITING,
    /** @brief Service owns the route and Pipeline produces negotiated media. / Service 持有输出路由，Pipeline 正在生成协商媒体。 */
    QM_AUDIO_CONTROL_PLAYING,
    /** @brief Both control paths accepted a preserving pause. / 两个控制路径均已确认保留式暂停。 */
    QM_AUDIO_CONTROL_PAUSED,
    /** @brief Pipeline ended while Service output drains. / Pipeline 已结束，Service 输出仍在 drain。 */
    QM_AUDIO_CONTROL_DRAINING,
    /** @brief Physical output drain completed normally. / 物理输出 drain 正常完成。 */
    QM_AUDIO_CONTROL_FINISHED,
    /** @brief Explicit stop or cancel completed. / 显式停止或取消已完成。 */
    QM_AUDIO_CONTROL_CANCELLED,
    /** @brief Another request preempted this operation. / 该操作已被其他请求抢占。 */
    QM_AUDIO_CONTROL_PREEMPTED,
    /** @brief Operation failed and requires release. / 操作失败，需要释放。 */
    QM_AUDIO_CONTROL_ERROR,
} qm_audio_control_state_t;

/**
 * @brief Pause output-route retention mode. / 暂停时的输出路由保留模式。
 *
 * HOLD_OUTPUT retains the output route; YIELD_OUTPUT keeps the media position
 * but releases the route so a higher-priority operation can use it.
 * / HOLD_OUTPUT 保留输出路由；YIELD_OUTPUT 保留媒体位置但释放路由，供更高优先级
 * 操作使用。
 */
typedef enum {
    QM_AUDIO_PAUSE_HOLD_OUTPUT = 0,
    QM_AUDIO_PAUSE_YIELD_OUTPUT,
} qm_audio_pause_mode_t;

/**
 * @brief Shared lifecycle notification type. / 共享生命周期通知类型。
 *
 * Meaning is scoped to the emitting module: Pipeline completion is not proof
 * of physical output completion; use Service or Engine drain confirmation for
 * the latter. An event is not interchangeable with a session-state value.
 * / 语义限定在发出模块：Pipeline 完成不代表物理输出完成，后者须使用 Service 或
 * Engine 的 drain 确认。事件与会话状态值不可互换。
 */
typedef enum {
    /** @brief No event; sentinel rather than progress. / 无事件；哨兵值而非进度通知。 */
    QM_AUDIO_EVENT_NONE = 0,
    /** @brief Request accepted, not necessarily admitted or audible. / 请求已接受，但未必已准入或出声。 */
    QM_AUDIO_EVENT_ACCEPTED,
    /** @brief Emitter started processing, not first sound. / 发出模块已开始处理，不代表首次出声。 */
    QM_AUDIO_EVENT_STARTED,
    /** @brief Emitter confirmed a pause. / 发出模块已确认暂停。 */
    QM_AUDIO_EVENT_PAUSED,
    /** @brief Emitter confirmed resume. / 发出模块已确认恢复。 */
    QM_AUDIO_EVENT_RESUMED,
    /** @brief Normal processing completed at emitter boundary. / 发出模块边界上的正常处理完成。 */
    QM_AUDIO_EVENT_FINISHED,
    /** @brief Cancellation completed at emitter boundary. / 发出模块边界上的取消完成。 */
    QM_AUDIO_EVENT_CANCELLED,
    /** @brief Service confirmed preemption by another request. / Service 已确认该请求被其他请求抢占。 */
    QM_AUDIO_EVENT_PREEMPTED,
    /** @brief Operation failed; result carries a QUMI error. / 操作失败；result 携带 QUMI 错误码。 */
    QM_AUDIO_EVENT_ERROR,
    /** @brief A frontend wake-up was detected. / 检测到前端唤醒。 */
    QM_AUDIO_EVENT_WAKE,
    /** @brief Seek reached the actual media position; position_ms may be zero. / seek 已到达实际媒体位置，position_ms 可以为零。 */
    QM_AUDIO_EVENT_SEEKED,
} qm_audio_event_type_t;

/** @brief Normalized wake-up metadata for QM_AUDIO_EVENT_WAKE. / QM_AUDIO_EVENT_WAKE 携带的标准化唤醒元数据。 */
typedef struct {
    /** @brief Platform-neutral wake source ID. / 平台无关的唤醒源 ID。 */
    uint32_t source;
    /** @brief Platform-neutral wake reason ID. / 平台无关的唤醒原因 ID。 */
    uint32_t reason;
    /** @brief Detected keyword ID, or zero when unavailable. / 检测到的关键词 ID；不可用时为零。 */
    uint32_t keyword_id;
    /** @brief Confidence from 0 to 100, or zero when unavailable. / 置信度范围 0 到 100；不可用时为零。 */
    uint32_t confidence;
} qm_audio_wake_info_t;

/**
 * @brief Normalized event borrowed during its callback. / 仅在回调期间借用的标准化事件。
 *
 * Control supplies operation lifecycle events and frontend wake events through
 * qm_audio_control_subscribe(). Service/Pipeline use the same shape internally
 * but do not expose independent application listener registrations.
 * / Control 通过 qm_audio_control_subscribe() 提供操作生命周期事件与前端唤醒事件；
 * Service/Pipeline 内部使用相同结构，但不暴露独立的应用监听注册。
 */
typedef struct {
    /** @brief Normalized operation or wake notification type. / 标准化操作或唤醒通知类型。 */
    qm_audio_event_type_t type;
    /** @brief Associated operation/request, or invalid ID. / 关联的操作或请求；无关联时为无效 ID。 */
    qm_audio_request_id_t request_id;
    /**
     * @brief Module-owned execution generation for rejecting stale events, or
     * QM_AUDIO_INVALID_GENERATION when unavailable. Not a producer write token.
     * / 用于拒绝过期事件的模块自有执行代次；不可用时为 QM_AUDIO_INVALID_GENERATION。
     * 不是生产者写入令牌。
     */
    uint32_t generation;
    /** @brief QM_EOK or a negative QUMI error. / 成功时为 QM_EOK，失败时为负的 QUMI 错误码。 */
    qm_err_t result;
    /**
     * @brief Milliseconds from the current media run origin at the emitter's
     * observation boundary; zero also denotes unavailable position.
     * / 从当前媒体运行起点到发出模块观测边界的毫秒数；零也表示位置不可用。
     */
    uint32_t position_ms;
    /**
     * @brief Optional module-defined reason; zero means unspecified. No shared
     * reason-code namespace is defined, so consumers must not assume one.
     * / 可选模块定义原因；零表示未指定。未定义共享原因码命名空间，消费者不得
     * 假设其一。
     */
    uint32_t reason;
    /** @brief Control operation ID, or zero for non-operation events. / Control 操作 ID；非操作事件时为零。 */
    qm_audio_operation_id_t operation_id;
    /** @brief Event time in the audio monotonic millisecond domain. / 音频单调毫秒时钟域的事件时间。 */
    uint32_t timestamp_ms;
    /** @brief Wake metadata valid only for wake events. / 仅唤醒事件有效的唤醒元数据。 */
    qm_audio_wake_info_t wake;
} qm_audio_event_t;

/**
 * @brief Receive a lifecycle event from its owner. / 接收其所属模块发出的生命周期事件。
 *
 * @param[in] event Non-NULL borrowed event, valid only until callback return. / 非空的借用事件，仅在回调返回前有效。
 * @param[in] user_ctx Borrowed registration context, possibly NULL; it must
 * remain valid until the owning module has quiesced all callbacks. / 借用的注册
 * 上下文，可为 NULL；在所属模块静止所有回调前必须保持有效。
 *
 * @note The registration API defines its task context and serialization.
 * Callbacks are not ISR handlers. Return promptly and defer blocking I/O,
 * waits, shutdown, destruction, and other blocking lifecycle operations to a
 * caller-owned task. Do not retain event or free the registered context here.
 * / 注册接口定义其任务上下文与串行规则。回调不是 ISR 处理器。须及时返回，将阻塞
 * I/O、等待、关闭、销毁等阻塞生命周期操作推迟到调用方自有任务；此处不得保留
 * 事件或释放注册上下文。
 */
typedef void (*qm_audio_event_cb_t)(const qm_audio_event_t *event, void *user_ctx);

/**
 * @brief Receive a borrowed capture block in callback mode. / 在回调模式中接收借用的采集块。
 *
 * Receive one nonempty PCM block aligned to the Engine input sample frame.
 * timestamp_ms, when valid, uses the source capture clock; END_OF_STREAM is
 * not accepted. / 接收非空且按 Engine 输入采样帧对齐的 PCM 块；有效时间戳使用源
 * 采集时钟，不接收 END_OF_STREAM。
 *
 * @param[in] frame Non-NULL borrowed frame and PCM, valid until callback return.
 * Copy the bytes before returning if another task must retain them. / 非空的借用
 * 帧和 PCM，仅在回调返回前有效；若其他任务需要保留，须在返回前复制字节。
 * @param[in] user_ctx Borrowed subscription context, possibly NULL; keep it
 * valid until capture_unsubscribe has confirmed callback quiescence. / 借用的订阅
 * 上下文，可为 NULL；保持有效直到 capture_unsubscribe 确认回调静止。
 *
 * @note Invoked on the shared audio_event task, not an ISR. Return promptly; do
 * not perform blocking I/O, receive/wait, unsubscribe, shutdown, or destruction
 * here. Schedule lifecycle changes on a caller-owned task. / 在共享 audio_event
 * 任务上调用，非 ISR。须及时返回；此处不得执行阻塞 I/O、接收/等待、退订、关闭或
 * 销毁，生命周期变更应调度到调用方自有任务。
 */
typedef void (*qm_audio_capture_frame_cb_t)(const qm_audio_data_t *frame, void *user_ctx);

/** @brief Capture delivery mechanism for a subscription; the two modes are mutually exclusive. / 采集订阅的交付机制；两种模式互斥。 */
typedef enum {
    /** @brief Queue blocks on a bounded qm_ring_queue for capture_recv. / 在有界 qm_ring_queue 上入队，供 capture_recv 复制到调用方存储。 */
    QM_AUDIO_CAPTURE_DELIVERY_QUEUE = 0,
    /** @brief Deliver borrowed blocks through callback. / 通过注册的帧回调交付借用数据块。 */
    QM_AUDIO_CAPTURE_DELIVERY_CALLBACK,
} qm_audio_capture_delivery_t;

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_TYPES_H */
