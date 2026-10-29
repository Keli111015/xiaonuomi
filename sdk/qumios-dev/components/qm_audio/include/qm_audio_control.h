/**
 * @file qm_audio_control.h
 * @brief Playback control facade over the shared audio service. / 共享音频服务之上的播放控制门面。
 *
 * A Control owns the operation and event-subscription handles created through
 * this API. Callers must honor the documented shutdown / release / unsubscribe
 * barriers before destroying a handle. All functions are task-context APIs;
 * event callbacks run on the shared audio event task.
 * / Control 持有本 API 创建的操作与事件订阅句柄；销毁句柄前必须先完成文档规定的
 * 关闭、释放、退订步骤。所有函数均为任务上下文接口，事件回调运行在共享
 * audio event 任务上。
 */
#ifndef QM_AUDIO_CONTROL_H
#define QM_AUDIO_CONTROL_H

#include "qm_audio_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Description of one playback operation. / 单次播放操作的描述。 */
typedef struct {
    /** @brief Input source; ownership rules follow qm_audio_source_t. / 输入源；所有权规则见 qm_audio_source_t。 */
    qm_audio_source_t source;
    /** @brief Input codec and framing configuration. / 输入编解码与封装配置。 */
    qm_audio_codec_config_t codec;
    /** @brief Service admission priority. / Service 准入优先级。 */
    qm_audio_priority_t priority;
    /** @brief Behavior when the output route is occupied. / 输出路由被占用时的处理策略。 */
    qm_audio_admission_policy_t admission;
    /**
     * @brief Admission deadline in milliseconds. / 准入等待时长（毫秒）。
     *
     * Zero rejects queuing when busy; QM_WAIT_FOREVER disables expiry; any other
     * value must be below UINT32_MAX / 2. Expiry is automatic and reports
     * CANCELLED with result -QM_ETIMEOUT. / 忙时零值不排队；QM_WAIT_FOREVER 表示
     * 不限时；其他值必须小于 UINT32_MAX / 2。超时自动生效，以 CANCELLED 状态和
     * -QM_ETIMEOUT 结果上报。
     */
    uint32_t admission_timeout_ms;
    /**
     * @brief Optional resume record captured from a previous operation. / 可选的续播记录，
     * 由上一操作捕获得到。
     *
     * When non-NULL the Pipeline opens the URI source with an HTTP Range request
     * and restores the codec breakpoint so playback continues near the recorded
     * offset instead of from byte zero. Callers obtain a record from
     * qm_audio_control_capture_resume_point(); an unrepresentable or rejected
     * record transparently falls back to a full restart. Only URI/decoder-backed
     * sources honor it. The record is borrowed for the duration of this call. /
     * 非 NULL 时 Pipeline 会用 HTTP Range 打开 URI 源并恢复编解码
     * 断点，使播放从记录偏移附近继续而非从字节零开始。记录来自
     * qm_audio_control_capture_resume_point()；无法表示或被拒绝的记录会自动回退为从
     * 头全量播放。仅 URI/解码器支持的源会采用该记录。记录在本调用期间借用。
     */
    const qm_audio_resume_point_t *resume;
} qm_audio_control_play_request_t;

/** @brief Snapshot progress flags; test the matching *_VALID bit before use. / 快照进度标志；使用对应数值前须先检查 *_VALID 位。 */
enum {
    QM_AUDIO_PROGRESS_POSITION_VALID = (1U << 0),
    QM_AUDIO_PROGRESS_DURATION_VALID = (1U << 1),
    QM_AUDIO_PROGRESS_SEEKABLE = (1U << 2),
    /** @brief A seek was accepted but has not reached its target. / seek 已接受但尚未到达目标。 */
    QM_AUDIO_PROGRESS_SEEKING = (1U << 3),
    /** @brief Position is measured at the device boundary, before the DAC. / 位置在设备消费边界测得，位于 DAC 之前。 */
    QM_AUDIO_PROGRESS_POSITION_ESTIMATED = (1U << 4),
    /** @brief Duration is estimated, not an exact boundary. / 时长为估算值，不能当作精确边界。 */
    QM_AUDIO_PROGRESS_DURATION_ESTIMATED = (1U << 5),
};

