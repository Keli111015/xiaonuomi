/**
 * @file qm_audio_service.h
 * @brief Internal playback-session admission and lifecycle interface. / 内部播放会话准入与生命周期接口。
 *
 * Playback-session admission and lifecycle contracts; declarations only.
 *
 * Service is the sole scheduling authority for its borrowed Engine. A caller
 * must not open a competing Engine render stream on a Service-managed route.
 * This header describes PCM or encoded playback sessions, not recording or
 * duplex-session submission; those broader design goals are not implemented
 * by these declarations.
 *
 * All functions are task-context APIs. Lifecycle notifications are consumed by
 * the owning Control facade and are not exposed from Service. Applications
 * subscribe through qm_audio_control_subscribe(). One Service may serve
 * multiple Control facades: each session carries its owner's sink at acquire,
 * so notices are delivered only to the Control that admitted the session, and
 * request IDs are Service-allocated per-Control ranges to stay unique. Control
 * operations on one session are serialized by the caller. Producers write
 * directly to Engine through a retained output handle. Closing a route revokes
 * further writes; the handle stays alive until its producer releases it.
 * Session release/destroy must not race access to the session being freed.
 * Synchronous action APIs use the same action slot as runtime requests and
 * wait for completion within CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS. They must
 * not run on audio_event or audio_render. A wait timeout retains the pending
 * action and stream for retry; cleanup remains possible after audio_event exits.
 * / Service 是其借用 Engine 的唯一调度权威；调用方不得在 Service 管理的路由上
 * 打开竞争渲染流。本头文件描述 PCM 或编码播放会话，不涉及录音或双工会话提交。
 * 所有函数均为任务上下文接口。生命周期通知由所属 Control 门面消费，不通过
 * Service 暴露。一个 Service 可服务多个 Control 门面：会话在 acquire 时携带
 * 其所有者的 sink，因此通知只投递给准入该会话的 Control，请求 ID 按 Control
 * 分段分配以保持唯一。生产者通过保留的输出句柄直接写入 Engine；路由关闭会
 * 撤销后续写入，但句柄在生产者释放前保持有效。会话 release/destroy 不得与
 * 待释放会话的访问并发。
 * 同步动作接口与运行时请求共用动作槽，在 CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS
 * 内等待完成，不得在 audio_event 或 audio_render 中调用。等待超时保留待完成动作
 * 和流供重试；audio_event 退出后仍可执行同步清理。
 */
#ifndef QM_AUDIO_SERVICE_H
#define QM_AUDIO_SERVICE_H

#include "qm_audio_engine.h"
#include "qm_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque Service scheduler handle. / 不透明的 Service 调度器句柄。 */
typedef struct qm_audio_service qm_audio_service_t;
/** @brief Opaque playback-session handle. / 不透明的播放会话句柄。 */
typedef struct qm_audio_session qm_audio_session_t;

/** @brief Service-managed session state. / Service 管理的会话状态。 */
typedef enum {
    /** @brief Allocated but not admitted. / 已分配，尚未准入。 */
    QM_AUDIO_SESSION_NEW = 0,
    /** @brief Awaiting admission. / 等待准入。 */
    QM_AUDIO_SESSION_QUEUED,
    /** @brief Owns the output route. / 已占用输出路径。 */
    QM_AUDIO_SESSION_ACTIVE,
    /** @brief Input is paused. / 输入已暂停。 */
    QM_AUDIO_SESSION_PAUSED,
    /** @brief Input sealed while output drains. / 输入已封闭，输出正在排空。 */
    QM_AUDIO_SESSION_DRAINING,
    /** @brief Completed normally. / 正常完成。 */
    QM_AUDIO_SESSION_FINISHED,
    /** @brief Cancelled by caller or shutdown. / 被调用方或关闭流程取消。 */
    QM_AUDIO_SESSION_CANCELLED,
    /** @brief Replaced by a higher-priority session. / 被更高优先级会话抢占。 */
    QM_AUDIO_SESSION_PREEMPTED,
    /** @brief Ended with an error. / 因错误结束。 */
    QM_AUDIO_SESSION_ERROR,
} qm_audio_session_state_t;

/** @brief Session admission request. / 会话准入请求。 */
typedef struct {
    qm_audio_request_id_t request_id;
    qm_audio_priority_t priority;
    qm_audio_admission_policy_t policy;
    qm_audio_delivery_t delivery;
    uint32_t acquire_timeout_ms;
} qm_audio_session_request_t;

