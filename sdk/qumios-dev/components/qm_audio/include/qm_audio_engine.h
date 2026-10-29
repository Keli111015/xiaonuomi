/**
 * @file qm_audio_engine.h
 * @brief Native audio device lifecycle and exclusive rendering. / 原生音频设备生命周期与独占渲染。
 *
 * Engine enforces physical ownership and I/O lifetime, not business priority.
 * An Engine with rendering enabled owns one resident render worker. Producers
 * submit copied PCM blocks, encoded bytes or complete encoded packets to one
 * bounded queue. The worker executes backend start/write/pause/resume/stop,
 * position queries and volume/mute settings. Drain reserves exclusive device
 * access on its calling task; stop calls abort from its caller to interrupt
 * blocked write/drain. After Service takes ownership, public render_open is
 * rejected; Service uses its private owner-checked entry. Capture subscriptions
 * are declared separately in qm_audio_capture.h. / Engine 负责物理所有权与 I/O
 * 生命周期，不处理业务优先级。开启渲染的 Engine 持有一个常驻渲染任务；生产者
 * 向有界队列提交复制后的 PCM 块、编码字节或完整编码包。任务执行后端
 * start/write/pause/resume/stop、位置查询和音量/静音设置。drain 在调用任务中
 * 独占设备访问；stop 从调用任务执行 abort，打断阻塞的 write/drain。Service
 * 取得所有权后，公开 render_open 返回拒绝；Service 通过校验所有者的私有入口
 * 打开流。采集订阅另行声明于 qm_audio_capture.h。
 *
 * Calls require task context. Serialize lifecycle/control calls on one handle;
 * stop may interrupt a blocked write/drain, but close/destroy must not race
 * access to the handle. Calls with timeout_ms use milliseconds: zero does not
 * wait and QM_WAIT_FOREVER waits until completion or interruption. A timeout
 * never authorizes freeing memory that a backend may still be accessing.
 * / 所有调用需要任务上下文。同一句柄上的生命周期/控制调用须串行；stop 可打断
 * 阻塞的 write/drain，但 close/destroy 不得与句柄访问并发。带 timeout_ms 的调用
 * 使用毫秒：零值不等待，QM_WAIT_FOREVER 等待完成或被打断。超时绝不意味着后端
 * 不再访问内存；在 I/O 确认静止前不得释放相关资源。
 */
#ifndef QM_AUDIO_ENGINE_H
#define QM_AUDIO_ENGINE_H

#include "adapter/qm_audio_device.h"
#include "qm_audio_types.h"
#include "qm_audio_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque Engine handle. / 不透明的 Engine 句柄。 */
typedef struct qm_audio_engine qm_audio_engine_t;
/** @brief Opaque exclusive render-stream handle. / 不透明的独占渲染流句柄。 */
typedef struct qm_audio_render_stream qm_audio_render_stream_t;

/**
 * @brief Fixed device formats and bounded capture resources. / 固定设备格式与有界采集资源。
 *
 * The top-level structure is copied during create. The native audio device is
 * the linked implementation of adapter/qm_audio_device.h; the Engine owns only
 * the context created by qm_audio_device_open. / 顶层结构在 create 时复制。原生
 * 音频设备是 adapter/qm_audio_device.h 的链接实现；Engine 只拥有
 * qm_audio_device_open 创建的上下文。
 */
typedef struct {
    /** @brief Native capture stream format; invalid/zero PCM disables PCM capture, opaque encoded profiles may omit PCM geometry. / 原生采集流格式；无效/零 PCM 禁用 PCM 采集，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t input_format;
    /** @brief Native render stream format; invalid/zero PCM disables PCM render, opaque encoded profiles may omit PCM geometry. / 原生播放流格式；无效/零 PCM 禁用 PCM 播放，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t output_format;
    /**
     * @brief Maximum bytes per capture delivery block. / 每个采集交付块的最大字节数。
     *
     * Required for capture; PCM aligns to input sample frames, encoded capture
     * must fit at least max_packet_bytes. Zero selects the native format's
     * default capacity. / 采集必需；PCM 按输入采样帧对齐，编码采集至少容纳
     * max_packet_bytes。零值选择原生格式的默认容量。
     */
    uint32_t capture_frame_bytes;
    /**
     * @brief Default subscription capacity in capture blocks. / 默认采集订阅容量（块）。
     *
     * Zero selects CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN; ignored without
     * capture. / 零值使用 CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN；无采集时忽略。
     */
    uint32_t capture_queue_len;
} qm_audio_engine_config_t;