/** @brief Operation state copied by qm_audio_control_get_snapshot(). / 由 qm_audio_control_get_snapshot() 复制的操作状态。 */
typedef struct {
    /** @brief Submitted Service request ID. / 已提交的 Service 请求 ID。 */
    qm_audio_operation_id_t operation_id;
    /** @brief Aggregated operation state. / 聚合后的操作状态。 */
    qm_audio_control_state_t state;
    /** @brief Media position in milliseconds; see the progress flags. / 媒体位置（毫秒）；参考进度标志。 */
    uint32_t position_ms;
    /** @brief Media duration in milliseconds; see the progress flags. / 媒体时长（毫秒）；参考进度标志。 */
    uint32_t duration_ms;
    /** @brief Bitwise combination of QM_AUDIO_PROGRESS_* values. / QM_AUDIO_PROGRESS_* 的按位组合。 */
    uint32_t flags;
    /** @brief Last result; QM_EOK when no failure occurred. / 最近结果；无失败时为 QM_EOK。 */
    int result;
} qm_audio_control_snapshot_t;

/**
 * @brief Create a Control on the shared audio service. / 在共享音频服务上创建一个 Control。
 * @param[out] out_control Receives the new Control; NULL on failure. / 输出新 Control；失败时为 NULL。
 * @return QM_EOK, -QM_EINVAL for a NULL output, -QM_ENOMEM on allocation
 * failure, or -QM_EBUSY when called from the shared event task. / 成功返回
 * QM_EOK；输出为 NULL 返回 -QM_EINVAL；分配失败返回 -QM_ENOMEM；从共享事件任务
 * 调用返回 -QM_EBUSY。
 */
int qm_audio_control_create(qm_audio_control_t **out_control);

/**
 * @brief Stop pending commands and cancel all operations. / 停止待处理命令并取消所有操作。
 *
 * All operation handles must be released before the Control is destroyed.
 * / 销毁 Control 前必须先释放全部操作句柄。
 * @param[in] control Control to shut down. / 待关闭的 Control。
 * @param[in] timeout_ms Shutdown wait budget in milliseconds. / 关闭等待时长（毫秒）。
 * @return QM_EOK, -QM_EINVAL for a NULL control, or -QM_EBUSY while an
 * operation is being released/initialized or shutdown is already active.
 * / 成功返回 QM_EOK；control 为 NULL 返回 -QM_EINVAL；有操作正在释放/初始化或
 * 关闭已在进行返回 -QM_EBUSY。
 */
int qm_audio_control_shutdown(qm_audio_control_t *control, uint32_t timeout_ms);

/**
 * @brief Destroy a shut-down Control with no operations or subscriptions.
 * / 销毁已关闭、且无操作与订阅的 Control。
 * @param[in] control Control to destroy. / 待销毁的 Control。
 * @return QM_EOK, or a negative QUMI error if shutdown is incomplete.
 * / 成功返回 QM_EOK；关闭不完整时返回负的 QUMI 错误码。
 */
int qm_audio_control_destroy(qm_audio_control_t *control);

/**
 * @brief Submit a source for playback. / 提交一个音频源进行播放。
 *
 * Acceptance does not mean audio is audible yet; observe events, snapshots, or
 * use qm_audio_control_wait() for progress. / 接受请求不代表已经出声；请通过
 * 事件、快照或 qm_audio_control_wait() 观察进度。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] request Playback source and scheduling request. / 播放源与调度请求。
 * @param[out] operation_id Receives the new operation identifier. / 输出新操作标识。
 * @return QM_EOK on submission, or a negative QUMI error. / 提交成功返回 QM_EOK；
 * 否则返回负的 QUMI 错误码。
 */
int qm_audio_control_play(qm_audio_control_t *control, const qm_audio_control_play_request_t *request, qm_audio_operation_id_t *operation_id);

