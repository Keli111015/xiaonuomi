#ifndef QM_AGENT_INTERNAL_H
#define QM_AGENT_INTERNAL_H

/**
 * @file qm_agent_internal.h
 * @brief Internal qm_agent session state shared by core, audio, transport, and IoT modules.
 *
 * This header is intentionally private to the component. Public applications
 * should include qm_agent.h and must not depend on the fields declared here.
 * 本头文件仅供组件内部使用；应用应只包含 qm_agent.h，不能依赖此处声明的字段。跨任务动作
 * 必须通过控制队列串行化，借用回调数据不得保存到回调返回之后。
 */

#include "qm_agent.h"
#include "qm_agent_auth.h"
#include "qm_agent_log.h"
#include "qm_agent_module.h"
#include "qm.h"
#include "qm_audio.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "backoff_algorithm.h"

/** @brief Owned copy of one cloud-originated module request. 云端模块请求的自有副本。 */
typedef struct qm_agent_module_cloud_request qm_agent_module_cloud_request_t;

/** @brief Commands and timed actions serviced by the Agent control task. Agent 控制任务处理的命令和定时动作。 */
typedef enum {
    /** @brief Recompute the next timed action after scheduling changes. */
    QM_AGENT_CONTROL_WAKE = 0,
    /** @brief Start the requested Agent session. */
    QM_AGENT_CONTROL_START,
    /** @brief Stop the active Agent session. */
    QM_AGENT_CONTROL_STOP,
    /** @brief Stop a session as the first phase of destruction. */
    QM_AGENT_CONTROL_DESTROY,
    /** @brief Send a text request through the active transport. */
    QM_AGENT_CONTROL_SEND_TEXT,
    /** @brief Start one Agent-owned TTS transaction. */
    QM_AGENT_CONTROL_TTS_REQUEST,
    /** @brief Cancel one Agent-owned TTS transaction. */
    QM_AGENT_CONTROL_TTS_CANCEL,
    /** @brief Start transport-managed long-form ASR. */
    QM_AGENT_CONTROL_ASR_START,
    /** @brief Stop transport-managed long-form ASR. */
    QM_AGENT_CONTROL_ASR_STOP,
    /** @brief Change transport-managed realtime ASR reporting. */
    QM_AGENT_CONTROL_ASR_REALTIME,
    /** @brief Interrupt the current remote request and local playback. */
    QM_AGENT_CONTROL_INTERRUPT,
    /** @brief Cache and forward IP network availability. */
    QM_AGENT_CONTROL_NETWORK_UPDATE,
    /** @brief Apply one normalized transport control event. */
    QM_AGENT_CONTROL_TRANSPORT_EVENT,
    /** @brief Apply one normalized Audio lifecycle event on the Agent task. */
    QM_AGENT_CONTROL_AUDIO_EVENT,
    /** @brief Start a downlink stream before its first high-rate data block. */
    QM_AGENT_CONTROL_AUDIO_BEGIN,
    /** @brief Abort a downlink stream after a data-path admission failure. */
    QM_AGENT_CONTROL_AUDIO_INPUT_ERROR,
    /** @brief Apply capture gates and retry retained Audio cleanup. */
    QM_AGENT_CONTROL_AUDIO_RECONCILE,
    /** @brief Run authentication and connection reconciliation. */
    QM_AGENT_CONTROL_RECONCILE,
    /** @brief Retry authentication after the configured backoff delay. */
    QM_AGENT_CONTROL_AUTH_RETRY,
    /** @brief Resolve an active TTS request timeout. */
    QM_AGENT_CONTROL_TTS_TIMEOUT,
    /** @brief Dispatch one owned cloud module request on qm_agent_ctl. */
    QM_AGENT_CONTROL_MODULE_REQUEST,
    /** @brief Deliver one cloud connection-state transition on qm_agent_ctl. */
    QM_AGENT_CONTROL_MODULE_EVENT,
    QM_AGENT_CONTROL_COPY_RESOURCE_TOKEN,
} qm_agent_control_action_t;

/**
 * @brief Synchronous completion owned by the caller that submitted a command.
 * 由提交命令的调用方持有的同步完成对象。
 *
 * The caller keeps this storage valid until qm_agent_control_call() returns;
 * qm_agent_ctl writes result and signals done exactly once.
 * 调用方必须让该存储持续有效直到 qm_agent_control_call() 返回；qm_agent_ctl 只写入一次 result
 * 并通知一次 done。
 */
