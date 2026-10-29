/**
 * @file qm_audio_device.h
 * @brief Platform PCM and encoded audio contract. / 平台 PCM 与编码音频适配契约。
 *
 * Native audio device adaptation interface consumed by Engine.
 *
 * Unlike an ops-table SPI, this is a fixed named-function interface provided
 * by the platform (same model as hal/qm_fs and adapter/qm_audio_codec.h):
 * the application only includes this header, and exactly one platform
 * implementation is linked into the firmware. Implementations belong in
 * platform/mcu/<mcu>/qm_audio; board-specific pins, routing, and amplifier
 * policy belong in board/<board>/audio. This contract must not expose vendor
 * SDK types or implement business-session arbitration. / 与 ops 表 SPI 不同，这是
 * 平台提供的固定命名函数接口（与 hal/qm_fs、adapter/qm_audio_codec.h 同模式）：
 * 应用只包含本头文件，固件中只链接一个平台实现。实现位于
 * platform/mcu/<mcu>/qm_audio；板级引脚、路由与功放策略位于 board/<board>/audio。
 * 本契约不得暴露厂商 SDK 类型，也不得实现业务会话仲裁。
 *
 * The adapter owns one device instance and keeps its state private. Engine
 * serializes open/close and must stop both directions before close. Failed
 * close retains all state for retry; it never permits a second open.
 * / 适配层拥有唯一设备实例并内部管理状态；Engine 串行调用 open/close，关闭前
 * 须停止两个方向；关闭失败保留状态供重试，不允许再次打开。
 * Operations requiring an open device return -QM_EINIT before open succeeds
 * or after close succeeds. / 依赖已打开设备的操作在 open 成功前或 close 成功后
 * 返回 -QM_EINIT。
 *
 * Engine and Capture call adapter operations from task context, never an ISR. The resident
 * Engine playback worker is the only caller of playback_write. A PULL capture
 * adapter permits one capture_read and one playback_write at a time. A PUSH
 * capture adapter submits input to qm_audio_capture_feed(). Capture producers must
 * transfer ISR/DMA work to task context before submitting data. Abort
 * operations must be safe concurrently with corresponding I/O; they must not
 * acquire a lock held by the blocked read/write. / Engine 和 Capture 从任务上下文调用适配
 * 操作，绝不在 ISR 中调用。常驻 Engine 播放任务独占调用 playback_write。PULL
 * 采集适配器同一时刻只允许一个 capture_read 和一个 playback_write；PUSH 采集
 * 适配器通过 qm_audio_capture_feed() 提交输入。采集生产者必须在提交数据前把 ISR/DMA 工作转移
 * 到任务上下文。abort 操作须与对应 I/O 并发安全，且不得获取被阻塞 read/write
 * 持有的锁。
 *
 * Every adapter operation must be implemented: optional capabilities return
 * -QM_ENOSYS instead of being omitted. All operations return QM_EOK or a
 * negative qm_errno value, never a byte count or a raw SDK error. timeout_ms
 * is milliseconds: zero does not wait, QM_WAIT_FOREVER waits until completion
 * or abort. Operations without a caller timeout must document a finite platform
 * bound. A timeout never proves DMA or callbacks have stopped and never permits
 * premature resource release. / 每个适配操作都必须实现：可选能力返回 -QM_ENOSYS
 * 而非省略。所有操作返回 QM_EOK 或负的 qm_errno 值，绝不返回字节数或原始 SDK
 * 错误。timeout_ms 单位为毫秒：零值不等待，QM_WAIT_FOREVER 等待完成或中断。
 * 无调用方超时的操作必须声明有限的平台界限。超时绝不证明 DMA 或回调已停止，
 * 也绝不允许提前释放资源。
 */
#ifndef QM_AUDIO_DEVICE_H
#define QM_AUDIO_DEVICE_H

#include "qm_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed configuration passed to device open. / 传递给设备打开接口的固定配置。 */
typedef struct {
    /** @brief Capture stream format; an invalid PCM/zero descriptor disables capture, opaque encoded profiles may omit PCM geometry. / 采集流格式；无效 PCM/零描述符禁用采集，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t input_format;
    /** @brief Playback stream format; an invalid PCM/zero descriptor disables playback, opaque encoded profiles may omit PCM geometry. / 播放流格式；无效 PCM/零描述符禁用播放，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t output_format;
    /** @brief Reserved open options; must be zero, not a capability bitmap. / 保留的打开选项；必须为零，不能作为能力位图。 */
    uint32_t flags;
} qm_audio_device_config_t;