/**
 * @brief Write PCM or encoded bytes into a PUSH operation. / 向 PUSH 操作写入 PCM 或编码字节。
 *
 * Accepts only PCM or encoded byte-stream framing; PACKET framing must use
 * qm_audio_control_stream_write_packet(). Input is copied before this call
 * returns and data remains caller-owned. Calls from an event callback must use
 * QM_NO_WAIT. / 仅接受 PCM 或编码字节流封装；PACKET 封装须使用
 * qm_audio_control_stream_write_packet()。返回前已复制输入，data 仍归调用方；
 * 从事件回调调用时必须使用 QM_NO_WAIT。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Active PUSH operation ID. / 活跃的 PUSH 操作 ID。
 * @param[in] data Borrowed input bytes. / 借用的输入字节。
 * @param[in] size Input byte count. / 输入字节数。
 * @param[in] timeout_ms Bounded queue wait in milliseconds. / 有界队列等待时长（毫秒）。
 * @return QM_EOK when all bytes are accepted, -QM_EINVAL for invalid arguments
 * or PACKET framing, -QM_EBUSY for an inactive/terminal operation or a
 * blocking call from the event task, -QM_ETIMEOUT when the queue cannot accept
 * the bytes in time, or another negative QUMI error. / 全部字节接收成功返回
 * QM_EOK；参数无效或为 PACKET 封装返回 -QM_EINVAL；操作未活跃/已终止，或从事件
 * 任务发起阻塞调用返回 -QM_EBUSY；队列超时未接收返回 -QM_ETIMEOUT；其余失败
 * 返回负的 QUMI 错误码。
 */
int qm_audio_control_stream_write(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, const void *data, uint32_t size, uint32_t timeout_ms);

/**
 * @brief Write one complete encoded packet into a PACKET PUSH operation.
 * / 向 PACKET PUSH 操作写入一个完整编码包。
 *
 * The packet must match the Control's fixed native output profile; packet
 * transcoding is unsupported. Acceptance is atomic: QM_EOK means the whole
 * packet was copied, and an error retains no partial packet. Only the
 * TIMESTAMP_VALID, DURATION_VALID and DISCONTINUITY data flags are accepted.
 * End input with qm_audio_control_stream_finish(), never with
 * QM_AUDIO_DATA_END_OF_STREAM. The descriptor and its bytes are borrowed only
 * for the duration of this call. / 数据包必须匹配 Control 固定的原生输出 profile，
 * 不支持包级转码。接收是原子的：返回 QM_EOK 表示整包已复制，出错时不保留半包。
 * 仅接受时间戳有效、时长有效和断续标志；输入结束使用
 * qm_audio_control_stream_finish()，不要使用 QM_AUDIO_DATA_END_OF_STREAM。
 * 描述符及其字节仅在本调用期间借用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Active PACKET PUSH operation ID. / 活跃的 PACKET PUSH 操作 ID。
 * @param[in] packet Non-NULL, nonempty packet descriptor. / 非空且含数据的完整包描述符。
 * @param[in] timeout_ms Bounded queue wait in milliseconds; use QM_NO_WAIT
 * from an event callback. / 有界队列等待时长（毫秒）；从事件回调调用时使用 QM_NO_WAIT。
 * @return QM_EOK when the complete packet is accepted, -QM_EINVAL for invalid
 * metadata/framing or a profile mismatch, -QM_EBUSY for an inactive/terminal
 * operation or a blocking call from the event task, -QM_ETIMEOUT when the
 * queue cannot accept the packet in time, or another negative QUMI error.
 * / 整包接收成功返回 QM_EOK；元数据/封装无效或 profile 不匹配返回 -QM_EINVAL；
 * 操作未活跃/已终止，或从事件任务发起阻塞调用返回 -QM_EBUSY；队列超时未接收
 * 返回 -QM_ETIMEOUT；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_stream_write_packet(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, const qm_audio_data_t *packet, uint32_t timeout_ms);

/**
 * @brief Mark a PUSH operation input complete. / 标记 PUSH 操作输入结束。
 *
 * This is the end-of-input marker for byte-stream and PACKET PUSH operations.
 * It does not imply the physical output has drained; use
 * qm_audio_control_wait() or the terminal event for completion. Repeated calls
 * are harmless until the operation is cancelled. / 这是字节流与 PACKET PUSH 操作的
 * 输入结束标记，不代表物理输出已排空；完成确认请使用 qm_audio_control_wait()
 * 或终止事件。操作取消前重复调用无副作用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Active PUSH operation ID. / 活跃的 PUSH 操作 ID。
 * @return QM_EOK when input is sealed, -QM_EINVAL for an invalid or non-PUSH
 * operation, -QM_EINTR if the operation was cancelled, or another negative
 * QUMI error. / 输入封闭成功返回 QM_EOK；操作无效或非 PUSH 返回 -QM_EINVAL；
 * 操作已取消返回 -QM_EINTR；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_stream_finish(qm_audio_control_t *control, qm_audio_operation_id_t operation_id);

/**
 * @brief Pause an operation while preserving its resumable state. / 暂停操作并保留可恢复状态。
 *
 * HOLD_OUTPUT keeps the output route; YIELD_OUTPUT releases it while retaining
 * source and codec state. Custom sources/codecs must use bounded I/O for the
 * pause to complete. QM_EOK acknowledges acceptance; QM_AUDIO_EVENT_PAUSED
 * confirms completion. A later stop cancels an unacknowledged pause; a failed
 * transition is reported with QM_AUDIO_EVENT_ERROR. / HOLD_OUTPUT 保留输出路由；
 * YIELD_OUTPUT 释放路由但保留源与编解码状态。自定义源/编解码器须使用有界 I/O，
 * 暂停才能完成。QM_EOK 表示请求已接受，QM_AUDIO_EVENT_PAUSED 确认暂停完成；
 * 后续 stop 可取消尚未确认的暂停，转换失败通过 QM_AUDIO_EVENT_ERROR 上报。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to pause. / 待暂停的操作。
 * @param[in] mode Output-route retention policy. / 输出路由保留策略。
 * @return QM_EOK when accepted, -QM_EINVAL for an invalid mode/argument,
 * -QM_EBUSY when the operation cannot pause or the Control is shutting down,
 * or another negative QUMI error. Completion is reported asynchronously.
 * / 请求接受返回 QM_EOK；模式/参数无效返回 -QM_EINVAL；操作无法暂停或 Control
 * 正在关闭返回 -QM_EBUSY；其余失败返回负的 QUMI 错误码。完成异步上报。
 */