/** @brief Mechanism-only rendering options. / 仅描述渲染机制的选项，不含调度策略。 */
typedef struct {
    /** @brief Reliable preservation or documented realtime latency behavior. / 可靠保序或文档规定的实时延迟行为。 */
    qm_audio_delivery_t delivery;
} qm_audio_render_config_t;

/**
 * @brief Allocate an Engine and open its platform backend. / 分配 Engine 并打开平台后端。
 * @param[in] config Required configuration; at least one direction is enabled.
 * Unsupported formats must be rejected, not silently reinterpreted as PCM.
 * / 必填配置；至少启用一个方向。不支持的格式必须拒绝，不得静默当作 PCM。
 * @param[out] out_engine Required output; caller owns cleanup through shutdown
 * followed by destroy. On failure it is normally NULL; failed rollback returns
 * a non-NULL closing Engine that must be retained for cleanup retry. Creation
 * does not start a render run, but starts the resident render worker when
 * output is enabled. / 必填输出；调用方负责 shutdown 后 destroy。失败时通常为
 * NULL；回滚失败会返回非 NULL 的 closing Engine，须保留以待清理重试。创建不会
 * 启动渲染运行，但启用输出时会启动常驻渲染任务。
 * @return QM_EOK, -QM_EINVAL for invalid options, -QM_ENOMEM for allocation
 * failure, -QM_ENOSYS for unsupported formats, or another negative qm_errno
 * from backend setup. Failure unwinds partial construction. / 成功返回 QM_EOK；
 * 选项无效返回 -QM_EINVAL；分配失败返回 -QM_ENOMEM；格式不支持返回 -QM_ENOSYS；
 * 后端安装失败返回其他负 qm_errno。失败会回滚部分构造。
 */
int qm_audio_engine_create(const qm_audio_engine_config_t *config, qm_audio_engine_t **out_engine);

/**
 * @brief Borrow this Engine's independent Capture. / 借用 Engine 持有的独立 Capture。
 * @param[in] engine Live Engine; exclude concurrent shutdown/destruction. / 有效 Engine；须排除并发关闭/销毁。
 * @return Borrowed handle or NULL when unavailable. Engine retains lifecycle ownership;
 * consumers only subscribe. / 借用句柄；不可用时为 NULL。Engine 保留生命周期所有权，消费者仅进行订阅。
 */
qm_audio_capture_t *qm_audio_engine_get_capture(qm_audio_engine_t *engine);

/**
 * @brief Close admission, stop device I/O, and quiesce capture callbacks.
 * / 关闭准入、停止设备 I/O 并静止采集回调。
 *
 * Shut down a managing Service, release its sessions and destroy it before
 * shutting down the Engine. Successful shutdown closes the backend but does
 * not free Engine, render, or subscription handles; those
 * remain available for their respective cleanup calls. Pending capture state
 * notifications are suppressed; terminal snapshots stay queryable until
 * unsubscribe. Existing capture callbacks must return first. / 应先关闭管理它的
 * Service、释放其会话并销毁 Service，然后关闭 Engine。shutdown 成功会关闭
 * 后端，但不释放 Engine、渲染流或订阅句柄；这些句柄
 * 仍可用于各自的清理调用。待处理的采集状态通知被抑制；终止快照在退订前仍可查询。
 * 在途采集回调必须先返回。
 * @param[in] engine Engine to close; repeated successful shutdown is a no-op. / 待关闭的 Engine；重复成功 shutdown 无操作。
 * @param[in] timeout_ms Shared budget for Capture, render-action and worker-exit
 * waits. Synchronous adapter operations without a timeout follow their own
 * documented finite bounds, so this is not a hard wall-clock return deadline.
 * Zero skips core waits but can still execute those adapter operations;
 * QM_WAIT_FOREVER removes the caller's wait deadline. / Capture、渲染动作和任务
 * 退出等待共用的预算。无超时参数的同步适配器调用另受平台文档规定的有限界限
 * 约束，因此本参数不保证整个调用的严格返回时限。零值不等待核心屏障，但仍可能
 * 执行上述适配器调用；QM_WAIT_FOREVER 不设置调用方等待截止时间。
 * @return QM_EOK after all backend access is quiescent, -QM_ETIMEOUT while
 * cleanup is incomplete, -QM_EBUSY while still bound to a Service or another
 * shutdown is in progress,
 * -QM_EINVAL for an invalid handle, or another negative qm_errno value.
 * / 后端访问全部静止后返回 QM_EOK；清理未完成返回 -QM_ETIMEOUT；仍绑定 Service
 * 或已有 shutdown 在途返回 -QM_EBUSY；句柄无效返回 -QM_EINVAL；其余失败返回负 qm_errno。
 * @note Retain backend contexts and owned memory on error or timeout. Keep
 * admission closed and retry cleanup; do not reopen the route over live DMA.
 * / 出错或超时时保留后端上下文与自有内存。保持准入关闭并重试清理；不要在活跃
 * DMA 之上重开路由。
 */