/**
 * @brief Service construction options. / Service 构造选项。
 *
 * The Engine is borrowed, not transferred, and must outlive the Service. No
 * application callback, SDK handle, or business-resource handle belongs here;
 * lifecycle events are forwarded to Control through a private sink.
 * / Engine 是借用的而非转移，且必须比 Service 存活更久。此处不含应用回调、SDK
 * 句柄或业务资源句柄；生命周期事件通过私有 sink 转发给 Control。
 */
typedef struct {
    /** @brief Required open Engine; Service does not destroy this handle. / 必填的已打开 Engine；Service 不销毁该句柄。 */
    qm_audio_engine_t *engine;
    /**
     * @brief Maximum allocated sessions, including unreleased terminal ones.
     * Zero selects CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS. / 最大会话分配数，含未
     * 释放的终态会话。零值使用 CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS。
     */
    uint32_t max_sessions;
} qm_audio_service_config_t;

/**
 * @brief Create a Service bound to an existing Engine. / 创建绑定到已有 Engine 的 Service。
 * @param[in] config Options; the top-level structure is copied. / 配置；顶层结构会被复制。
 * @param[out] out_service Required output, set to NULL on failure. The caller
 * owns the returned handle and must shut it down before destroying it.
 * / 必填输出，失败时为 NULL。调用方持有返回句柄，销毁前必须先 shutdown。
 * @return QM_EOK, -QM_EINVAL for invalid configuration, -QM_ENOMEM for
 * allocation failure, -QM_EBUSY if the Engine is shutting down, already
 * managed by another Service, or still owns a render stream, or another
 * negative qm_errno value.
 * / 成功返回 QM_EOK；配置无效返回 -QM_EINVAL；分配失败返回 -QM_ENOMEM；Engine
 * 正在关闭、已被其他 Service 管理或仍持有渲染流返回 -QM_EBUSY；其余失败返回
 * 负 qm_errno 值。
 */
int qm_audio_service_create(const qm_audio_service_config_t *config, qm_audio_service_t **out_service);

/**
 * @brief Stop admission and quiesce all sessions. / 停止准入并静止活动与排队会话。
 *
 * This is a blocking cleanup barrier, not a handle deallocation. It cancels
 * queued sessions and synchronously stops the active session (the underlying
 * Engine stop applies an internal fixed timeout). Session handles remain valid
 * for query/release after success; the borrowed Engine is not destroyed.
 * / 这是阻塞式清理屏障，不是句柄释放。它取消排队会话并同步停止活动会话（底层
 * Engine stop 使用内部固定超时）。成功后会话句柄仍可查询/释放；借用的 Engine
 * 不会被销毁。
 * @param[in] service Service to close; repeat shutdown is allowed. / 待关闭的 Service；重复 shutdown 允许。
 * @return QM_EOK after quiescence, -QM_EBUSY while a concurrent session release
 * still owns required cleanup, -QM_EINVAL for an invalid handle, or another
 * negative qm_errno value. / 静止后返回 QM_EOK；并发会话释放仍持有必要的清理时
 * 返回 -QM_EBUSY；句柄无效返回 -QM_EINVAL；其余失败返回负 qm_errno 值。
 * @note On failure, retain Service, Engine, and callback contexts. Admission
 * stays closed; do not release resources still used by I/O or callbacks.
 * / 失败时保留 Service、Engine 与回调上下文。准入保持关闭；不要释放仍被 I/O 或
 * 回调使用的资源。
 */
int qm_audio_service_shutdown(qm_audio_service_t *service);

/**
 * @brief Free a shut-down Service with no sessions. / 释放已关闭且没有未完成会话的 Service。
 * @param[in] service Handle to free; invalid only after this call succeeds. / 待释放句柄；仅本调用成功后才失效。
 * @return QM_EOK, -QM_EBUSY before shutdown or while sessions remain,
 * -QM_EINVAL for an invalid handle. On failure the handle remains owned.
 * / 成功返回 QM_EOK；未关闭或仍有会话返回 -QM_EBUSY；句柄无效返回 -QM_EINVAL。
 * 失败时句柄仍归调用方所有。
 */
int qm_audio_service_destroy(qm_audio_service_t *service);