int qm_audio_control_pause(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_pause_mode_t mode);

/**
 * @brief Resume from the retained source position. / 从保留的源位置恢复播放。
 *
 * QM_EOK acknowledges the request; QM_AUDIO_EVENT_RESUMED confirms output has
 * resumed. A failed transition is reported with QM_AUDIO_EVENT_ERROR.
 * / QM_EOK 表示请求已接受；QM_AUDIO_EVENT_RESUMED 确认输出已恢复，转换失败通过
 * QM_AUDIO_EVENT_ERROR 上报。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Paused operation to resume. / 待恢复的已暂停操作。
 * @return QM_EOK when accepted, -QM_EBUSY when the operation is not resumable
 * or the Control is shutting down, or another negative QUMI error. / 请求接受
 * 返回 QM_EOK；操作不可恢复或 Control 正在关闭返回 -QM_EBUSY；其余失败返回负的
 * QUMI 错误码。
 */
int qm_audio_control_resume(qm_audio_control_t *control, qm_audio_operation_id_t operation_id);

/**
 * @brief Request an absolute media position. / 请求绝对媒体位置。
 *
 * PCM routes align down to a sample frame; native encoded byte/packet
 * passthrough routes return -QM_ENOSYS because the Pipeline does not invent
 * container or codec seek semantics. Memory and local files support
 * restart-and-decode seeking; finite HTTP objects additionally require
 * Content-Length and a strong ETag, and replay validates If-Match and
 * re-downloads the prefix. Decoder-backed compressed media may take time
 * proportional to the target. / PCM 路由按采样帧向下对齐；原生编码字节流/数据包
 * 直通路由返回 -QM_ENOSYS，因为 Pipeline 不虚构容器或编解码器的 seek 语义。
 * 内存与本地文件支持重启并解码的 seek；有限长度 HTTP 对象还要求 Content-Length
 * 和强 ETag，重放会校验 If-Match 并重新下载前缀。基于解码器的压缩媒体定位耗时
 * 随目标位置增加。
 *
 * QM_EOK acknowledges the request, not completion: SEEKING remains set until
 * QM_AUDIO_EVENT_SEEKED. A paused operation stays paused and resolves the seek
 * after resume. Source exit is asynchronous; stop can cancel either that
 * pending transition or decoding. Unknown-length targets past EOF resolve to
 * EOF; targets beyond a known exact duration return -QM_EINVAL. Nonseekable
 * inputs return -QM_ENOSYS without changing playback. Source bytes must remain
 * unchanged until release. Do not call from an event callback. / QM_EOK 只表示
 * 请求已接受，不表示完成：SEEKING 保持置位直到 QM_AUDIO_EVENT_SEEKED。暂停中的
 * 操作保持暂停，恢复后再处理 seek。源退出是异步的，stop 可取消待处理的转换或
 * 解码。未知长度源越过 EOF 时定位到 EOF；已知精确时长源越界返回 -QM_EINVAL。
 * 不可 seek 输入返回 -QM_ENOSYS 且不改变播放。源字节在 release 前必须保持不变。
 * 不得从事件回调调用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to reposition. / 待重定位的操作。
 * @param[in] position_ms Target media position in milliseconds. / 目标媒体位置（毫秒）。
 * @return QM_EOK when accepted, -QM_EINVAL for an invalid argument or an
 * out-of-range position on a known-duration source, -QM_EBUSY during an
 * incompatible transition, -QM_ENOSYS for a nonseekable route, or another
 * negative QUMI error. / 请求接受返回 QM_EOK；参数无效或已知时长源越界返回
 * -QM_EINVAL；转换状态不兼容返回 -QM_EBUSY；路由不可 seek 返回 -QM_ENOSYS；
 * 其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_seek(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t position_ms);

/**
 * @brief Stop an operation and discard pending output. / 停止操作并丢弃待播放数据。
 *
 * Stopping an operation that already reached a terminal state is an idempotent
 * no-op: it is accepted without arming a transition, so the following release
 * is not deferred. / 对已进入终态的操作调用停止是幂等空操作：请求被接受但不挂起
 * 迁移，因此随后的释放不会被推迟。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to stop. / 待停止的操作。
 * @param[in] reason Product-defined stop reason. / 产品定义的停止原因。
 * @return QM_EOK when accepted, -QM_EINVAL for an invalid argument, -QM_EBUSY
 * when the operation is not stoppable or the Control is shutting down, or
 * another negative QUMI error. / 请求接受返回 QM_EOK；参数无效返回 -QM_EINVAL；
 * 操作不可停止或 Control 正在关闭返回 -QM_EBUSY；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_stop(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t reason);

/**
 * @brief Wait for an operation to become terminal. / 等待操作进入终止状态。
 *
 * Caller wait expiry neither cancels nor releases the operation. Admission
 * expiry does terminate it; inspect the snapshot to distinguish the two
 * timeouts. / 调用方等待超时既不会取消也不会释放操作；准入超时会终止操作，
 * 可通过快照区分这两种超时。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to observe. / 待观察的操作。
 * @param[in] timeout_ms Wait budget in milliseconds. / 等待时长（毫秒）。
 * @return QM_EOK on normal completion, -QM_EINTR when stopped or preempted,
 * the operation's failure code on error, or -QM_ETIMEOUT when admission or the
 * caller's wait budget expires. / 正常完成返回 QM_EOK；停止或抢占返回 -QM_EINTR；
 * 出错返回操作错误码；准入或调用方等待超时返回 -QM_ETIMEOUT。
 */