int qm_audio_engine_shutdown(qm_audio_engine_t *engine, uint32_t timeout_ms);

/**
 * @brief Free an Engine after shutdown. / 在关闭并释放所有依赖句柄后释放 Engine。
 * @param[in] engine Handle to free; invalid only after success. / 待释放句柄；仅成功后才失效。
 * @return QM_EOK, -QM_EBUSY before shutdown or while Service/render/capture
 * references remain, -QM_EINVAL for an invalid handle. Failure retains the
 * handle and its resources; destroy is not an implicit unbounded wait.
 * / 成功返回 QM_EOK；未关闭或仍有 Service/渲染/采集引用返回 -QM_EBUSY；句柄无效
 * 返回 -QM_EINVAL。失败保留句柄及其资源；destroy 不是隐式的无限等待。
 */
int qm_audio_engine_destroy(qm_audio_engine_t *engine);

/**
 * @brief Copy cached physical-path capabilities. / 复制已配置物理路径的缓存能力。
 * @param[in] engine Live Engine handle. / 有效的 Engine 句柄。
 * @param[out] caps Required output; fixed negotiated formats and feature bits. / 必填输出；固定的协商格式与特性位。
 * @return QM_EOK, or -QM_EINVAL for an invalid argument. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL。
 * @note The current descriptor is not a list of every format the chip supports. / 当前描述符不是芯片支持的全部格式列表。
 */
int qm_audio_engine_get_caps(const qm_audio_engine_t *engine, qm_audio_device_caps_t *caps);

/**
 * @brief Copy the fixed native capture format. / 复制固定的原生采集格式。
 * @param[in] engine Live Engine; exclude concurrent destruction. / 有效 Engine；须排除并发销毁。
 * @param[out] format Required output, valid on success. / 必填输出，成功时有效。
 * @return QM_EOK, -QM_ENOSYS if capture is disabled or has no valid profile,
 * -QM_EINVAL for an invalid argument. Includes both PCM and encoded profiles;
 * the copied value owns no backend resources. / 成功返回 QM_EOK；采集禁用或无
 * 有效 profile 返回 -QM_ENOSYS；参数无效返回 -QM_EINVAL。包含 PCM 和编码
 * profile，复制值不持有后端资源。
 */
int qm_audio_engine_get_input_stream_format(const qm_audio_engine_t *engine, qm_audio_stream_format_t *format);

/**
 * @brief Copy the fixed native render format. / 复制固定的原生渲染格式。
 * @param[in] engine Live Engine; exclude concurrent destruction. / 有效 Engine；须排除并发销毁。
 * @param[out] format Required output, valid on success. / 必填输出，成功时有效。
 * @return QM_EOK, -QM_ENOSYS if rendering is disabled or has no valid profile,
 * -QM_EINVAL for an invalid argument. Includes both PCM and encoded profiles;
 * the copied value owns no backend resources. / 成功返回 QM_EOK；播放禁用或无
 * 有效 profile 返回 -QM_ENOSYS；参数无效返回 -QM_EINVAL。包含 PCM 和编码
 * profile，复制值不持有后端资源。
 */
int qm_audio_engine_get_output_stream_format(const qm_audio_engine_t *engine, qm_audio_stream_format_t *format);