/**
 * @brief Submit an asynchronous admission request. / 提交异步准入请求并返回会话。
 *
 * Success means accepted, not granted or audible. Confirm admission through
 * STARTED or session_wait. A preempting request cannot write until the old
 * output has confirmed stopping; priority never bypasses that barrier. If the
 * required stop fails, the accepted preempting session transitions to ERROR
 * and still requires release. / 成功只表示已接受，不代表已准入或已出声；通过
 * STARTED 或 session_wait 确认准入。抢占请求必须等旧输出确认停止后才能写入，
 * 优先级不会绕过该屏障。若必需的 stop 失败，已接受的抢占会话转入 ERROR，仍需
 * release。
 * @param[in] service Open Service that will own scheduling for this request. / 已打开的 Service，负责本请求的调度。
 * @param[in] request Descriptor, copied during the call. / 描述符，调用期间复制。
 * @param[in] event_callback Required lifecycle sink for this session. Every
 * notice this session raises is delivered to this callback with
 * event_context, so one Service can route events to isolated Controls. Pass
 * NULL to suppress delivery. / 必填的会话生命周期 sink。会话产生的每条通知都会
 * 以 event_context 投递给该回调，因此一个 Service 可把事件路由到隔离的 Control；
 * 传 NULL 可抑制投递。
 * @param[in] event_context Borrowed context for event_callback; it must stay
 * valid until the session is released and no in-flight notice remains.
 * / 借用的事件回调上下文；必须保持有效直到会话被释放且没有在途通知。
 * @param[out] out_session Required output; NULL on rejection. On acceptance,
 * the caller must eventually release the handle, even after error/preemption.
 * / 必填输出；拒绝时为 NULL。接受后调用方最终必须 release 该句柄，即使在出错
 * 或抢占之后。
 * @return QM_EOK if accepted, -QM_EINVAL for invalid/duplicate request IDs,
 * -QM_EFULL for session capacity exhaustion, -QM_EBUSY if admission is closed
 * or the policy rejects an occupied route, -QM_ENOSYS for unsupported options,
 * or another negative qm_errno value. / 接受返回 QM_EOK；请求 ID 无效或重复返回
 * -QM_EINVAL；会话容量耗尽返回 -QM_EFULL；准入关闭或策略拒绝占用路由返回
 * -QM_EBUSY；选项不支持返回 -QM_ENOSYS；其余失败返回负 qm_errno 值。
 */
int qm_audio_service_session_acquire(qm_audio_service_t *service, const qm_audio_session_request_t *request, qm_audio_event_cb_t event_callback, void *event_context, qm_audio_session_t **out_session);

/**
 * @brief Wait for admission without cancelling on timeout. / 等待准入，调用方超时不取消请求。
 * @param[in] session Accepted and unreleased session. / 已接受且未释放的会话。
 * @param[in] timeout_ms Caller wait budget; zero polls, QM_WAIT_FOREVER waits
 * without a caller deadline. The request's admission deadline still applies.
 * / 调用方等待预算；零值轮询，QM_WAIT_FOREVER 表示无调用方截止时间。请求的准入
 * 截止时间仍然生效。
 * @note This waiter also advances a pending START or its preemption dependency,
 * so admission can be retried after an earlier wait timed out without audio_event.
 * / 等待方也推进未完成的 START 或其抢占依赖，因此早先等待超时后，即使没有
 * audio_event 也可继续准入。
 * @return QM_EOK when admitted, -QM_ETIMEOUT if the wait/deadline expires,
 * -QM_EINTR after cancellation/preemption, or another negative qm_errno value
 * for admission failure. A timeout alone does not permit freeing the handle.
 * / 准入成功返回 QM_EOK；等待/截止时间到期返回 -QM_ETIMEOUT；取消或抢占返回
 * -QM_EINTR；准入失败返回负 qm_errno 值。仅凭超时不得释放句柄。
 */
int qm_audio_service_session_wait(qm_audio_session_t *session, uint32_t timeout_ms);