int qm_audio_control_wait(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t timeout_ms);

/**
 * @brief Copy the current state and position of an operation. / 复制操作的当前状态与位置。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to query. / 待查询的操作。
 * @param[out] snapshot Caller-owned snapshot output. / 调用方快照输出。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_control_get_snapshot(const qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_control_snapshot_t *snapshot);

/**
 * @brief Capture a resume record for a later re-play. / 捕获续播记录供后续重播使用。
 *
 * Samples the live decoder and records the source offset plus codec breakpoint,
 * so a subsequent qm_audio_control_play() can continue near this point instead
 * of refetching from byte zero. Capture before the operation is stopped or
 * released: once the decoder is destroyed the operation returns -QM_ENOSYS.
 * Only URI/decoder-backed operations produce a record; passthrough, file, PUSH
 * and memory sources return -QM_ENOSYS. The record is a plain value with no
 * ownership crossing the call and may be copied freely. Do not call from an
 * event callback. / 采样活动解码器并记录源偏移与编解码断点，使后续
 * qm_audio_control_play() 可从该点附近继续而非从字节零重取。须在操作被停止或释放
 * 之前捕获：解码器销毁后返回 -QM_ENOSYS。仅 URI/解码器支持的操作会产生记录；直通、
 * 文件、PUSH 与内存源返回 -QM_ENOSYS。记录是普通值，无跨调用的所有权，可自由拷贝。
 * 不得从事件回调调用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Operation to sample. / 待采样的操作。
 * @param[out] resume Caller-owned record output. / 调用方持有的记录输出。
 * @param[in] timeout_ms Bounded wait for the decoder worker in milliseconds. /
 * 等待解码器任务的有界时长（毫秒）。
 * @return QM_EOK on success, -QM_EINVAL for invalid arguments, -QM_ENOSYS when
 * the route or a terminal operation cannot produce a record, -QM_ETIMEOUT when
 * the decoder does not answer in time, or another negative QUMI error.
 * / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL；路由或已终止操作无法产生记录返回
 * -QM_ENOSYS；解码器未及时响应返回 -QM_ETIMEOUT；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_capture_resume_point(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_resume_point_t *resume, uint32_t timeout_ms);

/**
 * @brief Release a terminal operation. / 释放终止状态的操作。
 *
 * A failure retains the operation for a later retry; a success invalidates the
 * operation ID. / 失败时保留操作以供重试；成功会使操作 ID 失效。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] operation_id Terminal operation to release. / 待释放的终止操作。
 * @param[in] timeout_ms Release wait budget in milliseconds. / 释放等待时长（毫秒）。
 * @return QM_EOK, -QM_EINVAL for an invalid argument or operation ID,
 * -QM_EBUSY while the operation is not terminal or is still in use, or another
 * negative QUMI error. / 成功返回 QM_EOK；参数或操作 ID 无效返回 -QM_EINVAL；
 * 操作未终止或仍在使用返回 -QM_EBUSY；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_release(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t timeout_ms);

/**
 * @brief Subscribe to this Control's operation lifecycle events. / 订阅本 Control 的操作生命周期事件。
 *
 * Callbacks run on the shared event task. Event data is valid only during the
 * callback; user_ctx must remain valid until unsubscribe succeeds.
 * / 回调运行在共享事件任务上；事件数据仅在回调期间有效，退订成功前必须保持
 * user_ctx 有效。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] callback Event callback. / 事件回调。
 * @param[in] user_ctx Borrowed callback context, kept alive by the caller. / 借用的回调上下文，由调用方保持存活。
 * @param[out] out_subscription Receives the event subscription. / 输出事件订阅。
 * @return QM_EOK, -QM_EINVAL for invalid arguments, -QM_ENOMEM when the
 * subscription cannot be allocated, -QM_EFULL when the subscriber limit is
 * reached, or another negative QUMI error. / 成功返回 QM_EOK；参数无效返回
 * -QM_EINVAL；订阅分配失败返回 -QM_ENOMEM；订阅数达上限返回 -QM_EFULL；其余
 * 失败返回负的 QUMI 错误码。
 */