/**
 * @brief Open a standalone exclusive render stream. / 打开独立使用的独占渲染流。
 * @param[in] engine Open Engine not owned by a Service. Serialize this call
 * with Service creation and other lifecycle/control calls. / 已打开且未被
 * Service 接管的 Engine；须与 Service 创建及其他生命周期/控制调用串行。
 * @param[in] config Required render options, copied before the call returns. / 必填渲染选项，调用返回前复制。
 * @param[out] out_stream Required output. NULL when no stream was retained.
 * An error or timeout may return a non-NULL stream whose start/cleanup is still
 * in progress; retain it and use only stop followed by close to retry cleanup.
 * Do not write through a stream returned with an error. Successful streams use
 * drain, then stop and close for normal completion, or stop and close for
 * cancellation. / 必填输出；没有保留流时为 NULL。出错或超时可能返回非 NULL 的
 * 流，其启动/清理仍在进行；须保留句柄，仅通过 stop 后 close 重试清理。不得向
 * 出错时返回的流写入。成功打开的流正常结束时依次 drain、stop、close；取消时
 * 依次 stop、close。
 * @return QM_EOK once the backend is ready, -QM_EBUSY if an owner or incomplete
 * stop still occupies the route or a Service owns it, -QM_ETIMEOUT when the
 * fixed start wait expires, -QM_ENOSYS for unsupported rendering, -QM_EINVAL
 * for invalid arguments, or another negative qm_errno value. / 后端就绪后返回
 * QM_EOK；路由已占用、stop 未完成或由 Service 接管返回 -QM_EBUSY；固定启动等待
 * 到期返回 -QM_ETIMEOUT；渲染不支持返回 -QM_ENOSYS；参数无效返回 -QM_EINVAL；
 * 其余失败返回负 qm_errno。
 */
int qm_audio_engine_render_open(qm_audio_engine_t *engine, const qm_audio_render_config_t *config, qm_audio_render_stream_t **out_stream);

/**
 * @brief Submit output media with bounded backpressure. / 以有界背压提交输出媒体。
 *
 * Accepted bytes are copied into the Engine's bounded queue before returning;
 * neither the block nor its data is retained. The resident render worker later
 * writes each accepted block to the platform backend. One producer writes each
 * stream. PCM sizes and accepted counts must align to complete sample frames;
 * encoded byte-stream routes accept arbitrary byte counts. Complete encoded
 * packets use qm_audio_engine_render_write_packet. Large blocks accept a prefix
 * bounded by CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES; retry only the
 * remaining suffix and clear DISCONTINUITY after any prefix is accepted.
 * EOS is applied only when the entire submitted suffix is
 * accepted. It seals input but does not confirm hardware drain.
 * / 已接受的字节在返回前复制进 Engine 的有界队列，块及其数据都不会被保留。常驻
 * 渲染任务随后把每个已接受块写入平台后端。每个流只有一个生产者。PCM 大小与
 * 接受计数须按完整采样帧对齐；编码字节流路由接受任意字节数。完整编码包使用
 * qm_audio_engine_render_write_packet。大块按 CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES
 * 上限接受前缀，只重试未接受的后缀；接受任意前缀后须清除后缀的 DISCONTINUITY。
 * 仅当整个提交后缀都被接受时才提交 EOS。
 * EOS 封闭输入但不确认硬件排空。
 *
 * Only QM_AUDIO_DATA_DISCONTINUITY and QM_AUDIO_DATA_END_OF_STREAM flags are
 * accepted; QM_AUDIO_DATA_NONE denotes ordinary data. Metadata is unused here:
 * set timestamp_ms, duration_ms and sequence to zero and omit validity flags.
 * A zero-byte END_OF_STREAM marker is allowed. DISCONTINUITY requires nonempty
 * media and is rejected on every zero-byte submission, including with EOS.
 * / 仅接受断续与结束标志，普通数据使用 QM_AUDIO_DATA_NONE；此入口不使用元数据，
 * 时间戳、时长和序号置零，不设置有效性标志。允许零字节 END_OF_STREAM 标记。
 * DISCONTINUITY 必须附带非空媒体；任何零字节提交都不得携带它，含同时设置 EOS。
 * @param[in] stream Active, exclusive render stream, not paused or sealed. / 活跃的独占渲染流，未暂停或封闭。
 * @param[in] buffer Borrowed PCM/encoded-byte block or zero-byte marker. / 借用的 PCM/编码字节块或零字节标记。
 * @param[in] timeout_ms Maximum capacity wait, using file-level wait semantics. / 容量等待上限，采用文件级等待语义。
 * @param[out] written Required accepted byte count, zeroed by this function and valid
 * on every return, including interruption after partial acceptance. / 必填的已接受
 * 字节数，由本函数先清零，每次返回（含部分接受后被中断）都有效。
 * @return QM_EOK for accepted bytes/marker, -QM_ETIMEOUT on capacity timeout,
 * -QM_EINTR after stop, -QM_EBUSY in an ineligible state, -QM_EINVAL for
 * invalid data, or another negative qm_errno value. / 接受字节/标记返回 QM_EOK；
 * 容量超时返回 -QM_ETIMEOUT；stop 后返回 -QM_EINTR；状态不符返回 -QM_EBUSY；
 * 数据无效返回 -QM_EINVAL；其余失败返回负 qm_errno。
 */