typedef struct {
    /** @brief Completion semaphore initialized by the submitting caller. 提交调用方初始化的完成信号量。 */
    qm_sem_t done;
    /** @brief Whether done was created and must be freed by the caller. done 是否已创建并需由调用方释放。 */
    qm_bool valid;
    /** @brief Result written by qm_agent_ctl before signaling done. qm_agent_ctl 通知 done 前写入的结果。 */
    int result;
} qm_agent_control_completion_t;

/**
 * @brief Fixed-size control command copied into the Agent control queue.
 *
 * Pointer-bearing fields are used only by synchronous calls that wait for the
 * command result, or by an owned event clone that the control task releases.
 * Media payloads never use this structure.
 * 含指针字段只用于等待结果的同步调用，或由控制任务释放的自有事件克隆；媒体载荷绝不能进入该结构。
 */
typedef struct {
    /** @brief Control action selected by the producer. 生产者选择的控制动作。 */
    qm_agent_control_action_t type;
    union {
        /** @brief Borrowed synchronous text, valid until command completion. 借用的同步文本，在命令完成前有效。 */
        const char *text;
        /** @brief Caller-owned resource-token output buffer. 调用方持有的资源令牌输出缓冲区。 */
        struct { char *buffer; uint32_t capacity; } resource_token;
        struct {
            const qm_agent_tts_request_t *request;
            uint32_t *request_id;
        } tts_request;
        struct {
            uint32_t request_id;
            uint32_t generation;
        } tts_cancel;
        /** @brief Copied Audio lifecycle event; contains no borrowed pointers. 已复制的音频生命周期事件，不含借用指针。 */
        qm_audio_event_t audio_event;
        /** @brief Negotiated format copied before the first downlink frame. 首个下行帧前复制的协商格式。 */
        struct {
            qm_agent_audio_format_t format;
            uint32_t generation;
        } audio_begin;
        /** @brief Data-path error associated with one stream generation. 与一个流 generation 关联的数据路径错误。 */
        struct {
            int code;
            uint32_t generation;
        } audio_error;
        struct {
            uint32_t timeout_ms;
            qm_bool enabled;
        } asr;
        qm_agent_network_state_t network_state;
        qm_agent_event_t *event;
        /** @brief Owned request released after qm_agent_ctl has dispatched it. qm_agent_ctl 分发后释放的自有请求。 */
        qm_agent_module_cloud_request_t *module_request;
        /** @brief Pointer-free cloud event delivered to managed modules. 交给托管模块的无指针云端事件。 */
        qm_agent_module_event_id_t module_event;
    } data;
    /** @brief Optional caller-owned completion for synchronous commands. 同步命令可选的调用方自有完成对象。 */
    qm_agent_control_completion_t *completion;
} qm_agent_control_message_t;

/**
 * @brief Runtime state owned by the audio bridge.
 *
 * Audio data is owned and buffered by qm_audio.  Agent keeps only the current
 * Control operation and small lifecycle flags; it has no TTS queue, source
 * callback, or playback task of its own.
 * 音频数据由 qm_audio 持有和缓冲；Agent 只保存当前 Control 操作及少量生命周期标志，自己不维护
 * TTS 队列、数据源回调或播放任务。
 */