int qm_audio_control_subscribe(qm_audio_control_t *control, qm_audio_event_cb_t callback, void *user_ctx, qm_audio_subscription_t *out_subscription);

/**
 * @brief Unsubscribe and wait for in-flight callbacks. / 取消订阅并等待在途回调结束。
 *
 * Do not call from the shared event task. / 不得从共享事件任务调用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] subscription Event subscription to remove. / 待移除的事件订阅。
 * @return QM_EOK after in-flight callbacks quiesce, -QM_EINVAL for an invalid
 * or already removed subscription, -QM_EBUSY when called from the shared event
 * task, or another negative QUMI error. / 在途回调结束后返回 QM_EOK；订阅无效或
 * 已移除返回 -QM_EINVAL；从共享事件任务调用返回 -QM_EBUSY；其余失败返回负的
 * QUMI 错误码。
 */
int qm_audio_control_unsubscribe(qm_audio_control_t *control, qm_audio_subscription_t subscription);

/**
 * @brief Queue a frontend wake report to this Control's event task.
 * / 将前端唤醒报告排入本 Control 的事件任务。
 *
 * Copies the wake metadata into the shared event queue; the event is delivered
 * to this Control's subscribers as QM_AUDIO_EVENT_WAKE. Never call from the
 * shared event task. / 唤醒元数据被复制进共享事件队列，事件以
 * QM_AUDIO_EVENT_WAKE 交付给本 Control 的订阅者；不得从共享事件任务调用。
 * @param[in] control Owning Control. / 所属 Control。
 * @param[in] wake Wake metadata copied by the API. / 由接口复制的唤醒元数据。
 * @return QM_EOK when queued, -QM_EINVAL for invalid arguments, -QM_EBUSY when
 * called from the shared event task, or another negative QUMI error. / 排队成功
 * 返回 QM_EOK；参数无效返回 -QM_EINVAL；从共享事件任务调用返回 -QM_EBUSY；
 * 其余失败返回负的 QUMI 错误码。
 */
int qm_audio_control_report_wake(qm_audio_control_t *control, const qm_audio_wake_info_t *wake);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_CONTROL_H */
