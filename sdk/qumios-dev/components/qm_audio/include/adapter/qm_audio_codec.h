/**
 * @file qm_audio_codec.h
 * @brief Platform audio codec provider contracts. / 平台音频编解码 provider 契约。
 *
 * This is the single codec-provider entry point for qm_audio. Decoder and
 * encoder contracts live together because both are platform media providers,
 * while their data flow and lifecycle APIs remain deliberately separate.
 * / 这是 qm_audio 唯一的编解码 provider 入口。解码器与编码器契约放在一起，因为
 * 二者都是平台媒体 provider，但各自的数据流与生命周期 API 刻意保持独立。
 *
 * The decoder accepts encoded bytes and emits negotiated PCM. The encoder
 * accepts negotiated PCM and emits complete encoded packets. Neither provider
 * acquires Service sessions, opens an output device, or implements cloud
 * control. Vendor SDK attributes remain inside the platform implementation.
 * / 解码器接收编码字节并输出协商的 PCM；编码器接收协商的 PCM 并输出完整编码包。
 * 两个 provider 都不获取 Service 会话、打开输出设备或实现云控制。厂商 SDK 属性
 * 保留在平台实现内部。
 *
 * These are fixed named functions rather than an ops-table SPI. Exactly one
 * platform implementation is linked into the firmware. Pipeline serializes
 * operations for each context in task context. Callbacks are serialized per
 * provider instance and never run in an ISR; no callback may call back into
 * the same provider's push, flush, finish, or close. / 这些是固定命名函数，而非
 * ops 表 SPI。固件中只链接一个平台实现。Pipeline 在任务上下文为每个 context
 * 串行操作。回调按 provider 实例串行，绝不在 ISR 中运行；任何回调都不得回调
 * 同一 provider 的 push、flush、finish 或 close。
 */
#ifndef QM_AUDIO_CODEC_H
#define QM_AUDIO_CODEC_H

