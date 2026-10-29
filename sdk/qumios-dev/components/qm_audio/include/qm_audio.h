/**
 * @file qm_audio.h
 * @brief Application-facing qm_audio entry header. / 面向应用的 qm_audio 统一入口头文件。
 *
 * Include this header for the default-instance lifecycle, fixed route queries,
 * shared output controls, Control playback APIs, and Capture APIs. The latter
 * two remain declared in their dedicated headers and are re-exported here.
 * Engine, Service, Pipeline, adapter, frontend, and cloud contracts remain
 * separate lower-level boundaries. / 应用通过本头文件获得默认实例生命周期、固定路由
 * 查询、共享输出控制、Control 播放接口和 Capture 接口；后两类接口仍在各自专用
 * 头文件中声明，并由此处聚合导出。Engine、Service、Pipeline、adapter、frontend
 * 和 cloud 合同继续保持为独立的底层边界。
 */
#ifndef QM_AUDIO_H
#define QM_AUDIO_H

#include "qm_audio_types.h"
#include "qm_audio_capture.h"
#include "qm_audio_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Audio instance configuration copied during initialization. / 初始化时复制的音频实例配置。 */
typedef struct {
    /** @brief Capture media format; zero PCM geometry disables that direction, opaque encoded profiles may omit it. / 采集媒体格式；PCM 几何为零禁用该方向，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t input_format;
    /** @brief Render media format; zero PCM geometry disables that direction, opaque encoded profiles may omit it. / 播放媒体格式；PCM 几何为零禁用该方向，透明编码 profile 可省略 PCM 几何。 */
    qm_audio_stream_format_t output_format;
    /** @brief Capture bytes per block; zero selects the default. / 每个采集块的字节数；零值使用默认值。 */
    uint32_t capture_frame_bytes;
    /** @brief Default capture queue capacity in blocks. / 默认采集队列容量（块）。 */
    uint32_t capture_queue_len;
} qm_audio_config_t;

/**
 * @brief Initialize the process-wide audio service. / 初始化进程级音频服务。
 *
 * The caller must serialize init/deinit with all other audio API calls. A
 * repeat init with an identical config returns QM_EOK; a different config
 * returns -QM_EBUSY. / 调用方必须使初始化/反初始化与所有其他音频 API 调用串行。
 * 以相同配置重复初始化返回 QM_EOK；配置不同返回 -QM_EBUSY。
 * @param[in] config Configuration copied by the instance. / 实例复制的配置。
 * @return QM_EOK, -QM_EINVAL for a NULL config, -QM_EBUSY for a conflicting
 * repeat init, or another negative QUMI error. / 成功返回 QM_EOK；config 为 NULL
 * 返回 -QM_EINVAL；重复初始化且配置冲突返回 -QM_EBUSY；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_init(const qm_audio_config_t *config);

/**
 * @brief Shut down and destroy the default audio instance. / 关闭并销毁默认音频实例。
 * @param[in] timeout_ms Shutdown wait budget in milliseconds. / 关闭等待时长（毫秒）。
 * @return QM_EOK after all child users stop, or a negative QUMI error.
 * / 所有子用户停止后返回 QM_EOK，否则返回负的 QUMI 错误码。
 */
int qm_audio_deinit(uint32_t timeout_ms);

/**
 * @brief Copy the initialized capture PCM format. / 复制初始化后的采集 PCM 格式。
 * @param[out] format Caller-owned format output. / 调用方格式输出。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_get_input_format(qm_audio_pcm_format_t *format);
/**
 * @brief Copy the initialized render PCM format. / 复制初始化后的播放 PCM 格式。
 * @param[out] format Caller-owned format output. / 调用方格式输出。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_get_output_format(qm_audio_pcm_format_t *format);

/**
 * @brief Copy the fixed native capture format, including encoded profiles.
 * / 复制固定的原生采集格式，含编码 profile。
 *
 * The result is the format selected by qm_audio_init() and remains valid only
 * while the default instance is initialized. / 结果为 qm_audio_init() 选定的格式，
 * 仅在默认实例保持初始化期间有效。
 * @param[out] format Caller-owned stream format output. / 调用方流格式输出。
 * @return QM_EOK, -QM_ENOSYS when capture is disabled or has no valid format,
 * or another negative QUMI error. / 成功返回 QM_EOK；采集禁用或没有有效格式返回
 * -QM_ENOSYS；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_get_input_stream_format(qm_audio_stream_format_t *format);
/**
 * @brief Read the capture delivery block size in bytes. / 获取采集交付块大小，单位字节。
 *
 * The maximum size of one capture unit delivered to any subscriber. QUEUE-mode
 * recv() capacity_bytes should be at least this value; CALLBACK-mode
 * borrowed frames never exceed it. / 单个采集交付单元的最大字节数；QUEUE 模式
 * recv() 的 capacity_bytes 至少应为此值；CALLBACK 模式借用帧不会超过该值。
 * @param[out] bytes Required caller-owned output. / 必填的调用方输出。
 * @return `QM_EOK` on success; `-QM_ENOSYS` when capture is disabled or has no
 *         valid format. / 成功返回 `QM_EOK`；采集禁用或无有效格式返回 `-QM_ENOSYS`。
 */