int qm_audio_engine_render_write(qm_audio_render_stream_t *stream, const qm_audio_data_t *buffer, uint32_t timeout_ms, uint32_t *written);

/**
 * @brief Copy one encoded packet into the bounded render queue. / 原子复制一个编码包到有界渲染队列。
 *
 * QM_EOK accepts the whole packet; an error accepts nothing. End input with
 * render_drain. The caller need not keep input memory alive after return. PCM and
 * encoded byte-stream routes return -QM_ENOSYS; packet routes accept only
 * packets matching the negotiated output profile. The packet must be nonempty.
 * Only TIMESTAMP_VALID, DURATION_VALID and DISCONTINUITY flags are accepted;
 * END_OF_STREAM is invalid here. / QM_EOK 表示整包被接受，出错则一个字节都不接受。
 * 输入结束使用 render_drain。返回后调用方无需保留输入内存。PCM 与编码字节流
 * 路由返回 -QM_ENOSYS；包路由只接受匹配协商输出 profile 的包。包必须非空；仅
 * 接受时间戳有效、时长有效及断续标志，不接受结束标志。
 * @param[in] stream Active, exclusive render stream. / 活跃的独占渲染流。
 * @param[in] packet Non-NULL, nonempty encoded packet. / 非空、非零长度的编码包。
 * @param[in] timeout_ms Capacity wait budget. / 容量等待时长。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_engine_render_write_packet(qm_audio_render_stream_t *stream, const qm_audio_data_t *packet, uint32_t timeout_ms);

/**
 * @brief Seal input and wait for physical output drain. / 封闭输入并等待后端物理输出排空。
 * @param[in] stream Exclusive stream whose producer has finished writing.
 * The calling task reserves device access until drain returns. Ordinary
 * New pause/resume/position calls are rejected during drain; an already
 * accepted position query finishes before device drain starts. Stop may interrupt it.
 * / 生产者已写完的独占流；调用任务独占设备访问直到 drain 返回。普通 pause、
 * resume、位置查询在 drain 期间被拒绝；已受理的位置查询先完成，再开始设备
 * drain。stop 可打断它。
 * @param[in] timeout_ms Total drain budget; zero polls, QM_WAIT_FOREVER waits. / 排空总预算；零值轮询，QM_WAIT_FOREVER 等待。
 * @return QM_EOK only after confirmed device drain, -QM_ENOSYS if unsupported,
 * -QM_ETIMEOUT if incomplete, -QM_EBUSY if another drain/action is active or
 * the stream state is ineligible, -QM_EINTR on stop, or another negative qm_errno.
 * / 确认设备排空后返回 QM_EOK；不支持返回 -QM_ENOSYS；未完成返回 -QM_ETIMEOUT；
 * 已有 drain/动作在途或流状态不符返回 -QM_EBUSY；stop 返回 -QM_EINTR；其余
 * 失败返回负 qm_errno。
 * @note Drain never closes the stream or releases ownership. On timeout the
 * input remains sealed and the handle remains live; retry drain or stop.
 * After successful drain, stop must still finalize the backend run before
 * close can release the stream. / drain 从不关闭流或释放所有权。超时后输入保持
 * 封闭、句柄保持有效；可重试 drain 或 stop。drain 成功后仍须 stop 完成后端运行
 * 的停止屏障，然后才能 close 释放流。
 */
int qm_audio_engine_render_drain(qm_audio_render_stream_t *stream, uint32_t timeout_ms);