/**
 * @brief Cancel a queued or active session without draining. / 取消排队或活动会话，不执行排空。
 *
 * Input waits must be interrupted. Completion is confirmed by the terminal
 * event/state, not by this call's return; the session still needs release.
 * / 输入等待必须被打断。完成由终止事件/状态确认，不由本调用返回确认；会话仍需
 * release。
 * @param[in] session Session to cancel; a terminal session may still retry its
 * physical stream cleanup without changing its terminal state. / 待取消的会话；
 * 终态会话仍可重试其物理流清理，且不改变终态。
 * @return QM_EOK if cancellation is accepted or cleanup is already complete,
 * -QM_EINVAL for an invalid handle, or another negative qm_errno value.
 * / 取消被接受或清理已完成返回 QM_EOK；句柄无效返回 -QM_EINVAL；其余失败返回
 * 负 qm_errno 值。
 */
int qm_audio_service_session_cancel(qm_audio_session_t *session);

/**
 * @brief Request a preserving pause. / 请求保留上下文的暂停。
 * @param[in] session Admitted session; context and accepted media are retained. / 已准入会话；上下文与已接收媒体被保留。
 * @return QM_EOK if accepted, -QM_ENOSYS if preserving pause is unsupported,
 * -QM_EBUSY for an incompatible state, -QM_EINVAL for an invalid handle.
 * / 接受返回 QM_EOK；不支持保留式暂停返回 -QM_ENOSYS；状态不符返回 -QM_EBUSY；
 * 句柄无效返回 -QM_EINVAL。
 * @note Writes stop being eligible once paused. PAUSED confirms completion;
 * failure must not silently implement stop/replay as a substitute for pause.
 * / 暂停后写入不再被接受。PAUSED 确认完成；失败时不得静默用 stop/重放代替暂停。
 */
int qm_audio_service_session_pause(qm_audio_session_t *session);

/**
 * @brief Release output while retaining a paused session. / 释放输出，同时保留已暂停会话以供恢复。
 *
 * The producer must already be paused. Queued output is discarded; the session
 * and its producer remain owned by the caller. On failure retain both and
 * retry suspend/stop; PAUSED alone does not prove release succeeded.
 * / 生产者必须先已暂停。排队输出被丢弃；会话及其生产者仍归调用方所有。失败时
 * 保留两者并重试 suspend/stop；仅 PAUSED 不能证明释放成功。
 */
int qm_audio_service_session_suspend(qm_audio_session_t *session);

/**
 * @brief Resume a paused session without replaying input. / 恢复已暂停会话，不重放其输入。
 * @param[in] session Paused session whose context remains valid. / 已暂停会话，其上下文仍有效。
 * @return QM_EOK if accepted, -QM_ENOSYS if unsupported, -QM_EBUSY for an
 * incompatible state, -QM_EINVAL for an invalid handle. RESUMED confirms it.
 * / 接受返回 QM_EOK；不支持返回 -QM_ENOSYS；状态不符返回 -QM_EBUSY；句柄无效
 * 返回 -QM_EINVAL。恢复由 RESUMED 确认。
 * @note Backend resume/open failure transitions to ERROR, matching the runtime
 * request path; retain the session and clean it up with stop/release.
 * / 后端恢复或打开失败转入 ERROR，与运行时请求路径一致；保留会话并用 stop/release
 * 清理。
 */
int qm_audio_service_session_resume(qm_audio_session_t *session);

/**
 * @brief Finish input and wait for output completion. / 在输入结束后等待输出到达设备边界。
 *
 * Call only after the producer, including any decoder tail, has finished.
 * Success completes the session and hands off the physical route without
 * freeing the session; callers must still release it. This call does not
 * complete a separate Pipeline on the caller's behalf. / 仅在生产者（含解码器
 * 尾帧）完成后调用。成功会完成会话并让出物理路由，但不释放会话；调用方仍需
 * release。本调用不会替调用方完成独立的 Pipeline。
 * @param[in] session Active session with no concurrent producer write. / 活动会话，无并发生产者写入。
 * @param[in] timeout_ms Output-completion wait; zero polls, QM_WAIT_FOREVER waits. / 输出完成等待；零值轮询，QM_WAIT_FOREVER 等待。
 * @return QM_EOK after confirmed output completion, -QM_ENOSYS if confirmation
 * is not supported, -QM_ETIMEOUT with the handle retained and input still
 * sealed, -QM_EINTR on interruption, or another negative qm_errno value.
 * / 确认输出完成后返回 QM_EOK；不支持确认返回 -QM_ENOSYS；超时返回 -QM_ETIMEOUT
 * 且句柄保留、输入仍封闭；中断返回 -QM_EINTR；其余失败返回负 qm_errno 值。
 * @note Do not free or grant the same physical resources on timeout alone.
 * / 仅凭超时不得释放或让出相同的物理资源。
 * Physical drain uses timeout_ms; subsequent stop/close uses the fixed action
 * cleanup budget. FINISHED is published only after both succeed.
 * / 物理排空使用 timeout_ms，随后停止和关闭使用固定动作清理预算；两者均成功后
 * 才发布 FINISHED。
 */