int qm_audio_get_capture_frame_bytes(uint32_t *bytes);
/**
 * @brief Copy the fixed native playback format, including encoded profiles. / 复制固定的原生播放格式，包括编码 profile。
 *
 * The result is the format selected by qm_audio_init() and remains valid only
 * while the default instance is initialized. / 结果为 qm_audio_init() 选定的格式，
 * 仅在默认实例保持初始化期间有效。
 * @param[out] format Caller-owned stream format output. / 调用方流格式输出。
 * @return QM_EOK, -QM_ENOSYS when playback is disabled or has no valid format,
 * or another negative QUMI error. / 成功返回 QM_EOK；播放禁用或没有有效格式返回
 * -QM_ENOSYS；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_get_output_stream_format(qm_audio_stream_format_t *format);

/**
 * @brief Copy capabilities of the initialized default route. / 复制已初始化默认路由的能力。
 *
 * The result describes the fixed route selected by qm_audio_init() and is
 * valid only while that instance remains initialized. / 结果描述 qm_audio_init()
 * 选择的固定路由，仅在该实例保持初始化期间有效。
 * @param[out] caps Caller-owned capability output. / 调用方能力输出。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_get_caps(qm_audio_caps_t *caps);

/**
 * @brief Return the default instance's independent Capture handle. / 返回默认实例的独立 Capture 句柄。
 *
 * The returned handle is borrowed and remains valid only while the default
 * instance stays initialized. Callers must serialize this lookup and all
 * default-instance init/deinit operations externally. A Capture subscription
 * keeps its own handle valid until unsubscribe succeeds; an active
 * subscription can therefore make Engine/default-instance destruction return
 * -QM_EBUSY. / 返回的句柄为借用句柄，仅在默认实例保持初始化期间有效。调用方须
 * 在外部串行化本查询与默认实例 init/deinit。Capture 订阅在退订成功前保持自身句柄
 * 有效，因此活跃订阅可能使 Engine/默认实例销毁返回 -QM_EBUSY。
 *
 * @return Borrowed Capture handle, or NULL when the default instance is not
 * initialized. / 返回借用的 Capture 句柄；默认实例未初始化时返回 NULL。
 */
qm_audio_capture_t *qm_audio_get_capture(void);

/**
 * @brief Set the shared render volume. / 设置共享播放音量。
 *
 * The request is ordered with other instance commands on audio_event; the
 * backend call runs on audio_render. An external caller waits on its own side
 * for that durable result; from an audio callback the call returns after
 * acceptance and the render worker wakes a later event turn on completion.
 * / 请求在 audio_event 上与其他实例命令排序，后端调用在 audio_render 上执行。
 * 外部调用方只在自身侧等待该持久结果；从音频回调调用时，接受后即返回，渲染
 * 任务在完成时唤醒后续事件轮次。
 * A callback never waits for another setting caller; it returns -QM_EBUSY
 * while that caller owns the request or its unread result. / 回调不等待其他设置
 * 调用方；请求或未读结果仍由其他调用方持有时，返回 -QM_EBUSY。
 * @param[in] volume Normalized gain from 0 to 100. / 归一化增益，范围 0 到 100。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_set_volume(uint8_t volume);
/**
 * @brief Set the shared render mute state. / 设置共享播放静音状态。
 *
 * The request is ordered with other instance commands on audio_event; the
 * backend call runs on audio_render. An external caller waits on its own side
 * for that durable result; from an audio callback the call returns after
 * acceptance and the render worker wakes a later event turn on completion.
 * / 请求在 audio_event 上与其他实例命令排序，后端调用在 audio_render 上执行。
 * 外部调用方只在自身侧等待该持久结果；从音频回调调用时，接受后即返回，渲染
 * 任务在完成时唤醒后续事件轮次。
 * A callback never waits for another setting caller; it returns -QM_EBUSY
 * while that caller owns the request or its unread result. / 回调不等待其他设置
 * 调用方；请求或未读结果仍由其他调用方持有时，返回 -QM_EBUSY。
 * @param[in] muted QM_TRUE mutes, QM_FALSE unmutes. / QM_TRUE 静音，QM_FALSE 取消静音。
 * @return QM_EOK, or a negative QUMI error. / 成功返回 QM_EOK；失败返回负的 QUMI 错误码。
 */