/**
 * @brief Pause rendering while preserving media and ownership. / 暂停渲染，保留媒体与所有权。
 * @param[in] stream Active stream; the caller must suspend its producer first. / 活跃流；调用方须先挂起其生产者。
 * @return QM_EOK after pause confirmation, -QM_ENOSYS if not supported,
 * -QM_EBUSY for an incompatible state or active drain, -QM_ETIMEOUT when the
 * fixed action wait expires, or another negative qm_errno value. / 暂停确认后
 * 返回 QM_EOK；不支持返回 -QM_ENOSYS；状态不符或 drain 在途返回 -QM_EBUSY；
 * 固定动作等待到期返回 -QM_ETIMEOUT；其余失败返回负 qm_errno。
 * @note An unsupported preserving pause must not be emulated by stop/drop. / 不支持的保留式暂停不得用 stop/丢弃模拟。
 */
int qm_audio_engine_render_pause(qm_audio_render_stream_t *stream);

/**
 * @brief Resume a preserved render stream. / 恢复保留的渲染流，不重置时间线。
 * @param[in] stream Paused stream that still owns the route. / 仍持有路由的已暂停流。
 * @return QM_EOK after resumption, -QM_ENOSYS if unsupported, -QM_EBUSY in an
 * incompatible state or active drain, -QM_ETIMEOUT when the fixed action wait
 * expires, or another negative qm_errno value. / 恢复后返回 QM_EOK；不支持返回
 * -QM_ENOSYS；状态不符或 drain 在途返回 -QM_EBUSY；固定动作等待到期返回
 * -QM_ETIMEOUT；其余失败返回负 qm_errno。
 */
int qm_audio_engine_render_resume(qm_audio_render_stream_t *stream);

/**
 * @brief Interrupt writes and discard queued output. / 中断写入并安全丢弃剩余输出。
 * @param[in] stream Stream to stop; a successfully stopped stream is a no-op. / 待停止的流；已成功停止的流调用无操作。
 * @return QM_EOK only after in-flight I/O and hardware access have stopped,
 * -QM_ETIMEOUT if the fixed action wait or bounded backend stop expires, or another negative
 * qm_errno value. The handle and route remain retained on failure. / 在途 I/O 与
 * 硬件访问停止后返回 QM_EOK；固定动作等待或后端 stop 到期返回 -QM_ETIMEOUT；其余失败
 * 返回负 qm_errno。失败时句柄与路由仍被保留。
 * @note This call uses CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS for its action
 * wait. It signals backend abort from the caller so a blocked worker or drain
 * can exit. A timeout retains the pending stop and never releases the route;
 * retry stop with the same handle. The backend must provide a documented
 * finite stop bound; it must not wait forever for the producer.
 * STOP_DRAINS devices finish already submitted device media before confirming
 * stop; this is cancellation with a bounded tail, not immediate silence.
 * / 本调用使用 CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS 等待动作完成，并从调用
 * 任务触发后端 abort，让阻塞的 worker 或 drain 得以退出。超时保留待完成 stop
 * 和路由；须用同一句柄重试。后端必须提供文档规定的有限停止上限，不得无限等待
 * 生产者。STOP_DRAINS 设备在确认停止前会先放完已提交的媒体；这是带有限尾部的
 * 取消，不是立即静音。
 */
int qm_audio_engine_render_stop(qm_audio_render_stream_t *stream);

/**
 * @brief Release a stopped render stream without device I/O or waiting. / 释放已停止的渲染流，不执行设备 I/O 或等待。
 * @param[in] stream Handle to close; the caller excludes all concurrent access. / 待关闭句柄；调用方须排除所有并发访问。
 * @return QM_EOK when the owner's reference is released and its handle must
 * no longer be used, -QM_EBUSY unless stop has succeeded and all actions,
 * queued media and device I/O are quiescent, -QM_EINVAL for an invalid handle.
 * Every error preserves the handle. A successful drain alone is insufficient:
 * call stop before close. / 释放所有者引用后返回 QM_EOK，此后调用方不得再访问该
 * 句柄；stop 尚未成功，或仍有动作、排队媒体、设备 I/O 时返回 -QM_EBUSY；句柄
 * 无效返回 -QM_EINVAL。任何失败都保留句柄。仅 drain 成功不足以释放流，必须先
 * stop 再 close。
 */
