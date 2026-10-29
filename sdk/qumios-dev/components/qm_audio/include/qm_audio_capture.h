/**
 * @file qm_audio_capture.h
 * @brief Independent native capture and subscriptions. / 独立的原生采集与订阅。
 *
 * Capture borrows an open device and owns its reader and callback tasks. It
 * does not depend on Engine, Service, Pipeline or the shared audio event task.
 * PULL creates one reader on demand; PUSH uses the device producer. One lazy dispatcher
 * serializes callbacks outside capture locks. Slow callbacks can delay other
 * callbacks, but never block the reader or QUEUE consumers. Use QUEUE for
 * encoding or other lengthy work. No format conversion is performed. QUEUE
 * without state_cb creates no dispatcher, but PULL still needs its reader.
 * / Capture 借用已打开设备，自主管理读取和回调任务，不依赖 Engine、Service、
 * Pipeline 或共享音频事件任务。PULL 按需创建唯一读取任务，PUSH 使用设备生产者。
 * 独立分发任务在锁外串行执行回调；慢回调会延迟其他回调，但不阻塞读取任务或
 * QUEUE 消费者。纯 QUEUE 且无 state_cb 时不创建分发任务；PULL 仍需要读取任务。
 * 编码等耗时处理使用 QUEUE；本模块不进行格式转换。
 *
 * Data is nonempty PCM aligned to complete sample frames or one complete native
 * encoded packet. Only TIMESTAMP_VALID, DURATION_VALID and DISCONTINUITY flags
 * are accepted; END_OF_STREAM is not a capture unit. Valid timestamps retain
 * the source capture clock. / 数据为非空且按完整采样帧对齐的 PCM 或一个原生完整
 * 编码包。仅接受 TIMESTAMP_VALID、DURATION_VALID 和 DISCONTINUITY 标志，
 * END_OF_STREAM 不是采集单元；有效时间戳保留源采集时钟。
 *
 * All calls require task context. Serialize create/shutdown/destroy and exclude
 * new handle access before destroy. Serialize enable/flush/unsubscribe for a
 * subscription; unsubscribe may interrupt its one blocked receiver. Do not
 * call subscribe, enable, shutdown, destroy, unsubscribe or recv from a Capture
 * callback; query and flush are allowed. Conflicting subscribe, enable,
 * shutdown or unsubscribe calls on one Capture return -QM_EBUSY.
 * / 所有接口要求任务上下文。create/shutdown/destroy 须串行，destroy 前排除新的
 * 句柄访问。同一订阅的 enable/flush/unsubscribe 须串行；退订可打断唯一阻塞接收者。
 * 不得在 Capture 回调中调用 subscribe、enable、shutdown、destroy、unsubscribe
 * 或 recv；允许查询和 flush。同一 Capture 的 subscribe、enable、shutdown、
 * unsubscribe 生命周期操作发生冲突时返回 -QM_EBUSY。
 */
#ifndef QM_AUDIO_CAPTURE_H
#define QM_AUDIO_CAPTURE_H

#include "qm_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque Capture instance. / 不透明的 Capture 实例。 */
typedef struct qm_audio_capture qm_audio_capture_t;

/**
 * @brief Query the immutable maximum delivery size. / 查询固定的最大交付字节数。
 * @param[in] capture Live Capture, externally protected from destruction. / 有效 Capture；调用方须排除并发销毁。
 * @param[out] bytes Required output capacity for any native block/packet. / 接收任意原生块/包所需的缓冲容量。
 * @return QM_EOK or -QM_EINVAL. / 成功或参数错误。
 */
int qm_audio_capture_get_frame_bytes(const qm_audio_capture_t *capture, uint32_t *bytes);

/** @brief Fixed native source and bounded storage configuration. / 固定原生输入和有界存储配置。 */
typedef struct {
    /** @brief Borrowed open device, valid through successful shutdown; never closed by Capture. / 借用的已打开设备，须保持到 shutdown 成功；Capture 不关闭它。 */
    void *device_ctx;
    /** @brief Copied native PCM or complete-packet profile matching device open. / 复制且匹配 device open 的原生 PCM 或完整编码包格式。 */
    qm_audio_stream_format_t input_format;
    /** @brief Nonzero maximum bytes per block; PCM is sample-frame-aligned and packets fit max_packet_bytes. / 非零的单块最大字节数；PCM 按采样帧对齐，编码包至少容纳 max_packet_bytes。 */
    uint32_t frame_bytes;
    /** @brief Default records per subscription; zero uses CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN. / 每订阅默认记录数；零使用 CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN。 */
    uint32_t queue_len;
} qm_audio_capture_open_config_t;