typedef struct {
    /** @brief Playback facade borrowed from qm_agent_config_t. 从 qm_agent_config_t 借用的播放 Control。 */
    qm_audio_control_t *control;

    /** @brief Owned copy of the optional playback admission descriptor. 可选播放准入描述符的自有副本。 */
    qm_agent_audio_playback_config_t playback;

    /** @brief Native input media format reported by the Engine, when capture exists. */
    qm_audio_stream_format_t input_stream_format;

    /** @brief Native output media format reported by the Engine. */
    qm_audio_stream_format_t output_stream_format;

    /** @brief Fixed PCM view of input_stream_format, retained for legacy helpers. */
    qm_audio_pcm_format_t input_format;

    /** @brief Fixed PCM view of output_stream_format, retained for legacy helpers. */
    qm_audio_pcm_format_t output_format;

    /** @brief Wire format advertised to the selected transport on uplink. */
    qm_agent_audio_format_t transport_input_format;

    /** @brief Wire format expected from the selected transport on downlink. */
    qm_agent_audio_format_t transport_output_format;

    /** @brief Wire format fixed for the current downlink stream. */
    qm_agent_audio_format_t stream_format;

    /** @brief Whether stream_format has been initialized for the current stream. */
    qm_bool stream_format_valid;

    /** @brief qm_audio capture subscriber used for upstream microphone frames. */
    qm_audio_capture_subscription_t *capture_sub;

    /** @brief Stop has closed capture admission; protected by lock. */
    qm_bool capture_closing;

    /** @brief Normalized Control subscription for playback lifecycle events. */
    qm_audio_subscription_t audio_subscription;

    /** @brief Whether audio_subscription is registered and must be removed. */
    qm_bool audio_subscription_valid;

    /** @brief Current Control PUSH operation, or QM_AUDIO_INVALID_REQUEST_ID. */
    qm_audio_operation_id_t playback_operation;

    /** @brief Operation ID observed while a Control play command is pending. */
    qm_audio_operation_id_t pending_operation;

    /** @brief Agent generation associated with playback_operation. */
    uint32_t playback_generation;

    /** @brief A Control play command has been submitted but has no ID yet. */
    qm_bool playback_pending;
    /** @brief One asynchronous stream-admission command is already queued. */
    qm_bool stream_begin_pending;

    /** @brief Whether captured audio is currently allowed to upload. */
    qm_bool upload_enabled;
    /** @brief User intent, preserved across internal gates and stop/start. */
    qm_bool user_upload_enabled;
    /** @brief Last remote dialog state, independent of local rendering. */
    qm_agent_dialog_state_t dialog_state;
    /** @brief Local playback has actually started and has not reached a terminal. */
    qm_bool playback_started;

    /** @brief Whether the active transport is ready for upstream audio. */
    qm_bool transport_ready;

    /** @brief Serializes the operation, stream format, and capture gates. */
    qm_mutex_t lock;
    /** @brief Lock was initialized successfully. */
    qm_bool lock_valid;
    /** @brief Audio bridge is open and may be used by the Agent lifecycle. */
    qm_bool runtime_valid;
    /** @brief Downlink bytes are currently accepted by the PUSH operation. */
    qm_bool stream_accepting;
    /** @brief A new reply/listening boundary must reopen admission after cleanup. */
    qm_bool stream_reopen_pending;
    /** @brief Suppress downlink TTS audio admission and enqueue when set. */
    qm_bool downlink_muted;
    /** @brief Transport has sealed the current PUSH input. */
    qm_bool stream_finished;
    /** @brief One data-path error is waiting for control-task handling. */
    qm_bool stream_error_pending;
    /** @brief One lifecycle event may arrive before play() returns. play() 返回前可能到达的一条生命周期事件。 */
    qm_bool pending_event_valid;
    /** @brief Value copy retained until play() assigns its operation ID. 在 play() 分配操作 ID 前保留的值副本。 */
    qm_audio_event_t pending_event;
    /** @brief Cleanup keeps its operation until release succeeds. 清理会一直保留操作直到 release 成功。 */
    qm_bool cleanup_pending;
    /** @brief Cleanup must stop the operation before waiting and releasing it. 清理在等待和释放前必须停止操作。 */
    qm_bool cleanup_stop;
    /** @brief Emit one playback terminal after successful cleanup. 清理成功后是否发出一次播放终态。 */
    qm_bool cleanup_notify;
    /** @brief Remote interrupt still owed after local abort/preemption. 本地中断或抢占后仍待发送的远端打断。 */
    qm_bool remote_abort_pending;
    /** @brief Terminal value retained across retryable cleanup. 跨可重试清理保留的终态值。 */
    qm_audio_event_t cleanup_event;
    /** @brief Backoff state for retained cleanup and capture gate retries. 保留清理和采集门控重试的退避状态。 */
    backoff_algorithm_ctx_t cleanup_backoff;
    /** @brief Consecutive failed attempts of the retained cleanup. 保留清理的连续失败次数。 */
    uint16_t cleanup_retry;
    /** @brief Transport input ended before a PUSH operation was created. */
    qm_bool stream_finish_pending;
    /** @brief Result saved for a pending data-path error. */
    int stream_error;
    /** @brief Agent-local generation incremented when a stream is replaced. */
    uint32_t stream_generation;
} qm_agent_audio_engine_t;

/**
 * @brief Runtime state owned by the video engine.
 * 视频引擎持有的运行状态。
 */