/** @brief Supported device operations; each set bit requires a real implementation, not a stub. / 支持的设备操作；每个置位都要求真实可用的实现，不能是占位桩。 */
enum {
    /** @brief Capture start and stop are implemented. / 已实现采集启动和停止。 */
    QM_AUDIO_DEVICE_CAP_CAPTURE = (1U << 0),
    /** @brief Playback start, write, and stop are implemented. / 已实现播放启动、写入和停止。 */
    QM_AUDIO_DEVICE_CAP_PLAYBACK = (1U << 1),
    /** @brief Normalized output gain is supported. / 支持标准化输出音量。 */
    QM_AUDIO_DEVICE_CAP_VOLUME = (1U << 2),
    /** @brief Mute independent of volume is supported. / 支持独立于音量的静音。 */
    QM_AUDIO_DEVICE_CAP_MUTE = (1U << 3),
    /** @brief Playback drain confirms physical output completion. / 播放 drain 可确认物理输出完成。 */
    QM_AUDIO_DEVICE_CAP_DRAIN = (1U << 4),
    /** @brief Capture abort can wake a blocked PULL read or halt PUSH production. / 采集中断可唤醒阻塞的 PULL 读取或停止 PUSH 生产。 */
    QM_AUDIO_DEVICE_CAP_ABORT_READ = (1U << 5),
    /** @brief Playback abort can wake blocked write or drain. / 播放中断可唤醒阻塞的写入或 drain。 */
    QM_AUDIO_DEVICE_CAP_ABORT_WRITE = (1U << 6),
    /** @brief Rendering supports consumed-position observation. / 播放支持已消费位置查询。 */
    QM_AUDIO_DEVICE_CAP_POSITION = (1U << 7),
    /** @brief Stop must finish accepted media before becoming idle; no fast discard. / 停止须先放完已接收媒体再进入空闲，不允许快速丢弃。 */
    QM_AUDIO_DEVICE_CAP_STOP_DRAINS = (1U << 8),
};

/**
 * @brief Negotiated format and operation set of an opened device. / 已打开设备协商得到的格式与操作集合。
 *
 * CAPTURE and PLAYBACK together do not prove simultaneous duplex support. This
 * descriptor has no duplex, clock-precision, or position-query field. An
 * unsupported simultaneous combination must fail explicitly at start. The
 * PULL/PUSH adapter strategy is selected by the generic qm_audio Kconfig at
 * build time, not advertised as a hardware capability here. / CAPTURE 与
 * PLAYBACK 同时置位不代表支持同时双工。本描述符没有双工、时钟精度或位置查询
 * 字段；不支持的同时组合必须在启动时显式失败。PULL/PUSH 适配策略由通用
 * qm_audio Kconfig 在构建时选择，不在此作为硬件能力声明。
 */
typedef struct {
    /** @brief Actual capture format; all zero when disabled. / 实际采集格式；禁用时为全零。 */
    qm_audio_stream_format_t input_format;
    /** @brief Actual playback format; all zero when disabled. / 实际播放格式；禁用时为全零。 */
    qm_audio_stream_format_t output_format;
    /** @brief Bitwise QM_AUDIO_DEVICE_CAP_* combination. / QM_AUDIO_DEVICE_CAP_* 的按位组合。 */
    uint32_t flags;
} qm_audio_device_caps_t;

/**
 * @brief Open shared device resources without starting I/O. / 打开共享设备资源但不启动 I/O。
 *
 * ctx receives the opaque device instance created by the adapter. The caller
 * passes it to every subsequent operation and to close. The adapter owns its
 * private state until close succeeds. A second open returns -QM_EBUSY.
 * / ctx 接收适配层创建的不透明设备实例；调用方将其传给后续操作和 close。适配层
 * 持有内部状态直到 close 成功；重复打开返回 -QM_EBUSY。
 * @param[out] ctx Required output; set to NULL on failure. / 必填输出参数；失败时置 NULL。
 * @param[in] config Borrowed for this call; copy values needed afterwards. / 本次调用期间借用；后续需要的值必须复制。
 * @return QM_EOK, -QM_EINVAL for invalid formats, -QM_ENOSYS for unsupported
 * configuration, or another negative qm_errno value. Failed open must unwind
 * its partially acquired resources. / 成功返回 QM_EOK；格式无效返回 -QM_EINVAL；
 * 配置不支持返回 -QM_ENOSYS；其余失败返回负 qm_errno 值。open 失败须回滚已部分
 * 获取的资源。
 */