int qm_audio_service_session_finish(qm_audio_session_t *session, uint32_t timeout_ms);

/**
 * @brief Stop immediately and discard queued output. / 立即停止并丢弃队列输出，不执行排空。
 * @param[in] session Session to stop; terminal state is unchanged, but a
 * terminal session may retry physical stream cleanup. / 待停止的会话；终态不变，
 * 但终态会话可重试物理流清理。
 * @param[in] reason Caller-defined reason copied to CANCELLED; zero is unspecified. / 调用方定义的原因，复制到 CANCELLED；零表示未指定。
 * @return QM_EOK if accepted, -QM_EINVAL for an invalid handle, or another
 * negative qm_errno value. CANCELLED confirms successful cleanup because this
 * ABI has no separate STOPPED state. / 接受返回 QM_EOK；句柄无效返回 -QM_EINVAL；
 * 其余失败返回负 qm_errno 值。CANCELLED 确认清理成功，因为本 ABI 没有独立的
 * STOPPED 状态。
 */
int qm_audio_service_session_stop(qm_audio_session_t *session, uint32_t reason);

/**
 * @brief Copy cached session state without I/O. / 不执行设备或网络 I/O，复制缓存会话状态。
 * @param[in] session Unreleased session, including a terminal session. / 未释放的会话，含终态会话。
 * @param[out] state Required output, valid on success. / 必填输出，成功时有效。
 * @return QM_EOK, or -QM_EINVAL for an invalid argument. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL。
 * @note ERROR is not proof of hardware quiescence; release may still be busy.
 * / ERROR 不证明硬件已静止；release 仍可能处于忙碌。
 */
int qm_audio_service_session_get_state(const qm_audio_session_t *session, qm_audio_session_state_t *state);

/**
 * @brief Query observed output progress. / 查询已观测的输出进度，而非仅已接收字节数。
 * @param[in] session Unreleased session to query. / 待查询的未释放会话。
 * @param[out] position_ms Required output, milliseconds from this session's
 * media origin. A preserving pause/resume does not reset this origin.
 * / 必填输出，从本会话媒体起点算起的毫秒数。保留式暂停/恢复不会重置该起点。
 * @return QM_EOK when an observation exists, -QM_ENOSYS when the backend cannot
 * observe output progress, -QM_EINVAL for an invalid argument. / 存在观测返回
 * QM_EOK；后端无法观测输出进度返回 -QM_ENOSYS；参数无效返回 -QM_EINVAL。
 * @note The current HAL has no position callback or precision field. Do not
 * report submitted-byte estimates as hardware-consumed progress. / 当前 HAL
 * 没有位置回调或精度字段。不要把已提交字节的估算当作硬件已消费的进度。
 */
int qm_audio_service_session_get_position(qm_audio_session_t *session, uint32_t *position_ms);

/**
 * @brief Release a terminal session after quiescence. / I/O 与回调静止后释放终态会话。
 * @param[in] session Handle to free. The caller must exclude concurrent access
 * until this call succeeds; lifecycle delivery is owned by Control.
 * / 待释放句柄。调用方在本调用成功前须排除并发访问；生命周期投递归 Control 所有。
 * @return QM_EOK on success (invalidating the handle), -QM_EBUSY if still
 * active or another release owns the handle, -QM_ETIMEOUT if a Service route
 * transition does not quiesce within its fixed cleanup budget, -QM_EINVAL for
 * an invalid handle. Failure does not free the handle; terminal event delivery
 * alone does not imply release. / 成功返回 QM_EOK（句柄失效）；仍活跃或另一释放
 * 持有句柄返回 -QM_EBUSY；路由转换未在固定清理预算内静止返回 -QM_ETIMEOUT；
 * 句柄无效返回 -QM_EINVAL。失败不释放句柄；仅收到终止事件不代表已释放。
 */
int qm_audio_service_session_release(qm_audio_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* QM_AUDIO_SERVICE_H */