typedef struct {
    /** @brief Board driver context returned by qm_agent_video_driver_if_t::open. qm_agent_video_driver_if_t::open 返回的板级驱动上下文。 */
    void *driver_ctx;

    /** @brief Whether the driver is currently allowed to push frames. 驱动当前是否允许推送帧。 */
    qm_bool running;
    /** @brief Driver stop/close barrier is still required before free. 释放前是否仍需完成驱动 stop/close 屏障。 */
    qm_bool stop_required;

    /** @brief Whether the active transport is ready for upstream video. 当前传输是否已就绪以接收上行视频。 */
    qm_bool transport_ready;
} qm_agent_video_engine_t;

/**
 * @brief Runtime state owned by automatic authentication.
 * 自动认证持有的运行状态。
 */
typedef struct {
    /** @brief qm_agent_auth helper context. */
    qm_agent_auth_t *helper;

    /** @brief Latest authentication info used by transport config. */
    qm_agent_auth_info_t info;

    /** @brief Whether a valid auth cache is available. */
    qm_bool has_auth;
} qm_agent_auth_runtime_t;

/**
 * @brief qm_agent-owned active TTS transaction state.
 * qm_agent 持有的活动 TTS 事务状态。
 *
 * lock protects every field below. Callback and user_ctx remain borrowed until
 * the terminal callback has returned.
 * lock 保护下方所有字段。callback 和 user_ctx 在终态回调返回前始终为借用引用。
 */
typedef struct {
    /** @brief Serializes TTS request state. 串行化 TTS 请求状态的锁。 */
    qm_mutex_t lock;
    /** @brief Whether lock was initialized successfully. lock 是否已成功初始化。 */
    qm_bool lock_valid;
    /** @brief A request currently owns the TTS slot. 是否有请求正占用 TTS 槽位。 */
    qm_bool active;
    /** @brief STARTED callback has already been delivered. 是否已经发送 STARTED 回调。 */
    qm_bool started;
    /** @brief Monotonic nonzero request ID allocator. 单调递增且非零的请求 ID 分配器。 */
    uint32_t next_id;
    /** @brief ID of the active request. 活动请求的 ID。 */
    uint32_t request_id;
    /** @brief Audio generation associated with the active request. 与活动请求关联的音频 generation。 */
    uint32_t generation;
    /** @brief Borrowed application callback. 借用的应用回调。 */
    qm_agent_tts_cb_t callback;
    /** @brief Borrowed application callback context. 借用的应用回调上下文。 */
    void *user_ctx;
} qm_agent_tts_runtime_t;

/**
 * @brief Runtime state of the serialized Agent control scheduler.
 * 串行 Agent 控制调度器的运行状态。
 *
 * network_lock protects scheduling flags and mailboxes. The task alone executes
 * lifecycle work; producers only copy facts or enqueue bounded commands.
 * network_lock 保护调度标志和邮箱。只有任务执行生命周期工作；生产者只能复制事实或入队有界命令。
 */
typedef struct {
    /** @brief Resident serialized control task. 常驻的串行控制任务。 */
    qm_task_t task;
    /** @brief Bounded low-rate control queue. 有界低频控制队列。 */
    qm_queue_t queue;
    /** @brief Available queue-slot accounting semaphore. 可用队列槽位计数信号量。 */
    qm_sem_t slots_sem;
    /** @brief Signaled after the control task exits. 控制任务退出后通知的信号量。 */
    qm_sem_t exit_sem;
    qm_bool queue_valid;
    qm_bool slots_sem_valid;
    qm_bool exit_sem_valid;
    qm_bool task_started;
    qm_bool task_exited;
    /** @brief New commands may be admitted. 是否可准入新命令。 */
    qm_bool accepting;
    qm_bool shutdown_requested;
    /** @brief A coalesced wake command is already queued. 是否已有一条合并后的唤醒命令入队。 */
    qm_bool wake_posted;
    qm_bool reconcile_pending;
    qm_bool auth_retry_pending;
    qm_bool tts_timeout_pending;
    /** @brief Retained Audio cleanup/capture reconciliation is scheduled. 是否已安排保留音频清理或采集协调。 */
    qm_bool audio_retry_pending;
    /** @brief Deadline for the next Audio reconciliation. 下一次音频协调的截止时间。 */
    qm_utils_time_t audio_retry_deadline;
    /** @brief Reserved value mailboxes; Audio terminals never depend on queue space. 保留值邮箱，音频终态不依赖队列空间。 */
    qm_bool audio_started_pending;
    qm_bool audio_terminal_pending;
    qm_bool audio_error_pending;
    /** @brief Copied STARTED fact awaiting control-task delivery. 等待控制任务投递的 STARTED 副本。 */
    qm_audio_event_t audio_started;
    /** @brief Copied terminal fact awaiting control-task delivery. 等待控制任务投递的终态副本。 */
    qm_audio_event_t audio_terminal;
    /** @brief First retained data-path error code. 首个保留的数据路径错误码。 */
    int audio_error;
    /** @brief Stream generation associated with audio_error. 与 audio_error 关联的流 generation。 */
    uint32_t audio_error_generation;
    qm_utils_time_t reconcile_deadline;
    qm_utils_time_t auth_retry_deadline;
    uint32_t auth_retry_generation;
    qm_utils_time_t tts_timeout_deadline;
    uint32_t tts_timeout_request_id;
    uint32_t tts_timeout_generation;
} qm_agent_control_runtime_t;