int qm_audio_device_open(void **ctx, const qm_audio_device_config_t *config);

/**
 * @brief Close the idle device. / 关闭空闲设备。
 *
 * Release private state after both directions have stopped. Closing an
 * unopened device is a no-op. / 在两个方向停止后释放内部状态。关闭未打开的
 * 设备无操作。
 * @param[in] ctx Device instance returned by open. / open 返回的设备实例。
 * @return QM_EOK only when no DMA, I/O, or SDK callback can reference it,
 * -QM_EBUSY/-QM_ETIMEOUT if still live, or another negative QUMI error. Never
 * free context on an error return. / 仅在无 DMA、I/O 或 SDK 回调可引用它时返回
 * QM_EOK；仍活跃返回 -QM_EBUSY/-QM_ETIMEOUT；其余失败返回负的 QUMI 错误码。
 * 错误返回时绝不可释放 context。
 */
int qm_audio_device_close(void *ctx);

/**
 * @brief Query device capabilities without changing state. / 查询设备能力，不改变设备状态。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[out] caps Caller-owned output, valid on success. / 调用方输出，成功时有效。
 * @return QM_EOK or a negative qm_errno value. / 返回 QM_EOK 或负的 qm_errno 错误码。
 */
int qm_audio_device_get_caps(void *ctx, qm_audio_device_caps_t *caps);

/**
 * @brief Start playback using the fixed native format. / 使用固定原生格式启动播放。
 *
 * Start a new output run using output_format saved by open(). The device must
 * be open with no active playback run. / 使用 open() 保存的 output_format 启动
 * 新一轮播放。设备须已打开且没有活跃播放。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[in] delivery Accepted-media preservation or latency policy. / 已接收媒体的保留或延迟策略。
 * @return QM_EOK once ready, -QM_EBUSY for occupied/shared resources,
 * -QM_ENOSYS for unsupported delivery or disabled playback, or another negative
 * QUMI error. / 就绪返回 QM_EOK，资源占用返回 -QM_EBUSY，不支持策略或未启用
 * 播放返回 -QM_ENOSYS，其他失败返回负的 QUMI 错误码。
 */
int qm_audio_device_playback_start(void *ctx, qm_audio_delivery_t delivery);

/**
 * @brief Write one output media unit with bounded backpressure. / 以有界背压写入一个输出媒体单元。
 *
 * The fixed output_format selects PCM, encoded bytes (max_packet_bytes == 0),
 * or complete encoded packets (max_packet_bytes > 0). QM_EOK means all
 * input->size bytes were accepted, not that physical playback has completed.
 * The adapter handles partial underlying writes internally, preserving PCM
 * sample-frame alignment and encoded packet boundaries. / 固定 output_format
 * 决定 PCM、编码字节流或完整编码包；QM_EOK 表示全部 input->size 字节已接收，
 * 不表示物理播放完成。适配层内部处理底层部分写入，保持 PCM 采样帧对齐和编码
 * 包边界。
 *
 * Input data must be non-NULL and size nonzero. PCM and byte-stream chunks set
 * timestamp_ms, duration_ms and sequence to zero; their only permitted flag is
 * DISCONTINUITY on the first chunk after a gap. Complete packets may
 * use TIMESTAMP_VALID, DURATION_VALID and DISCONTINUITY; END_OF_STREAM is
 * never accepted here. / data 必须非空且 size 非零；PCM 和字节流块的时间戳、
 * 时长和序号置零，仅允许在断续后的首块设置 DISCONTINUITY。完整编码包可带
 * 时间戳有效、时长有效和断续标志；本接口
 * 不接收 EOS。
 *
 * Any error, including timeout, must end the current playback run. The caller
 * must not retry this unit: some data may already have reached the device, and
 * an error neither reports progress nor rolls back accepted data. Requires an
 * active playback run. / 任何错误（包括超时）都必须终止当前播放；调用方不能
 * 重试该单元，因为部分数据可能已到达设备，错误既不报告进度，也不回滚已接收
 * 数据。要求当前存在活跃播放。
 * @param[in] ctx Open device instance with an active playback run. / 已打开且播放活跃的设备实例。
 * @param[in] input Borrowed descriptor and bytes; copy accepted data into owned
 * storage before returning. Never retain input/data, including for DMA.
 * / 借用的描述符和字节；返回前复制已接收数据，不得保留 input/data 供 DMA 等使用。
 * @param[in] timeout_ms Total wait budget for the entire unit, including all
 * internal writes, using file-level timeout semantics. / 按文件级超时语义，整个
 * 单元共用的总等待预算，包含所有内部写入。
 * @return QM_EOK on full acceptance, -QM_ETIMEOUT if incomplete at the deadline,
 * -QM_EINTR after abort, or another negative qm_errno value. / 接收成功返回
 * QM_EOK，超时返回 -QM_ETIMEOUT，中断返回 -QM_EINTR，其他失败返回负的 QUMI 错误码。
 */