/** @brief Delivery state. / 交付状态。 */
typedef enum {
    /** @brief Subscription delivery is disabled. / 订阅交付已禁用。 */
    QM_AUDIO_CAPTURE_DISABLED = 0,
    /** @brief Subscription accepts native capture data. / 订阅接收原生采集数据。 */
    QM_AUDIO_CAPTURE_RUNNING,
    /** @brief Terminal normal stop; unsubscribe to release. / 正常停止终态；须退订释放。 */
    QM_AUDIO_CAPTURE_STOPPED,
    /** @brief Terminal failure; result retains the first cause until unsubscribe. / 故障终态；result 保留首次原因直到退订。 */
    QM_AUDIO_CAPTURE_ERROR,
} qm_audio_capture_state_t;

/** @brief Copied subscriber state. / 复制的订阅状态。 */
typedef struct {
    /** @brief Current delivery state. / 当前交付状态。 */
    qm_audio_capture_state_t state;
    /** @brief Original negative QUMI error in ERROR; QM_EOK otherwise. / ERROR 时为原始负 QUMI 错误，其余为 QM_EOK。 */
    int result;
    /** @brief Effective record capacity, including a configured zero default. / 实际记录容量，包含配置为零时选用的默认值。 */
    uint32_t queue_len;
} qm_audio_capture_snapshot_t;

/**
 * @brief Latest-state callback on Capture's dispatcher. / Capture 分发任务上的最新状态回调。
 *
 * Intermediate states may coalesce; terminal state remains queryable until
 * unsubscribe. Shutdown may discard pending terminal callbacks; query the
 * retained snapshot when needed. State and media callbacks are serialized
 * without Capture locks, rotating between ready subscriptions.
 * snapshot is borrowed only for this call; retain user_ctx until unsubscribe
 * succeeds. Return promptly and use a consumer task for lengthy work.
 * / 中间状态可能合并；终态保留到退订。shutdown 不保证交付尚未执行的终态回调，
 * 需要时查询保留快照。状态与媒体回调在 Capture 锁外轮转选择就绪订阅并串行执行。
 * snapshot 仅本次调用借用；user_ctx 保持到退订成功。回调须及时返回，
 * 耗时工作交由消费者任务。
 */
typedef void (*qm_audio_capture_state_cb_t)(const qm_audio_capture_snapshot_t *snapshot, void *user_ctx);

/** @brief Copied subscriber configuration; output is always native. / 复制的订阅配置；输出始终是原生格式。 */
typedef struct {
    /** @brief Bounded record capacity; zero selects the Capture instance default. / 有界记录容量；零使用 Capture 实例默认值。 */
    uint32_t queue_len;
    /** @brief Initial delivery gate; the first enabled subscriber starts capture. / 初始交付开关；首个启用的订阅者启动采集。 */
    qm_bool enabled;
    /** @brief Fixed QUEUE or CALLBACK mode for this subscription. / 本订阅固定使用 QUEUE 或 CALLBACK 模式。 */
    qm_audio_capture_delivery_t delivery;
    /** @brief Required only for native PCM CALLBACK; data is borrowed until return. / 仅原生 PCM CALLBACK 必填；数据借用到回调返回。 */
    qm_audio_capture_frame_cb_t frame_cb;
    /** @brief Required only for native encoded CALLBACK; mutually exclusive with frame_cb. / 仅原生编码 CALLBACK 必填；与 frame_cb 互斥。 */
    void (*packet_cb)(const qm_audio_data_t *packet, void *user_ctx);
    /** @brief Borrowed callback context, valid through successful unsubscribe; may be NULL. / 借用回调上下文，须保持到退订成功；可为 NULL。 */
    void *user_ctx;
    /** @brief Optional state callback independent of delivery mode; may run before subscribe returns. / 独立于交付模式的可选状态回调；可能在订阅返回前执行。 */
    qm_audio_capture_state_cb_t state_cb;
} qm_audio_capture_config_t;