#include "qm_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Decoder provider / 解码器 provider 接口
 * @{ */

/**
 * @brief Deliver a borrowed PCM block to Pipeline. / 向 Pipeline 交付借用的 PCM 数据块。
 *
 * The block and bytes are valid only until callback return and must match the
 * negotiated output PCM format. Only DISCONTINUITY and END_OF_STREAM are
 * accepted; ordinary data uses QM_AUDIO_DATA_NONE. Timestamp, duration, and
 * sequence are zero. Pipeline owns downstream EOS propagation after flush
 * confirms all PCM is emitted. The callback must accept a whole block or
 * return a negative QUMI error. A sink using partial writes completes its
 * suffix internally; it must not replay a block after delivering a prefix.
 * The callback may apply bounded task-context backpressure, must honor
 * cancellation, and must not wait indefinitely. / 数据块与字节仅在回调返回前有效，
 * 且必须匹配协商的 PCM 输出格式。仅接受断续与结束标志，普通数据使用
 * QM_AUDIO_DATA_NONE；时间戳、时长和序号置零。flush 确认全部 PCM 输出后，由
 * Pipeline 负责向下游传播 EOS。回调必须整块接收或返回负的 QUMI 错误；使用部分
 * 写入的 sink 须在内部完成剩余后缀，不得在交付前缀后重放整块。回调可施加有界
 * 的任务上下文背压，必须响应取消，且不得无限等待。
 */
typedef int (*qm_audio_decoder_emit_pcm_fn)(const qm_audio_data_t *buffer, void *user_ctx);

/**
 * @brief Wire framing for a decoder session. / 解码器会话的传输封包格式。
 *
 * These values describe wire framing, not a codec implementation. The platform
 * maps them to native decoder attributes and rejects ambiguous input.
 * / 这些值描述传输封包，而非编解码实现。平台将其映射到原生解码器属性，并拒绝
 * 有歧义的输入。
 */
typedef enum {
    /** @brief Self-framed codec/container stream; push boundaries are ignored. / 自描述编解码器或容器流，忽略 push 边界。 */
    QM_AUDIO_DECODER_FRAMING_STREAM = 0,
    /** @brief Ogg container carrying Opus packets. / 承载 Opus 包的 Ogg 容器。 */
    QM_AUDIO_DECODER_FRAMING_OPUS_OGG,
    /** @brief Raw Opus with a 4-byte big-endian length. / 带四字节大端长度的原始 Opus。 */
    QM_AUDIO_DECODER_FRAMING_OPUS_RAW_HEADER,
    /** @brief Raw Opus packets with a fixed CBR length. / 使用固定 CBR 包长度的原始 Opus。 */
    QM_AUDIO_DECODER_FRAMING_OPUS_CBR,
} qm_audio_decoder_framing_t;

/**
 * @brief Fixed decoder settings borrowed through user_ctx. / 通过 user_ctx 借用的固定解码器配置。
 *
 * Pipeline copies retained scalar settings during create. The codec hint
 * storage remains valid through close when the provider retains it.
 * / Pipeline 在 create 时复制保留的标量配置。provider 若保留 codec hint，其存储
 * 在 close 前保持有效。
 */
typedef struct {
    /** @brief Decoder hint; NULL selects auto-probe. / 解码器提示；NULL 表示自动探测。 */
    const char *codec_hint;
    /** @brief Known compressed length; zero means unknown or streaming. / 已知压缩长度；零表示未知或流式输入。 */
    uint32_t input_size;
    /** @brief Push ring capacity; zero uses the platform default. / Push 环形缓冲容量；零值使用平台默认值。 */
    uint32_t fifo_bytes;
    /** @brief Wire framing for the source. / 输入源传输封包格式。 */
    qm_audio_decoder_framing_t framing;
    /** @brief Raw Opus packet size for CBR framing. / CBR 封包模式下的原始 Opus 包大小。 */
    uint32_t packet_bytes;
    /** @brief Declared duration for each Opus packet; zero means unknown. / 每个 Opus 包的声明时长；零表示未知。 */
    uint32_t packet_duration_ms;
    /**
     * @brief Decode-resume record borrowed through open. / 通过 open 借用的解码续播记录。
     *
     * Non-NULL asks the provider to continue the recorded stream instead of
     * starting at the stream head; the provider copies whatever it retains.
     * A record the provider cannot honor fails open, so Pipeline can retry from
     * byte zero. NULL always starts at the stream head. / 非空表示 provider 从记录的
     * 流位置续解而非从流头开始，provider 复制其保留的内容。无法满足的记录会使 open
     * 失败，Pipeline 可据此从字节零重试。NULL 始终从流头开始。
     */
    const qm_audio_resume_point_t *resume;
} qm_audio_decoder_config_t;

/**
 * @brief Validate decoder capability without allocation or I/O. / 校验解码器能力，不分配内存也不执行 I/O。
 * @param[in] output_format Negotiated PCM output format. / 协商的 PCM 输出格式。
 * @param[in] config Decoder settings. / 解码器配置。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_check_format(const qm_audio_pcm_format_t *output_format, const qm_audio_decoder_config_t *config);

/**
 * @brief Create a decoder context without acquiring an output device. / 创建解码器上下文，不占用输出设备。
 *
 * ctx is an opaque provider-owned handle returned through this output
 * parameter and must be passed back unchanged until close succeeds. emit_ctx
 * remains borrowed through close. user_ctx may point to a
 * qm_audio_decoder_config_t used during open; retained scalar values must be
 * copied by the provider. Failed open unwinds its resources. / ctx 是不透明的
 * provider 自有句柄，通过本输出参数返回，在 close 成功前必须原样传回。emit_ctx
 * 在 close 前保持借用。user_ctx 可指向 open 时使用的 qm_audio_decoder_config_t；
 * provider 必须复制其保留的标量值。open 失败会回滚其资源。
 * @param[out] ctx Provider-owned decoder context. / provider 自有的解码器上下文。
 * @param[in] output_format Negotiated PCM output format. / 协商的 PCM 输出格式。
 * @param[in] emit_pcm PCM emit callback. / PCM 输出回调。
 * @param[in] emit_ctx Borrowed callback context. / 借用的回调上下文。
 * @param[in] user_ctx Borrowed config reference, may be NULL. / 借用的配置引用，可为 NULL。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_open(void **ctx, const qm_audio_pcm_format_t *output_format, qm_audio_decoder_emit_pcm_fn emit_pcm, void *emit_ctx, void *user_ctx);

/**
 * @brief Submit encoded bytes and emit decoded PCM. / 提交编码字节并交付解码 PCM。
 *
 * Input bytes are borrowed only for this call. Push boundaries are not codec
 * packet boundaries. consumed measures accepted encoded bytes, not emitted PCM
 * bytes; retry only an unconsumed suffix after a recoverable error.
 * / 输入字节仅在本调用期间借用。push 边界不是编解码包边界。consumed 计量被接受
 * 的编码字节，而非已输出的 PCM 字节；可恢复错误后只重试未消费的后缀。
 * @param[in] ctx Decoder context. / 解码器上下文。
 * @param[in] data Borrowed encoded bytes. / 借用的编码字节。
 * @param[in] size Input byte count. / 输入字节数。
 * @param[in] timeout_ms Bounded wait in milliseconds. / 有界等待时长（毫秒）。
 * @param[out] consumed Accepted encoded byte count. / 已接受的编码字节数。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_push(void *ctx, const void *data, uint32_t size, uint32_t timeout_ms, uint32_t *consumed);

/**
 * @brief Seal encoded input and emit all remaining PCM. / 封闭编码输入并交付剩余 PCM。
 *
 * This is natural EOF draining, not cancellation. A timeout retains state for
 * retry; no result proves physical speaker drain. / 这是自然的 EOF 排空，不是取消。
 * 超时保留状态以供重试；任何结果都不证明物理扬声器已排空。
 * @param[in] ctx Decoder context. / 解码器上下文。
 * @param[in] timeout_ms Bounded wait in milliseconds. / 有界等待时长（毫秒）。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_flush(void *ctx, uint32_t timeout_ms);

/**
 * @brief Quiesce processing and release one decoder context. / 静止处理并释放一个解码器上下文。
 *
 * Failure retains state for retry. Close does not implicitly flush a cancelled
 * run or emit additional PCM. / 失败保留状态以供重试。close 不会隐式 flush 已取消
 * 的运行，也不会额外输出 PCM。
 * @param[in] ctx Decoder context. / 解码器上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_close(void *ctx);

/**
 * @brief Query cached media duration. / 查询缓存的媒体时长。
 *
 * Returns -QM_ENOSYS until metadata is available. Duration is an estimate until
 * Pipeline has counted all decoded PCM. / 元数据可用前返回 -QM_ENOSYS。在 Pipeline
 * 统计完全部解码 PCM 之前，时长都是估算值。
 * @param[in] ctx Decoder context. / 解码器上下文。
 * @param[out] duration_ms Media duration in milliseconds. / 媒体时长（毫秒）。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_get_duration(void *ctx, uint32_t *duration_ms);

/**
 * @brief Capture decode-resume state for a later reopen. / 捕获解码续播状态，供稍后重开使用。
 *
 * Records the current compressed read offset and the codec state needed to
 * continue that stream, so a later open carrying the same record skips the
 * bytes already decoded. Only a session that is still running can be captured;
 * a quiesced or terminal session returns -QM_ENOSYS, and so does a provider
 * whose stream cannot be continued mid-way. On any non-OK result nothing is
 * written, so the caller may safely keep a previously captured record; on
 * -QM_ETIMEOUT it may retry or fall back to a full restart. The record is
 * caller-owned and holds no storage borrowed from the provider. / 记录当前压缩读取
 * 偏移以及继续该流所需的编解码状态，使之后携带同一记录 open 时可跳过已解码字节。仅
 * 仍在运行的会话可被捕获；静止或终态会话返回 -QM_ENOSYS，无法中途续解的 provider 亦然。
 * 任一非 OK 结果都不写入任何内容，调用方可安全保留先前捕获的记录；超时返回
 * -QM_ETIMEOUT，调用方可重试或回退为完整重启。记录归调用方持有，不包含 provider
 * 借出的存储。
 * @param[in] ctx Decoder context. / 解码器上下文。
 * @param[out] resume Caller-owned record filled on success. / 成功时填充的调用方记录。
 * @param[in] timeout_ms Bounded capture wait in milliseconds. / 有界捕获等待时长（毫秒）。
 * @return QM_EOK, -QM_EINVAL for invalid arguments, -QM_ENOSYS when the stream
 * cannot be captured, -QM_ETIMEOUT when the bounded wait expires, or another
 * negative QUMI error. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL；无法捕获返回
 * -QM_ENOSYS；有界等待超时返回 -QM_ETIMEOUT；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_decoder_capture_resume(void *ctx, qm_audio_resume_point_t *resume, uint32_t timeout_ms);

/** @} */

/** @name Encoder provider / 编码器 provider 接口
 * @{ */

/**
 * @brief Emit one borrowed complete encoded packet. / 输出一个借用的完整编码包。
 *
 * The packet is nonempty and valid only until callback return. Only
 * TIMESTAMP_VALID, DURATION_VALID, and DISCONTINUITY are allowed; EOS is
 * reported by finish rather than as an encoded packet. / 包非空且仅在回调返回前
 * 有效。仅允许时间戳有效、时长有效和断续标志；结束由 finish 上报，不编码为包。
 * @param[in] packet Borrowed complete encoded packet. / 借用的完整编码包。
 * @param[in] user_ctx Borrowed callback context. / 借用的回调上下文。
 * @return QM_EOK on acceptance; a negative qm_errno value on failure. / 接收成功返回 QM_EOK，失败返回负 qm_errno 值。
 */
typedef int (*qm_audio_encoder_emit_packet_fn)(const qm_audio_data_t *packet, void *user_ctx);

/**
 * @brief Validate encoder capability without allocation or I/O. / 校验编码器能力，不分配内存也不执行 I/O。
 * @param[in] output Negotiated output stream format. / 协商的输出流格式。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_encoder_check_format(const qm_audio_stream_format_t *output);

/**
 * @brief Create one encoder context. / 创建一个编码器上下文。
 *
 * The output profile is copied. emit_ctx remains borrowed through close. The
 * provider owns its codec state and does not create an output device.
 * / 输出 profile 会被复制。emit_ctx 在 close 前保持借用。provider 持有自己的
 * 编解码状态，不创建输出设备。
 * @param[out] ctx Provider-owned encoder context. / provider 自有的编码器上下文。
 * @param[in] output Negotiated output stream format. / 协商的输出流格式。
 * @param[in] emit_packet Packet emit callback. / 编码包输出回调。
 * @param[in] emit_ctx Borrowed callback context. / 借用的回调上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_encoder_open(void **ctx, const qm_audio_stream_format_t *output, qm_audio_encoder_emit_packet_fn emit_packet, void *emit_ctx);

/**
 * @brief Accept PCM and assemble codec frames across calls. / 接收 PCM 并跨调用组装编码帧。
 *
 * The descriptor and bytes are borrowed only for this call. Only
 * QM_AUDIO_DATA_NONE or QM_AUDIO_DATA_END_OF_STREAM is accepted; EOS seals
 * input after these samples and may be an empty marker. Timestamp, duration,
 * and sequence are unused and must be zero. / 描述符与字节仅在本调用期间借用。
 * 仅接受 QM_AUDIO_DATA_NONE 或 QM_AUDIO_DATA_END_OF_STREAM；EOS 在这些采样后
 * 封闭输入，可为空标记。时间戳、时长和序号不被使用，必须置零。
 * @param[in] ctx Encoder context. / 编码器上下文。
 * @param[in] pcm Borrowed PCM block or EOS marker. / 借用的 PCM 块或 EOS 标记。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_encoder_push(void *ctx, const qm_audio_data_t *pcm);

/**
 * @brief Seal input, pad the final frame, and emit the codec tail once. / 封闭输入、填充最后一帧并输出一次编码尾帧。
 *
 * This is codec finishing, not physical speaker drain. / 这是编解码结束，而非物理
 * 扬声器排空。
 * @param[in] ctx Encoder context. / 编码器上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_encoder_finish(void *ctx);

/**
 * @brief Discard pending input and release encoder state. / 丢弃待处理输入并释放编码器状态。
 *
 * Close never emits a codec tail after cancellation. / close 在取消后不会输出
 * 编码尾帧。
 * @param[in] ctx Encoder context. / 编码器上下文。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_encoder_close(void *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_CODEC_H */