/**
 * @brief Per-session state for public default-loop notification publication.
 *
 * The default loop is process-wide and is never owned or destroyed by an
 * Agent session. Queued notices only contain values from this struct, never a
 * pointer back to the session.
 * 默认循环归进程所有，不由 Agent 会话拥有或销毁。排队通知只复制该结构中的值，绝不包含指回会话的指针。
 */
typedef struct {
    /** @brief Nonzero opaque identifier copied into every public notice. */
    uint32_t agent_id;
    /** @brief Public notification publication is enabled for this session. */
    qm_bool active;
} qm_agent_event_runtime_t;

/**
 * @brief Internal concrete session object behind qm_agent_handle_t.
 * qm_agent_handle_t 背后的内部会话实体。
 */
struct qm_agent {
    /** @brief Protects network state and notification access to transport_ctx. 保护网络状态和对 transport_ctx 的通知访问。 */
    qm_mutex_t network_lock;
    /** @brief Last network availability supplied by the application. 应用提供的最近网络可用性。 */
    qm_agent_network_state_t network_state;
    /** @brief Whether network changes may reach the active transport. 网络变化是否可转发到活动传输。 */
    qm_bool network_forwarding;

    /** @brief Serialize lifecycle transitions; protected by network_lock. */
    qm_bool lifecycle_busy;
    /** @brief Reject new work once destruction has started, including retries. */
    qm_bool destroying;
    /** @brief Active application callbacks; lifecycle APIs reject reentry. */
    uint32_t callback_depth;

    /** @brief Session configuration copied from qm_agent_create(). 从 qm_agent_create() 复制的会话配置。 */
    qm_agent_config_t config;

    /** @brief Current lifecycle state. */
    qm_agent_state_t state;

    /** @brief Whether qm_agent_start() completed enough to require stop(). */
    qm_bool started;

    /** @brief Desired user state; kept across cloud reconnects. */
    qm_bool desired_running;

    /** @brief Active transport context returned by qm_agent_transport_if_t::init. qm_agent_transport_if_t::init 返回的活动传输上下文。 */
    void *transport_ctx;

    /** @brief Optional board-audio runtime. */
    qm_agent_audio_engine_t audio;

    /** @brief Optional board-video runtime. */
    qm_agent_video_engine_t video;

    /** @brief Optional token runtime. */
    qm_agent_auth_runtime_t auth;

    /** @brief Optional managed module runtime. */
    qm_agent_module_manager_t modules;

    /** @brief Generic active TTS transaction runtime. */
    qm_agent_tts_runtime_t tts_request;

    /** @brief Public default-loop notification runtime. */
    qm_agent_event_runtime_t events;

    /** @brief Cloud/control scheduler; one instance per Agent session. */
    qm_agent_control_runtime_t control;
};

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
/**
 * @brief Copy HTTP auth config from qm_agent runtime config into the helper.
 * 将 qm_agent 运行时 HTTP 认证配置复制到辅助对象。
 *
 * String fields are duplicated into qm_agent_auth-owned storage.
 *
 * @param auth Helper context.
 * @param config HTTP auth configuration from qm_agent_config_t.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_auth_set_http_config(qm_agent_auth_t *auth,
                                  const qm_agent_auth_http_config_t *config);
#endif

/** @brief Schedule reconciliation after the private auth cache is updated. 私有认证缓存更新后安排协调。 */
void qm_agent_auth_ready(qm_agent_handle_t handle);