/**
 * @brief Create Capture; reader/callback tasks are lazy and device I/O remains stopped.
 * / 创建 Capture；读取/回调任务按需创建，不启动设备 I/O。
 * @param[in] config Copied configuration with a borrowed open device. / 复制的配置，包含借用的已打开设备。
 * @param[out] out_capture Required owned-handle output; NULL on failure. Release
 * with shutdown followed by destroy. / 必填的自有句柄输出；失败为 NULL；须先 shutdown 再 destroy。
 * In PUSH builds the linked device exposes a single ingress. One Capture owns
 * that slot from create to destroy, including while stopped. / PUSH 构建中链接
 * 设备提供唯一入口；一个 Capture 从 create 到 destroy 持有该槽位，停止时也不释放。
 * @return QM_EOK, -QM_EINVAL for invalid configuration/profile, -QM_ENOSYS when
 * Capture is disabled, -QM_EBUSY for an occupied PUSH slot, or a negative
 * allocation/OS error. / 成功返回 QM_EOK；配置/格式无效返回 -QM_EINVAL；Capture
 * 禁用返回 -QM_ENOSYS；PUSH 槽位占用返回 -QM_EBUSY；其余为负的分配/OS 错误。
 */
int qm_audio_capture_create(const qm_audio_capture_open_config_t *config, qm_audio_capture_t **out_capture);

/**
 * @brief Close admission and stop reader, device and callbacks. / 关闭准入并停止读取、设备和回调。
 *
 * Successful shutdown is terminal and repeatable. Existing subscriptions remain
 * queryable and require unsubscribe; the borrowed device may be closed only
 * after this barrier succeeds. ERROR subscriptions retain their first error;
 * pending terminal callbacks need not be delivered. On failure retain the
 * handle/device and retry with the same handle.
 * Shutdown wakes receivers but does not wait for recv() to return; unsubscribe
 * provides that subscription lifetime barrier.
 * / 成功关闭为终态且可重复调用。已有订阅仍可查询并须退订；只有此屏障成功后才能
 * 关闭借用设备。ERROR 订阅保留首次错误，不保证交付待处理的终态回调。
 * 失败时保留句柄和设备，用同一句柄重试。
 * shutdown 唤醒接收者，但不等待 recv() 返回；订阅接收者的静止屏障由 unsubscribe 提供。
 * The timeout bounds Capture's waits; the synchronous device stop additionally
 * follows the adapter's finite stop bound. / 超时限制 Capture 自身的等待；同步
 * device stop 另受适配器约定的有限停止时限约束。
 * @param[in] capture Live Capture instance; NULL is a no-op. / 有效 Capture 实例；NULL 无操作。
 * @param[in] timeout_ms Wait budget in milliseconds; zero polls and QM_WAIT_FOREVER waits. / 等待预算毫秒；零轮询，QM_WAIT_FOREVER 持续等待。
 * @return QM_EOK after quiescence, -QM_EBUSY for conflicting lifecycle/callback
 * context, -QM_ETIMEOUT if incomplete, or another negative QUMI error.
 * / 静止后返回 QM_EOK；生命周期冲突或回调上下文返回 -QM_EBUSY；未完成返回
 * -QM_ETIMEOUT；其余返回负 QUMI 错误。
 */
int qm_audio_capture_shutdown(qm_audio_capture_t *capture, uint32_t timeout_ms);

/**
 * @brief Free a successfully shut-down Capture with no subscriptions. / 释放已关闭且无订阅的 Capture。
 * @param[in] capture Handle invalidated only on success; NULL is a no-op; external ownership
 * excludes concurrent access. / 仅成功后失效的句柄；NULL 无操作；外部所有者须排除并发访问。
 * @return QM_EOK, or -QM_EBUSY until shutdown/subscription cleanup completes
 * or while another lifecycle call is active. / 成功返回 QM_EOK；关闭/订阅清理
 * 未完成或另一个生命周期调用活跃时返回 -QM_EBUSY。
 */
int qm_audio_capture_destroy(qm_audio_capture_t *capture);