int qm_audio_device_playback_write(void *ctx, const qm_audio_data_t *input, uint32_t timeout_ms);

/**
 * @brief Wait for accepted media to finish at the physical output boundary. / 等待已接收媒体到达物理输出完成点。
 *
 * Wait for previously accepted PCM or encoded media to finish at the device
 * boundary. The playback producer must be quiescent. / 等待先前已接受的 PCM 或
 * 编码媒体在设备边界完成。播放生产者须已静止。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[in] timeout_ms Total drain budget; zero polls and QM_WAIT_FOREVER waits. / 总 drain 等待时长；零轮询，QM_WAIT_FOREVER 持续等待。
 * @return QM_EOK only on confirmed output completion, -QM_ENOSYS without
 * trustworthy confirmation, -QM_ETIMEOUT if incomplete, -QM_EINTR after abort,
 * or another negative QUMI error. Drain does not free context. Do not equate a
 * software queue being empty with DAC completion; document the device
 * observation point. / 仅在确认输出完成后返回 QM_EOK；无可信确认返回 -QM_ENOSYS；
 * 未完成返回 -QM_ETIMEOUT；中断返回 -QM_EINTR；其余失败返回负的 QUMI 错误码。
 * drain 不释放 context。不要将软件队列为空等同于 DAC 完成，须说明设备观测点。
 */
int qm_audio_device_playback_drain(void *ctx, uint32_t timeout_ms);

/**
 * @brief Pause output without releasing accepted media. / 暂停输出但不释放已接收的媒体。
 *
 * Pause output without dropping accepted media or releasing ownership.
 * Requires active playback with a quiescent producer. / 暂停输出，不丢弃已接收
 * 媒体，也不释放所有权。要求播放活跃且生产者已静止。
 * @param[in] ctx Open device instance with active playback. / 已打开且播放活跃的设备实例。
 * @return QM_EOK after confirmation, -QM_ENOSYS if preserving pause is not
 * supported, or another negative QUMI error. No stop/replay approximation.
 * / 确认后返回 QM_EOK；不支持保留式暂停返回 -QM_ENOSYS；其余失败返回负的 QUMI
 * 错误码。不得用 stop/重放近似。
 */
int qm_audio_device_playback_pause(void *ctx);

/**
 * @brief Resume preserved output without resetting media position. / 恢复保留的输出，不重置媒体位置。
 *
 * Resume preserved output without resetting the media position. Requires
 * paused playback. / 恢复保留的输出，不重置媒体位置。要求播放已暂停。
 * @param[in] ctx Open device instance with paused playback. / 已打开且播放已暂停的设备实例。
 * @return QM_EOK after confirmation, -QM_ENOSYS if unsupported, or another
 * negative qm_errno value. / 确认后返回 QM_EOK；不支持返回 -QM_ENOSYS；其余失败
 * 返回负的 qm_errno 值。
 */
int qm_audio_device_playback_resume(void *ctx);

/**
 * @brief Interrupt a blocked playback write or drain. / 中断阻塞的播放写入或 drain。
 *
 * Interrupt a concurrent playback_write or playback_drain promptly. This is
 * not a stop barrier and does not free buffers; playback_stop must still
 * confirm quiescence. / 及时中断并发的 playback_write 或 playback_drain。这不是
 * stop 屏障，也不释放缓冲；静止仍须由 playback_stop 确认。
 * @return QM_EOK once interruption is signalled, -QM_ENOSYS if unsupported, or
 * another negative qm_errno value. / 中断信号发出后返回 QM_EOK；不支持返回
 * -QM_ENOSYS；其余失败返回负的 qm_errno 值。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 */