/** @brief Change session state and emit QM_AGENT_EVENT_STATE_CHANGED when needed. 修改会话状态，并在需要时发出 QM_AGENT_EVENT_STATE_CHANGED。 */
void qm_agent_set_state(qm_agent_handle_t handle, qm_agent_state_t new_state);

/** @brief Resolve an internal event and publish its low-rate public notice. 解析内部事件并发布其低频公开通知。 */
void qm_agent_emit(qm_agent_handle_t handle, const qm_agent_event_t *event);

/** @brief Enter an application callback without holding component locks. 进入应用回调前取得生命周期保护，但不持有组件锁。 */
qm_bool qm_agent_callback_enter(qm_agent_handle_t handle);
/** @brief Try to enter a high-rate callback without waiting for lifecycle work. 尝试进入高频回调，且不等待生命周期工作。 */
qm_bool qm_agent_callback_try_enter(qm_agent_handle_t handle);
/** @brief Leave a previously admitted application callback. 离开此前已准入的应用回调。 */
void qm_agent_callback_leave(qm_agent_handle_t handle);

/** @brief Bind one session to the shared default qm_event loop. 将一个会话绑定到共享默认 qm_event 循环。 */
int qm_agent_event_runtime_init(qm_agent_handle_t handle);
/** @brief Disable publication without deleting the process-wide default loop. 停止发布，但不删除进程级默认循环。 */
void qm_agent_event_runtime_deinit(qm_agent_handle_t handle);

/** @brief Resolve a generic active TTS transaction from a normalized event. 根据标准化事件推进通用活动 TTS 事务。 */
void qm_agent_tts_runtime_on_event(qm_agent_handle_t handle,
                                   const qm_agent_event_t *event);

/** @brief Entry point used by transport adapters to report normalized events. transport 适配器上报标准化事件的入口。 */
void qm_agent_on_transport_event(const qm_agent_event_t *event, void *user_ctx);

/** @brief Execute one serialized auth/reconnect reconciliation pass. 执行一轮串行认证/重连协调。 */
void qm_agent_reconcile(qm_agent_handle_t handle);

/** @brief Serialize control actions with public session lifecycle calls. 将控制动作与公开会话生命周期调用串行化。 */
int qm_agent_lifecycle_begin(qm_agent_handle_t handle, qm_bool destroy);
void qm_agent_lifecycle_end(qm_agent_handle_t handle);

/** @brief Apply a serialized TTS timeout if its request/generation is current. 若请求/generation 仍有效，则应用串行 TTS 超时。 */
void qm_agent_tts_timeout(qm_agent_handle_t handle,
                          uint32_t request_id,
                          uint32_t generation);

/** @brief Start the per-session Agent control scheduler. 启动每会话 Agent 控制调度器。 */
int qm_agent_control_init(qm_agent_handle_t handle);
/** @brief Stop the per-session Agent control scheduler. 停止每会话 Agent 控制调度器。 */
int qm_agent_control_deinit(qm_agent_handle_t handle);
/** @brief Wait without a timeout for a create-time control task to exit. 无超时等待创建期控制任务退出。 */
int qm_agent_control_deinit_wait(qm_agent_handle_t handle);
/** @brief Submit a synchronous control command and wait for its completion. 提交同步控制命令并等待完成。 */
int qm_agent_control_call(qm_agent_handle_t handle,
                          const qm_agent_control_message_t *message);
/** @brief Submit an owned asynchronous transport event to the control task. 将自有异步传输事件提交给控制任务。 */
int qm_agent_control_post_event(qm_agent_handle_t handle,
                                qm_agent_event_t *event);
/** @brief Queue an owned cloud module request and release it on any failure. 排入自有云端模块请求，并在任一失败路径释放它。 */
int qm_agent_control_post_module_request(
    qm_agent_handle_t handle,
    qm_agent_module_cloud_request_t *request);
/** @brief Queue one pointer-free cloud lifecycle event for managed modules. 为托管模块排入一条无指针云端生命周期事件。 */
int qm_agent_control_post_module_event(qm_agent_handle_t handle,
                                       qm_agent_module_event_id_t event);