/**
 * @brief Subscribe to one explicit Capture. / 订阅指定 Capture。
 *
 * Each subscriber has independent copied storage. A full queue drops the oldest
 * record; sequence gaps expose loss. No resampling or encoding occurs. All
 * subscribers share one physical source. Prepare callback context before this
 * call because callbacks may start before it returns. QUEUE mode requires both
 * media callbacks NULL; CALLBACK mode requires exactly the callback matching
 * the native format. / 每订阅者拥有独立复制存储。队列满时丢最旧记录，序号间隙暴露
 * 丢失。不重采样或编码，全部订阅者共享唯一物理源。回调可能在返回前启动，须先
 * 准备上下文。QUEUE 要求两个媒体回调均为 NULL；CALLBACK 仅提供匹配原生格式的回调。
 * A new enabled subscription may restart a failed source; existing terminal
 * subscriptions retain their error and require recreation. / 新建启用订阅可
 * 重启故障源；已有终态订阅保留原错误，须退订后重建。
 * Starting the source is synchronous: this call returns the device-start or
 * reader-creation error. An enabled subscription is published only after
 * successful start, so early PUSH data is not delivered to that new subscription.
 * Source failure also terminates disabled subscriptions.
 * / 启动源为同步操作：本调用返回设备启动或读取任务创建错误。
 * 启用的新订阅仅在启动成功后发布，启动返回前的 PUSH 数据不交付给该新订阅。
 * 源故障也会使已禁用的订阅进入 ERROR 终态。
 * @param[in] capture Open Capture instance. / 已打开的 Capture 实例。
 * @param[in] config Copied options; callback context remains borrowed. / 复制的选项；回调上下文仍为借用。
 * @param[out] out_subscription Owned output, NULL on failure. Unsubscribe even
 * after Capture shutdown or terminal source failure. / 自有输出，失败为 NULL；
 * Capture 关闭或源故障进入终态后仍须退订。
 * @return QM_EOK, -QM_EINVAL for invalid options, -QM_ENOSYS for NULL Capture, -QM_EBUSY while closing or lifecycle-busy,
 * -QM_EFULL/-QM_ENOMEM on exhausted resources, or another negative QUMI error.
 * / 成功返回 QM_EOK；选项无效返回 -QM_EINVAL；Capture 为 NULL 返回 -QM_ENOSYS；关闭中或生命周期冲突返回 -QM_EBUSY；资源耗尽
 * 返回 -QM_EFULL/-QM_ENOMEM；其余返回负 QUMI 错误。
 */
int qm_audio_capture_subscribe(qm_audio_capture_t *capture, const qm_audio_capture_config_t *config, qm_audio_capture_subscription_t **out_subscription);

/**
 * @brief Gate one subscription's delivery. / 门控单个订阅的交付。
 *
 * Disabling clears queued media and interrupts recv with -QM_EBUSY without
 * stopping the physical source. Enabling may synchronously start the source
 * and returns its start error; repeated enable(true) preserves queued media. Terminal
 * subscriptions require unsubscribe and recreation. This is not a callback
 * quiescence barrier; context stays alive through unsubscribe. / 禁用清空队列并
 * 以 -QM_EBUSY 打断 recv，不停止物理源；启用可能同步启动源并返回启动错误。
 * 重复 enable(true) 保留排队数据。终态订阅须退订后重建。
 * 本调用不是回调静止屏障，上下文须保持到退订。
 * @param[in] subscription Live subscription. / 有效订阅。
 * @param[in] enabled QM_TRUE enables; QM_FALSE disables. / QM_TRUE 启用，QM_FALSE 禁用。
 * @return QM_EOK, -QM_EINVAL for invalid input, -QM_EBUSY while closing,
 * terminal or lifecycle-busy, or a negative start error. / 成功返回 QM_EOK；输入
 * 无效返回 -QM_EINVAL；关闭中、终态或生命周期冲突返回 -QM_EBUSY；其余为负的启动错误。
 */
int qm_audio_capture_enable(qm_audio_capture_subscription_t *subscription, qm_bool enabled);

/**
 * @brief Drop queued records without renumbering sequence. / 丢弃排队记录，不重排序号。
 * @param[in] subscription Live subscription; in-flight callbacks are unaffected. / 有效订阅；不影响在途回调。
 * @return QM_EOK, -QM_EBUSY while closing, -QM_EINVAL for an invalid handle.
 * / 成功返回 QM_EOK；关闭中返回 -QM_EBUSY；句柄无效返回 -QM_EINVAL。
 */