int qm_audio_device_playback_abort(void *ctx);

/**
 * @brief Stop playback and discard unplayed output. / 停止播放并丢弃未播放的输出。
 *
 * Stop playback and discard accepted output that has not played. A device
 * advertising STOP_DRAINS instead finishes accepted media before confirming
 * idle; callers must not promise immediate silence on that route. Repeat stop
 * is a no-op once stopped. / 停止播放并丢弃已接受但未播放的输出。声明 STOP_DRAINS
 * 的设备会先放完已接收媒体再确认空闲；调用方不得在该路由上承诺立即静音。停止
 * 后重复调用无操作。
 * @return QM_EOK only after device I/O and playback callbacks are quiescent,
 * -QM_ETIMEOUT if the documented stop bound expires, or another negative QUMI
 * error. Keep context and pending resources allocated on failure. Must preserve
 * capture resources still in use by the input direction. / 设备 I/O 与播放回调
 * 静止后返回 QM_EOK；文档规定的停止上限到期返回 -QM_ETIMEOUT；其余失败返回负
 * 的 QUMI 错误码。失败时保留 context 与待处理资源，且必须保留输入方向仍在
 * 使用的采集资源。
 * @param[in] ctx Open device instance with active playback. / 已打开且播放活跃的设备实例。
 */
int qm_audio_device_playback_stop(void *ctx);

/**
 * @brief Query media consumed from the playback origin. / 查询从播放起点开始已消费的媒体位置。
 *
 * Query consumed media position from the current playback origin. Valid for
 * active or stopped playback on an open device. / 查询从当前播放起点开始已消费
 * 的媒体位置；适用于已打开设备的活跃或已停止播放。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[out] position_ms Required consumed position in milliseconds. / 必填的已消费位置输出（毫秒）。
 * @return QM_EOK when observed, -QM_ENOSYS when unavailable, or another negative
 * QUMI error. Queued-byte estimates are not valid. / 观测到返回 QM_EOK；不可用
 * 返回 -QM_ENOSYS；其余失败返回负的 QUMI 错误码。排队字节的估算值无效。
 */
int qm_audio_device_playback_get_position(void *ctx, uint32_t *position_ms);

/**
 * @brief Start the single hardware capture run. / 启动唯一的硬件采集运行。
 *
 * Start the single physical capture run using input_format saved by open() and
 * the build-selected adapter mode. / 使用 open() 保存的 input_format 和构建时
 * 选择的适配模式启动唯一的物理采集。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @return QM_EOK once ready, -QM_EBUSY for an unsupported simultaneous resource
 * use, -QM_ENOSYS if capture is unsupported or disabled, or another negative
 * QUMI error. / 就绪返回 QM_EOK，资源并发使用不受支持时返回 -QM_EBUSY，不支持
 * 或未启用采集返回 -QM_ENOSYS，其他失败返回负的 QUMI 错误码。
 */
int qm_audio_device_capture_start(void *ctx);

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
/**
 * @brief Return one borrowed capture block or packet. / 返回一个借用的采集块或编码包。
 *
 * One reader at a time. Failures use the return value, not input metadata.
 * Success returns a nonempty sample-frame-aligned PCM block or one complete
 * encoded packet matching input_format. Metadata uses QM_AUDIO_DATA_*;
 * END_OF_STREAM is forbidden and valid timestamps use the capture clock.
 * / 同一时间只有一个读取者；失败通过返回值报告，不写入 input 元数据。成功返回
 * 匹配 input_format 的非空完整采样帧 PCM 块或一个完整编码包；元数据使用
 * QM_AUDIO_DATA_*，禁止 EOS，有效时间戳使用采集时钟。
 * @param[out] input Caller-owned output. On success its data remains stable
 * until the next read or capture_stop; Capture copies it before either
 * operation. Concurrent abort must not invalidate or change returned data.
 * On failure data is NULL and size is zero. / 调用方自有输出；成功时 data 在下一
 * 次 read 或 capture_stop 前保持稳定，Capture 在此之前完成复制。并发 abort 不得
 * 使已返回数据失效或改变；失败时 data 为 NULL，size 为零。
 * @param[in] ctx Open device instance with active capture. / 已打开且采集活跃的设备实例。
 * @param[in] timeout_ms Wait budget; zero polls and QM_WAIT_FOREVER waits. / 等待时长；零轮询，QM_WAIT_FOREVER 持续等待。
 * @return QM_EOK on capture, -QM_ETIMEOUT if no data before the deadline,
 * -QM_EINTR after abort, or another negative qm_errno value. / 采集到返回 QM_EOK；
 * 截止时间前无数据返回 -QM_ETIMEOUT；中断后返回 -QM_EINTR；其余失败返回负的
 * qm_errno 值。
 */
