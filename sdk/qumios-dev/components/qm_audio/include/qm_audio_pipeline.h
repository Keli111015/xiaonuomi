/**
 * @file qm_audio_pipeline.h
 * @brief Source passthrough and codec conversion. / 输入透传与编解码转换。
 *
 * This header is consumed by qm_audio implementation layers. It is installed
 * under include for shared component build visibility, but it is not part of
 * the application-facing qm_audio.h API. / 本头文件供 qm_audio 实现层使用；虽然
 * 位于 include 以便组件内共享构建可见性，但不属于应用层 qm_audio.h API。
 */
#ifndef QM_AUDIO_PIPELINE_H
#define QM_AUDIO_PIPELINE_H

#include "qm_audio_types.h"
#include "adapter/qm_audio_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque Pipeline handle. / 不透明的 Pipeline 句柄。 */
typedef struct qm_audio_pipeline qm_audio_pipeline_t;

/** @brief Pipeline execution state. / Pipeline 执行状态。 */
typedef enum {
    /** @brief Created but not prepared. / 已创建，尚未准备。 */
    QM_AUDIO_PIPELINE_NEW = 0,
    /** @brief Source processing is active. / 正在处理音频源。 */
    QM_AUDIO_PIPELINE_RUNNING,
    /** @brief Source context is retained for resume. / 保留源上下文，等待恢复。 */
    QM_AUDIO_PIPELINE_PAUSED,
    /** @brief Reached normal end of input. / 正常到达输入末尾。 */
    QM_AUDIO_PIPELINE_FINISHED,
    /** @brief Cancelled without codec tail output. / 已取消，不输出编解码尾帧。 */
    QM_AUDIO_PIPELINE_CANCELLED,
    /** @brief Stopped after an unrecoverable error. / 因不可恢复错误而停止。 */
    QM_AUDIO_PIPELINE_ERROR,
} qm_audio_pipeline_state_t;

/**
 * @brief Source-to-fixed-format configuration. / 输入源到固定目标格式的配置。
 *
 * Pipeline selects passthrough or decode/encode from the two formats. PCM
 * rate, channels and sample representation must match; no resampling or
 * mixing. Source and sink contexts remain caller-owned until destroy.
 * / Pipeline 根据两端格式选择透传或编解码；不改变 PCM 采样率、声道和位宽。
 * 源与 sink 上下文在 destroy 前归调用方持有。
 */
typedef struct {
    /** @brief Fixed sink media format. / 固定输出媒体格式。 */
    qm_audio_stream_format_t output_format;
    /** @brief Source codec and framing. / 输入编码与封包。 */
    qm_audio_codec_config_t input_format;
    /** @brief Source to process. / 待处理的音频源。 */
    qm_audio_source_t source;
    /**
     * @brief Optional decode-resume record for a remote reopen. / 远端重开时可选携带的解码续播记录。
     *
     * Non-NULL asks a remote source to fetch from the recorded compressed
     * offset instead of byte zero and hands the same record to the decoder, so
     * an interrupted stream continues instead of restarting. The record is
     * copied at create and the caller may release it once create returns. A
     * source or decoder that cannot honor it degrades to a full fetch from
     * byte zero. NULL always starts at the stream head. / 非空表示远端源从记录的压缩
     * 偏移而非字节零抓取，并把同一记录交给解码器，使被中断的流得以续播而非重启。记录
     * 在 create 时被复制，调用方可在 create 返回后释放。源或解码器无法满足时降级为从
     * 字节零完整抓取。NULL 始终从流头开始。
     */
    const qm_audio_resume_point_t *resume;
    /**
     * @brief PCM sink invoked by the worker. / 由工作任务调用的 PCM 输出回调。
     * @param[in] buffer Borrowed PCM block. / 借用的 PCM 数据块。
     * @param[in] user_ctx Borrowed caller context. / 借用的调用方上下文。
     * @return QM_EOK on acceptance; a negative qm_errno value on failure.
     * / 接收成功返回 QM_EOK，失败返回负 qm_errno 值。
     * @note Only DISCONTINUITY and END_OF_STREAM flags are used;
     * QM_AUDIO_DATA_NONE denotes ordinary data. Unused timestamp_ms,
     * duration_ms and sequence are zero. EOS may be an empty marker and is
     * emitted only after all decoded PCM has been delivered. / 仅使用断续和结束
     * 标志，普通数据使用 QM_AUDIO_DATA_NONE；未使用的时间戳、时长和序号置零。
     * 结束标记可为空，且在全部解码 PCM 交付后才发出。
     */
    int (*write_pcm)(const qm_audio_data_t *buffer, void *user_ctx);
    /**
     * @brief Atomic packet sink; exclusive with other sinks. / 整包输出，与其他输出互斥。
     *
     * The borrowed packet is nonempty and valid only during the call. Only
     * TIMESTAMP_VALID, DURATION_VALID and DISCONTINUITY flags are used; EOS is
     * reported through Pipeline completion, never END_OF_STREAM or an empty
     * packet. QM_EOK accepts the whole packet; an error terminates the run.
     * / 非空完整包仅在回调期间借用；仅使用时间戳有效、时长有效及断续标志。结束
     * 通过 Pipeline 完成通知，不发送结束标志或空包；成功接收整包，错误终止本轮。
     */
    int (*write_packet)(const qm_audio_data_t *packet, void *user_ctx);
    /**
     * @brief Encoded byte sink; exclusive with the other sinks. / 编码字节流输出，与其他输出互斥。
     *
     * Borrowed for the call; QM_EOK accepts all bytes, an error terminates the
     * run. No codec frame boundaries or timestamps are inferred from chunks.
     * EOF is the Pipeline FINISHED event, never an empty write. Data is
     * nonempty with QM_AUDIO_DATA_NONE; timestamp_ms, duration_ms and sequence
     * are zero. No metadata-validity or EOS flags are used here. / 调用期借用，
     * 成功接收全部字节，错误终止；块边界不代表音频帧。数据必须非空，flags 为
     * QM_AUDIO_DATA_NONE，时间戳、时长和序号置零；不使用元数据有效性或结束
     * 标志，结束由 Pipeline FINISHED 事件通知。
     */
    int (*write_bytes)(const qm_audio_data_t *data, void *user_ctx);
    /** @brief Borrowed sink context. / 借用的输出回调上下文。 */
    void *user_ctx;
} qm_audio_pipeline_config_t;