int qm_audio_capture_flush(qm_audio_capture_subscription_t *subscription);

/**
 * @brief Copy retained delivery state. / 复制保留的交付状态。
 * @param[in] subscription Handle valid until unsubscribe succeeds. / 退订成功前有效的句柄。
 * @param[out] snapshot Caller-owned snapshot output. / 调用方自有快照输出。
 * @return QM_EOK, or -QM_EINVAL for invalid arguments. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL。
 */
int qm_audio_capture_get_snapshot(qm_audio_capture_subscription_t *subscription, qm_audio_capture_snapshot_t *snapshot);

/**
 * @brief Inspect the current QUEUE payload size without consuming it. /
 * 查看当前 QUEUE 队头负载大小，但不消费记录。
 *
 * The returned size excludes Capture's private metadata and is the exact
 * payload size observed at the time of the nonblocking call. This is a
 * non-reserving query: serialize it with the matching recv() call, and still
 * handle a changed queue head or -QM_EINVAL if the queue overwrites the head
 * before receive. CALLBACK delivery cannot use this call. / 返回大小不包含
 * Capture 私有元数据，是非阻塞调用当时观测到的实际负载大小。本接口不预留队头：
 * 须与对应的 recv() 串行调用，并处理接收前队头变化或队列覆盖导致的 -QM_EINVAL。
 * CALLBACK 交付不可调用本接口。
 *
 * @param[in] subscription Live QUEUE subscription. / 有效 QUEUE 订阅。
 * @param[out] size Caller-owned payload size output; zero on entry and failure. /
 * 调用方持有的负载大小输出；进入时及失败时清零。
 * @return QM_EOK when a queued payload size is returned, -QM_EEMPTY when the
 * queue is empty, -QM_EBUSY when disabled, -QM_EINTR on stop/removal, the
 * retained source error, -QM_EIO for a corrupt subscription queue, or
 * -QM_EINVAL for invalid input or CALLBACK delivery. Queue corruption makes
 * only this subscription ERROR and clears its queue.
 * / 成功返回 QM_EOK；队列为空返回 -QM_EEMPTY；禁用返回 -QM_EBUSY；停止或移除返回
 * -QM_EINTR；源故障返回保留错误；订阅队列损坏返回 -QM_EIO，仅使该订阅进入
 * ERROR 并清空其队列；输入或 CALLBACK 模式无效返回 -QM_EINVAL。
 */
int qm_audio_capture_peek_size(qm_audio_capture_subscription_t *subscription, uint32_t *size);

/**
 * @brief Receive one native PCM block or complete encoded packet. / 接收一个原生 PCM 块或完整编码包。
 *
 * The format is fixed by Capture's input_format. PCM blocks are aligned to
 * complete sample frames; encoded packets are never split. No conversion is
 * performed. / 格式由 Capture 的 input_format 固定；PCM 按完整采样帧对齐，
 * 编码包不拆分，不进行格式转换。
 * One QUEUE receiver is allowed per subscription; CALLBACK delivery cannot use
 * this call. On entry frame->data points to caller-owned writable storage and
 * frame->size is its capacity. On success frame->size becomes the payload
 * length and the remaining metadata fields are filled by Capture. Insufficient
 * capacity leaves the head record queued. / 同一订阅仅允许一个 QUEUE 接收者；
 * CALLBACK 不可调用本接口。进入时 frame->data 指向调用方自有的可写存储，
 * frame->size 表示其容量。成功后 frame->size 改为负载长度，其余元数据由 Capture
 * 填写。容量不足时保留队头记录。
 * @param[in] subscription Live QUEUE subscription. / 有效 QUEUE 订阅。
 * @param[in,out] frame Caller-owned descriptor and storage; Capture never retains
 * the data pointer. / 调用方持有的描述符和存储；Capture 不保留 data 指针。
 * @param[in] timeout_ms Millisecond budget; zero polls, QM_WAIT_FOREVER waits. / 毫秒预算；零轮询，QM_WAIT_FOREVER 持续等待。
 * On failure frame->size is cleared; frame->data remains caller-owned. / 失败时
 * 清零 frame->size；frame->data 仍由调用方持有。
 * @return QM_EOK for one block/packet, -QM_ETIMEOUT, -QM_EBUSY when disabled/another
 * receiver exists, -QM_EINTR on stop, retained source error, -QM_EIO for a
 * corrupt subscription queue, or -QM_EINVAL for invalid input/capacity.
 * Corruption clears only this subscription's queue and makes it ERROR.
 * / 接收成功返回 QM_EOK；超时返回 -QM_ETIMEOUT；禁用/已有接收者返回 -QM_EBUSY；
 * 停止返回 -QM_EINTR；源故障返回保留错误；队列损坏返回 -QM_EIO，仅清空该订阅的
 * 队列并将其置为 ERROR；输入/容量无效返回 -QM_EINVAL。
 */