int qm_audio_device_capture_read(void *ctx, qm_audio_data_t *input, uint32_t timeout_ms);
#endif /* CONFIG_QM_AUDIO_CAPTURE_MODE_PULL */

/**
 * @brief Interrupt a blocked capture read. / 中断阻塞的采集读取。
 *
 * Interrupt a concurrent PULL capture_read or request a PUSH producer to stop.
 * Do not free or overwrite a PULL buffer already returned to Capture.
 * / 中断并发的 PULL capture_read 或请求 PUSH 生产者停止；不得释放或改写已返回
 * 给 Capture 的 PULL 缓冲区。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @return QM_EOK once interruption is signalled, -QM_ENOSYS if unsupported, or
 * another negative qm_errno value. capture_stop remains required to confirm the
 * device no longer accesses input buffers or ingress. / 中断信号发出后返回 QM_EOK；
 * 不支持返回 -QM_ENOSYS；其余失败返回负的 qm_errno 值。仍须 capture_stop 确认
 * 设备不再访问输入缓冲或入口。
 */
int qm_audio_device_capture_abort(void *ctx);

/**
 * @brief Stop capture while retaining resources needed for playback. / 停止采集，同时保留播放所需资源。
 *
 * Stop capture while retaining resources still needed by playback. Capture
 * calls stop after its PULL read and data copy have finished. In PUSH mode,
 * success also guarantees all in-flight feed calls have returned and the old
 * producer cannot submit again. Capture also calls stop after a failed start;
 * tolerate a partially started or already stopped run. A new start waits for
 * stop success.
 * / 停止采集，同时保留播放仍需要的资源。Capture 在 PULL 读取与数据复制结束后
 * 调用 stop。PUSH 模式下成功还须保证所有在途 feed 调用已返回，且旧生产者不会
 * 再次提交。Capture 在启动失败后也会调用 stop；须允许部分启动或已经停止的状态。
 * 只有停止成功后才能启动下一轮采集。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @return QM_EOK only after input I/O/DMA/callbacks are quiescent,
 * -QM_ETIMEOUT if the documented stop bound expires, or another negative QUMI
 * error. Failure retains resources and does not imply they are safe to free.
 * / 输入 I/O/DMA/回调静止后返回 QM_EOK；文档规定的停止上限到期返回 -QM_ETIMEOUT；
 * 其余失败返回负的 QUMI 错误码。失败保留资源，不代表可以安全释放。
 */
int qm_audio_device_capture_stop(void *ctx);

/**
 * @brief Apply normalized output volume. / 应用标准化输出音量。
 *
 * Apply normalized output gain without resetting mute or the stream. Requires
 * an open output-capable device. / 应用标准化输出增益，不重置静音或流。要求
 * 设备已打开且支持输出。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[in] volume Gain from 0 to 100 inclusive. / 0 到 100 的增益值，含边界。
 * @return QM_EOK, -QM_EINVAL out of range, -QM_ENOSYS if unsupported, or another
 * negative qm_errno value. / 成功返回 QM_EOK；越界返回 -QM_EINVAL；不支持返回
 * -QM_ENOSYS；其余失败返回负的 qm_errno 值。
 */
int qm_audio_device_set_volume(void *ctx, uint8_t volume);

/**
 * @brief Set mute independently of gain and ownership. / 独立于增益和输出所有权设置静音。
 *
 * Change mute independently of configured gain and output ownership. Requires
 * an open output-capable device. / 独立于已配置增益和输出所有权切换静音。要求
 * 设备已打开且支持输出。
 * @param[in] ctx Open device instance. / 已打开的设备实例。
 * @param[in] muted QM_TRUE mutes, QM_FALSE unmutes. / QM_TRUE 静音，QM_FALSE 取消静音。
 * @return QM_EOK, -QM_ENOSYS if unsupported, or another negative qm_errno value.
 * / 成功返回 QM_EOK；不支持返回 -QM_ENOSYS；其余失败返回负的 qm_errno 值。
 */
int qm_audio_device_set_mute(void *ctx, qm_bool muted);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_DEVICE_H */