/**
 * @brief Create a Pipeline without starting I/O. / 创建 Pipeline，但不启动 I/O。
 * @param[in] config Configuration copied by the Pipeline. / 由 Pipeline 复制的配置。
 * @param[out] out_pipeline Returned handle owned by the caller. / 返回的句柄由调用方持有。
 * @return QM_EOK, or a negative qm_errno value on failure. / 成功返回 QM_EOK，失败返回负 qm_errno 值。
 */
int qm_audio_pipeline_create(const qm_audio_pipeline_config_t *config, qm_audio_pipeline_t **out_pipeline);

/**
 * @brief Preserve source and codec state for later resume. / 保留源和编解码状态以供恢复。
 * @param[in] pipeline Pipeline to pause. / 要暂停的 Pipeline。
 * @return QM_EOK, or a negative qm_errno value on failure. / 成功返回 QM_EOK，失败返回负 qm_errno 值。
 * @note Accepted output is controlled separately by the sink owner. / 已接收的输出由 sink 所有者单独控制。
 */
int qm_audio_pipeline_pause(qm_audio_pipeline_t *pipeline);

/**
 * @brief Resume processing from the retained position. / 从保留的位置恢复处理。
 * @param[in] pipeline Pipeline to resume. / 要恢复的 Pipeline。
 * @return QM_EOK, or a negative qm_errno value on failure. / 成功返回 QM_EOK，失败返回负 qm_errno 值。
 */
int qm_audio_pipeline_resume(qm_audio_pipeline_t *pipeline);

/**
 * @brief Cancel processing without codec tail output or EOS. / 取消处理且不输出编解码尾帧或 EOS。
 * @param[in] pipeline Pipeline to cancel. / 要取消的 Pipeline。
 * @return QM_EOK on acceptance; a negative qm_errno value on failure.
 * / 接受取消请求返回 QM_EOK，失败返回负 qm_errno 值。
 * @note Retain borrowed source and sink contexts until destroy succeeds.
 * / 在 destroy 成功前，借用的源和 sink 上下文必须保持有效。
 */
int qm_audio_pipeline_cancel(qm_audio_pipeline_t *pipeline);

/**
 * @brief Wait until Source execution leaves the executor. / 等待 Source 执行退出执行器。
 * @param[in] pipeline Pipeline to wait for. / 要等待的 Pipeline。
 * @param[in] timeout_ms Wait timeout in milliseconds. / 等待超时时间（毫秒）。
 * @return QM_EOK when quiescent, -QM_ETIMEOUT on timeout, or a negative qm_errno
 * value. / 静止后返回 QM_EOK，超时返回 -QM_ETIMEOUT，否则返回负 qm_errno 值。
 * @note A timeout neither cancels the Pipeline nor releases borrowed storage.
 * Execution failure or cancellation is reported by get_state, never by this
 * barrier. A provider that failed close may still retain its callback context;
 * destroy retries close and is the final storage-lifetime barrier.
 * / 超时不会取消 Pipeline，也不会释放借用的存储。执行失败或取消由 get_state
 * 返回，不作为本屏障的返回值。provider 关闭失败时仍可能持有 callback context；
 * destroy 重试关闭，成功后才可释放借用存储。
 */
int qm_audio_pipeline_wait(qm_audio_pipeline_t *pipeline, uint32_t timeout_ms);

/**
 * @brief Copy the current state and last result. / 复制当前状态和最近结果。
 * @param[in] pipeline Pipeline to query. / 要查询的 Pipeline。
 * @param[out] state Current Pipeline state. / 当前 Pipeline 状态。
 * @param[out] result Last execution result. / 最近一次执行结果。
 * @return QM_EOK, or a negative qm_errno value on failure. / 成功返回 QM_EOK，失败返回负 qm_errno 值。
 */
int qm_audio_pipeline_get_state(const qm_audio_pipeline_t *pipeline, qm_audio_pipeline_state_t *state, int *result);

/**
 * @brief Destroy a quiescent Pipeline. / 销毁已静止的 Pipeline。
 * @param[in] pipeline Pipeline to destroy. / 要销毁的 Pipeline。
 * @return QM_EOK, -QM_EBUSY while work is active, or a negative qm_errno value.
 * / 成功返回 QM_EOK，工作未静止时返回 -QM_EBUSY，否则返回负 qm_errno 值。
 * @note Caller-owned source memory, backend handles, and contexts are not freed.
 * / 调用方持有的源内存、后端句柄和上下文不会被释放。
 */
int qm_audio_pipeline_destroy(qm_audio_pipeline_t *pipeline);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_PIPELINE_H */