int qm_audio_set_mute(qm_bool muted);

/**
 * @brief Report a frontend wake to the default instance. / 向默认实例上报前端唤醒。
 *
 * Called by the frontend producer in task context after audio initialization.
 * Copies the wake metadata into the shared event queue; it never invokes
 * callbacks inline or waits for delivery. Not ISR-safe. Stop and join producers
 * before deinit; init/deinit must not overlap this call. / 由前端生产者在音频
 * 初始化后的任务上下文调用；将唤醒元数据复制进共享事件队列，不内联执行回调或
 * 等待交付。不支持 ISR；反初始化前须停止并等待生产者退出，初始化/反初始化不得
 * 与本调用并发。
 * @param[in] wake Borrowed metadata, copied before return; confidence 0..100.
 * / 借用的元数据，返回前复制；confidence 范围 0..100。
 * @return QM_EOK when queued, -QM_EINVAL for invalid metadata, -QM_EINIT before
 * init, -QM_EBUSY while closing or from the event task, or a negative queue
 * error (including -QM_EFULL). / 排队成功返回 QM_EOK；元数据无效返回 -QM_EINVAL；
 * 未初始化返回 -QM_EINIT；关闭中或从事件任务调用返回 -QM_EBUSY；队列失败返回
 * 负错误码（含 -QM_EFULL）。
 */
int qm_audio_report_wake(const qm_audio_wake_info_t *wake);

/**
 * @brief Subscribe to instance-wide frontend wakes. / 订阅实例级前端唤醒。
 *
 * No Control is required. Only QM_AUDIO_EVENT_WAKE is delivered, on audio_event.
 * Subscribers present at dispatch receive the event; history is not replayed.
 * Callbacks run without audio locks, must return promptly, and may start before
 * subscribe returns. Event data is borrowed until the callback returns; user_ctx
 * stays valid until unsubscribe succeeds. / 无需 Control；仅在 audio_event 上交付
 * QM_AUDIO_EVENT_WAKE。分发时在场的订阅者收到事件，不回放历史。回调不持有音频
 * 锁，须及时返回，且可能早于订阅接口返回；事件数据仅在回调期间有效，user_ctx
 * 须保持到退订成功。
 * @param[in] callback Event callback. / 事件回调。
 * @param[in] user_ctx Borrowed context, may be NULL. / 借用上下文，可为 NULL。
 * @param[out] out_subscription Handle output. / 句柄输出。
 * @return QM_EOK, -QM_EINVAL for invalid arguments, -QM_EBUSY while closing,
 * -QM_EFULL when the subscriber limit is reached, or another negative QUMI
 * error. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL；关闭中返回 -QM_EBUSY；
 * 订阅数达上限返回 -QM_EFULL；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_wake_subscribe(qm_audio_event_cb_t callback, void *user_ctx, qm_audio_subscription_t *out_subscription);

/**
 * @brief Remove a wake subscription and wait for callbacks. / 移除唤醒订阅并等待回调结束。
 *
 * Never call on audio_event. Serialize calls for this handle; success
 * invalidates it, failure retains it for retry. Remove wake subscriptions
 * before deinit. / 不得在 audio_event 上调用；同一句柄的调用须串行。成功使句柄
 * 失效，失败保留以供重试。反初始化前须取消唤醒订阅。
 * @param[in] subscription Handle from qm_audio_wake_subscribe. / 唤醒订阅接口返回的句柄。
 * @return QM_EOK after callbacks quiesce, -QM_EINVAL for an invalid or inactive
 * subscription, -QM_EBUSY when called from the event task, or another negative
 * QUMI error. / 回调静止后返回 QM_EOK；订阅无效或已移除返回 -QM_EINVAL；从事件
 * 任务调用返回 -QM_EBUSY；其余失败返回负的 QUMI 错误码。
 */
int qm_audio_wake_unsubscribe(qm_audio_subscription_t subscription);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_H */