/** @brief Queue one copied qm_audio lifecycle event for the Agent task. 为 Agent 任务排入一条已复制的 qm_audio 生命周期事件。 */
int qm_agent_control_post_audio_event(qm_agent_handle_t handle,
                                      const qm_audio_event_t *event);
/** @brief Queue a nonblocking downlink stream-admission command. 排入一条非阻塞下行流准入命令。 */
int qm_agent_control_post_audio_begin(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format,
                                      uint32_t generation);
/** @brief Queue one coalesced stream-input error for the Agent task. 为 Agent 任务排入一条合并后的流输入错误。 */
int qm_agent_control_post_audio_error(qm_agent_handle_t handle,
                                      int code,
                                      uint32_t generation);
/** @brief Return whether execution is currently on this Agent's control task. 返回当前是否正在此 Agent 的控制任务上执行。 */
qm_bool qm_agent_control_is_current(qm_agent_handle_t handle);
/** @brief Schedule or replace one delayed control action using value-only arguments. 使用仅含值的参数安排或替换一条延迟控制动作。 */
int qm_agent_control_schedule(qm_agent_handle_t handle,
                              qm_agent_control_action_t type,
                              uint32_t delay_ms,
                              uint32_t request_id,
                              uint32_t generation);
/** @brief Execute one control command after dequeue; implemented by core. 出队后执行一条控制命令，由 core 实现。 */
int qm_agent_control_execute(qm_agent_handle_t handle,
                             const qm_agent_control_message_t *message);

/** @brief Release one owned cloud module request. 释放一条自有云端模块请求。 */
void qm_agent_module_cloud_request_free(qm_agent_module_cloud_request_t *request);
/** @brief Dispatch a cloud module request on the serialized control task. 在串行控制任务上分发云端模块请求。 */
int qm_agent_module_manager_dispatch_cloud_request(
    qm_agent_handle_t handle,
    qm_agent_module_cloud_request_t *request);
/** @brief Emit a cloud lifecycle event on the serialized control task. 在串行控制任务上发布云端生命周期事件。 */
int qm_agent_module_manager_emit_cloud_event(
    qm_agent_handle_t handle,
    qm_agent_module_event_id_t event);

/** @brief Open shared qm_audio runtime resources. 打开共享 qm_audio 运行时资源。 */
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
int qm_agent_audio_engine_open(qm_agent_handle_t handle);

/** @brief Close shared qm_audio runtime resources. 关闭共享 qm_audio 运行时资源。 */
int qm_agent_audio_engine_close(qm_agent_handle_t handle);

/** @brief Start optional capture processing. 启动可选的采集处理。 */
int qm_agent_audio_engine_start(qm_agent_handle_t handle);

/** @brief Stop optional capture processing. 停止可选的采集处理。 */
int qm_agent_audio_engine_stop(qm_agent_handle_t handle);

/** @brief Fill omitted wire geometry from Engine PCM without changing codecs. 用 Engine PCM 补齐省略的线缆格式参数，不改动编码类型。 */
int qm_agent_audio_engine_resolve_format(qm_agent_audio_format_t *output,
                                         const qm_agent_audio_format_t *input,
                                         const qm_audio_pcm_format_t *hardware);
/** @brief Fill omitted wire fields from a native Engine stream without changing a selected codec. */
int qm_agent_audio_engine_resolve_stream_format(
    qm_agent_audio_format_t *output,
    const qm_agent_audio_format_t *input,
    const qm_audio_stream_format_t *hardware);

/** @brief Write one transport PCM or encoded frame without blocking the callback. 非阻塞写入一帧传输 PCM 或编码数据。 */
int qm_agent_audio_engine_enqueue_transport(qm_agent_handle_t handle,
                                            const void *data,
                                            uint32_t len,
                                            const qm_agent_audio_format_t *format);

/** @brief Create the qm_audio PUSH operation for a new downlink stream. 为新的下行流创建 qm_audio PUSH 操作。 */
int qm_agent_audio_engine_begin_stream(qm_agent_handle_t handle,
                                       const qm_agent_audio_format_t *format);
/** @brief Request asynchronous stream admission for a spontaneous media path. 为自发媒体路径请求异步流准入。 */
int qm_agent_audio_engine_request_stream_begin(
    qm_agent_handle_t handle,
    const qm_agent_audio_format_t *format);