int qm_audio_capture_recv(qm_audio_capture_subscription_t *subscription,
                          qm_audio_data_t *frame, uint32_t timeout_ms);

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
/**
 * @brief Copy input into the single active PUSH Capture. / 复制输入到唯一活跃的 PUSH Capture。
 *
 * The device producer serializes calls in task context. Accepted media is
 * copied before return without running callbacks or waiting for consumers.
 * The first terminal error is retained and rejects further media until cleanup.
 * Production starts only through device capture_start after create succeeds.
 * A successful device stop waits for all in-flight feed calls and excludes
 * further calls from the old producer. Do not overlap feed with create/destroy;
 * the ingress pointer is stable between them, and QMOS protects instance data.
 * / 设备生产者在任务上下文串行调用。已接受媒体在返回前复制，不执行回调或等待
 * 消费者。首个终态错误保留并拒绝后续媒体直到清理。create 成功后由设备
 * capture_start 启动生产；设备 stop 成功须保证所有在途 feed 已返回，且旧生产者
 * 不再投递。feed 不得与 create/destroy 并发；两者之间入口指针稳定，实例数据由
 * QMOS 同步接口保护。
 * @param[in] input Borrowed native data when result is QM_EOK; NULL for error.
 * Nonempty PCM is frame-aligned; encoded input is exactly one complete packet.
 * / result 为 QM_EOK 时为借用原生数据，上报错误时为 NULL；非空 PCM 按采样帧
 * 对齐，编码输入恰好为一个完整包。
 * @param[in] result QM_EOK for media, a negative QUMI error for permanent
 * source failure. Temporary lack of data and normal stop are not errors.
 * / 媒体传 QM_EOK，永久源故障传负 QUMI 错误；暂时无数据和正常停止不是错误。
 * @return QM_EOK when accepted, -QM_EINVAL for invalid input, -QM_EBUSY when no
 * active run exists or it is stopping/failed. / 接受返回 QM_EOK；输入无效返回
 * -QM_EINVAL；无活动运行或正在停止/已失败返回 -QM_EBUSY。
 */
int qm_audio_capture_feed(const qm_audio_data_t *input, int result);
#endif

/**
 * @brief Stop delivery and free a subscription after quiescence. / 静止后停止交付并释放订阅。
 *
 * Interrupts recv and waits for receiver/callback completion; removing the final
 * subscription also stops physical capture. Exclude new access once this call
 * starts. Failure retains the closing handle for retry; callback context must
 * remain alive. Never call from Capture's reader or dispatcher task.
 * / 打断 recv 并等待接收者/回调完成；移除最后一个订阅还会停止物理采集。调用开始
 * 后须排除新访问。失败保留 closing 句柄供重试；回调上下文须继续存活。不得从
 * Capture 读取或分发任务调用。
 * Capture waits share CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS; device stop has
 * its adapter-defined finite bound. / Capture 的等待共享配置的停止时限；设备
 * stop 使用适配器约定的有限时限。
 * @param[in] subscription Owned subscription; invalid only after success. / 自有订阅；仅成功后失效。
 * @return QM_EOK, -QM_EBUSY for conflicting context/lifecycle, -QM_ETIMEOUT when
 * quiescence is incomplete, or another negative QUMI error. / 成功返回 QM_EOK；
 * 上下文/生命周期冲突返回 -QM_EBUSY；静止未完成返回 -QM_ETIMEOUT；其余为负 QUMI 错误。
 */
int qm_audio_capture_unsubscribe(qm_audio_capture_subscription_t *subscription);

#ifdef __cplusplus
}
#endif
#endif /* QM_AUDIO_CAPTURE_H */