int qm_audio_engine_render_close(qm_audio_render_stream_t *stream);

/**
 * @brief Query observed playback progress. / 查询从媒体起点开始的已观测播放进度。
 * @param[in] stream Live stream; a preserving pause/resume keeps the same origin. / 有效流；保留式暂停/恢复保持同一起点。
 * @param[out] position_ms Required output in milliseconds, valid on success. / 必填输出（毫秒），成功时有效。
 * @return QM_EOK when observed, -QM_ENOSYS when position cannot be measured,
 * -QM_EBUSY while drain, an incompatible action or another position query is pending,
 * -QM_ETIMEOUT when the fixed query wait expires, -QM_EINVAL for an invalid
 * argument, or another negative qm_errno value. / 观测到返回 QM_EOK；无法测量
 * 返回 -QM_ENOSYS；drain、不兼容动作或另一个位置查询在途返回 -QM_EBUSY；固定查询等待
 * 到期返回 -QM_ETIMEOUT；参数无效返回 -QM_EINVAL；其余失败返回负 qm_errno。
 * @note The device must advertise QM_AUDIO_DEVICE_CAP_POSITION and implement
 * playback_get_position to supply a hardware-consumed observation. Do not
 * substitute the number of bytes queued to the backend. The worker performs
 * queries for active or drained runs; stopped runs return their cached observation.
 * / 设备必须声明
 * QM_AUDIO_DEVICE_CAP_POSITION 并实现 playback_get_position 提供硬件已消费的观测。
 * 不得用排队到后端的字节数代替。活跃或已排空运行由 worker 执行查询；已停止
 * 运行返回缓存的观测值。
 */
int qm_audio_engine_render_get_position(const qm_audio_render_stream_t *stream, uint32_t *position_ms);

/**
 * @brief Set shared output gain through the render worker. / 通过渲染任务设置共享输出增益。
 * @param[in] engine Open Engine; serialize lifecycle and shared-setting calls.
 * Settings wait until a concurrent drain releases device access and do not
 * change stream ownership. / 已打开 Engine；生命周期及共享设置调用须串行。
 * 设置等待并发 drain 释放设备访问，不改变流所有权。
 * @param[in] volume Normalized gain from 0 to 100. / 归一化增益，范围 0 到 100。
 * @return QM_EOK after device confirmation, -QM_ENOSYS if unsupported,
 * -QM_EBUSY if closing or another setting is pending, -QM_ETIMEOUT when the
 * fixed wait expires, -QM_EINVAL for invalid arguments, or another negative
 * qm_errno value. A timeout may leave the setting pending. / 设备确认后返回
 * QM_EOK；不支持返回 -QM_ENOSYS；正在关闭或已有设置在途返回 -QM_EBUSY；固定
 * 等待到期返回 -QM_ETIMEOUT；参数无效返回 -QM_EINVAL；其余失败返回负 qm_errno。
 * 超时后设置仍可能在途。
 */
int qm_audio_engine_set_volume(qm_audio_engine_t *engine, uint8_t volume);
/**
 * @brief Set shared output mute through the render worker. / 通过渲染任务设置共享输出静音。
 * @param[in] engine Open Engine; serialize lifecycle and shared-setting calls.
 * Settings wait until a concurrent drain releases device access. / 已打开
 * Engine；生命周期及共享设置调用须串行；设置等待并发 drain 释放设备访问。
 * @param[in] muted QM_TRUE mutes, QM_FALSE unmutes without changing gain.
 * / QM_TRUE 静音，QM_FALSE 取消静音，不改变已配置增益。
 * @return QM_EOK after device confirmation, -QM_ENOSYS if unsupported,
 * -QM_EBUSY if closing or another setting is pending, -QM_ETIMEOUT when the
 * fixed wait expires, -QM_EINVAL for invalid arguments, or another negative
 * qm_errno value. A timeout may leave the setting pending. / 设备确认后返回
 * QM_EOK；不支持返回 -QM_ENOSYS；正在关闭或已有设置在途返回 -QM_EBUSY；固定
 * 等待到期返回 -QM_ETIMEOUT；参数无效返回 -QM_EINVAL；其余失败返回负 qm_errno。
 * 超时后设置仍可能在途。
 */
int qm_audio_engine_set_mute(qm_audio_engine_t *engine, qm_bool muted);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_ENGINE_H */