/** @brief Open an ordered reply boundary, preserving a preadmitted live PUSH. 打开有序回复边界，同时保留已准入的活动 PUSH。 */
int qm_agent_audio_engine_begin_reply(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format);

/** @brief Handle one normalized qm_audio lifecycle event on the Agent task. 在 Agent 任务上处理一条标准化 qm_audio 生命周期事件。 */
int qm_agent_audio_engine_handle_event(qm_agent_handle_t handle,
                                       const qm_audio_event_t *event);
/** @brief Apply current gates and retry cleanup on qm_agent_ctl. 在 qm_agent_ctl 上应用当前门控并重试清理。 */
int qm_agent_audio_engine_reconcile(qm_agent_handle_t handle);
/** @brief Stop failed input and retain its terminal until cleanup succeeds. 停止失败输入，并保留终态直到清理成功。 */
int qm_agent_audio_engine_fail_stream(qm_agent_handle_t handle, int code,
                                      uint32_t generation);
/** @brief Update an internal capture condition without changing user intent. 更新内部采集条件，不修改用户意愿。 */
void qm_agent_audio_engine_set_dialog_state(qm_agent_handle_t handle,
                                           qm_agent_dialog_state_t state);

/** @brief Set transport readiness without exposing the audio lock to Core. 设置传输就绪状态，不向 Core 暴露音频锁。 */
void qm_agent_audio_engine_set_transport_ready(qm_agent_handle_t handle,
                                               qm_bool ready);

/** @brief Return whether the capture path may submit one frame now. 返回采集路径当前是否可以提交一帧。 */
qm_bool qm_agent_audio_engine_upload_allowed(qm_agent_handle_t handle);

/** @brief Return whether a downlink PUSH operation is currently active. 返回下行 PUSH 操作当前是否活动。 */
qm_bool qm_agent_audio_engine_stream_active(qm_agent_handle_t handle);

/** @brief Seal current downlink input; local playback continues draining. 封闭当前下行输入；本地播放继续排空。 */
int qm_agent_audio_engine_finish_stream_input(qm_agent_handle_t handle);

/** @brief Stop the current downlink stream; wait_for_ack gates delayed packets. 停止当前下行流；wait_for_ack 用于门控延迟数据包。 */
int qm_agent_audio_engine_abort_stream(qm_agent_handle_t handle,
                                       qm_bool wait_for_ack);

/** @brief Return the current downlink generation under the audio lock. 在音频锁保护下返回当前下行 generation。 */
uint32_t qm_agent_audio_engine_stream_generation(qm_agent_handle_t handle);

/** @brief Mute or unmute downlink TTS audio frames without affecting uplink. */
void qm_agent_audio_engine_set_downlink_muted(qm_agent_handle_t handle, qm_bool muted);

/** @brief Return whether downlink TTS audio frames are currently muted. */
qm_bool qm_agent_audio_engine_is_downlink_muted(qm_agent_handle_t handle);
#endif

/** @brief Open optional video driver resources. 打开可选视频驱动资源。 */
#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
int qm_agent_video_engine_open(qm_agent_handle_t handle);

/** @brief Close optional video driver resources. 关闭可选视频驱动资源。 */
int qm_agent_video_engine_close(qm_agent_handle_t handle);

/** @brief Start optional video capture processing. 启动可选视频采集处理。 */
int qm_agent_video_engine_start(qm_agent_handle_t handle);

/** @brief Stop optional video capture processing. 停止可选视频采集处理。 */
int qm_agent_video_engine_stop(qm_agent_handle_t handle);

/** @brief Publish transport readiness under the video lifecycle lock. 在视频生命周期锁保护下发布传输就绪状态。 */
void qm_agent_video_engine_set_transport_ready(qm_agent_handle_t handle,
                                                qm_bool ready);

/** @brief Entry point used by video drivers to report normalized events. 视频驱动上报标准化事件的入口。 */
void qm_agent_on_video_event(const qm_agent_event_t *event, void *user_ctx);
#endif

/** @brief Send captured audio to the active transport. 将借用的采集音频发送至活动传输。 */
int qm_agent_transport_send_audio(qm_agent_handle_t handle, const void *data, uint32_t len);

/** @brief Send borrowed video data to the active transport. 将借用的视频数据发送至活动传输。 */
int qm_agent_transport_send_video(qm_agent_handle_t handle,
                               const void *data,
                               uint32_t len,
                               const qm_agent_video_format_t *format);

#endif
