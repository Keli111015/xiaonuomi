/**
 * @file qm_audio_priv.h
 * @brief Private state shared by qm_audio implementations. / qm_audio 实现层共享的私有状态。
 *
 * This header is not part of the component API.  Public callers use the
 * declarations under include/; the structures below only coordinate the
 * Engine, Service, Pipeline, and their callbacks. / 下方结构仅协调 Engine、Service、
 * Pipeline 及其回调，应用代码不得依赖这些声明。
 */
#ifndef QM_AUDIO_PRIV_H
#define QM_AUDIO_PRIV_H

#include "qm.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "qm_audio.h"
#include "qm_audio_engine.h"
#include "qm_audio_service.h"
#include "qm_audio_pipeline.h"
#include "adapter/qm_audio_codec.h"

#ifndef CONFIG_QM_AUDIO_ENCODER_SUPPORT
#define CONFIG_QM_AUDIO_ENCODER_SUPPORT 0
#endif

/** @brief Shared audio-instance handle. / 共享音频实例句柄。 */
typedef struct qm_audio qm_audio_t;

#ifndef CONFIG_QM_AUDIO_MAX_SUBSCRIBERS
/** @brief Subscriber limit per Control and for instance wakes. / 每个 Control 及实例唤醒的订阅上限。 */
#define CONFIG_QM_AUDIO_MAX_SUBSCRIBERS 8U
#endif

/**
 * @brief Shared audio-instance state. / 共享音频实例状态。
 *
 * Owns one device route and keeps it alive for all dependent users. Its lock
 * serializes instance lifecycle and worker ownership. / 实例拥有一条设备路径并为
 * 所有依赖用户维持其生命周期；`lock` 串行化实例生命周期和工作任务所有权。
 */
struct qm_audio {
    qm_audio_subscription_t wake_subscriptions[CONFIG_QM_AUDIO_MAX_SUBSCRIBERS];
    uint32_t wake_subscription_count;
    qm_audio_engine_t *engine;
    qm_audio_control_t *controls;
    /** Monotonic order across all coalesced Pipeline facts in this instance. */
    uint32_t pipeline_fact_sequence;
    /** Serialize one caller-observed volume/mute action at a time. */
    qm_sem_t settings_serial_sem;
    qm_audio_service_t *service;
    qm_mutex_t lock;
    uint32_t users;
    qm_bool closing;
    qm_bool shutdown_active;
    qm_bool stopped;
    qm_task_t source_task;
    qm_bool source_started;
    qm_audio_pipeline_t **pipelines;
    uint32_t pipeline_capacity;
    uint32_t next_pipeline;
    qm_sem_t source_wake;
    qm_sem_t source_stopped;
    qm_bool source_closing;
    qm_queue_t event_queue;
    qm_task_t event_task;
    qm_sem_t event_stopped;
    qm_bool event_started;
    qm_bool event_exit_sent;
};

/** @brief Assign a Pipeline to a shared source worker. / 将 Pipeline 分配给共享源任务。 */
int qm_audio_executor_submit(qm_audio_t *audio, qm_audio_pipeline_t *pipeline);
/** @brief Wake the worker that owns a Pipeline. / 唤醒持有 Pipeline 的工作任务。 */
void qm_audio_executor_wake(qm_audio_pipeline_t *pipeline);
/** @brief Start the shared event runtime. / 启动共享事件运行时。 */
int qm_audio_event_shared_init(qm_audio_t *audio);
/** @brief Stop the shared event runtime. / 停止共享事件运行时。 */
int qm_audio_event_shared_shutdown(qm_audio_t *audio, uint32_t timeout_ms);
/** @brief Destroy a stopped shared event runtime. / 销毁已停止的共享事件运行时。 */
int qm_audio_event_shared_destroy(qm_audio_t *audio);
/** @brief Wake the event task to recheck durable state. / 唤醒事件任务重新检查持久状态。 */
void qm_audio_event_wake(qm_audio_t *audio);
/** @brief Advance Service scheduling timers. / 推进 Service 调度计时器。 */
uint32_t qm_audio_service_tick(qm_audio_service_t *service);
/** @brief Retain an instance before dependent access. / 访问依赖模块前保留实例引用。 */
int qm_audio_retain(qm_audio_t *audio);
/** @brief Acquire the selected or default instance. / 获取指定实例或已初始化默认实例的引用。 */
int qm_audio_acquire(qm_audio_t **audio);
/** @brief Release an instance reference. / 释放实例引用。 */
void qm_audio_release(qm_audio_t *audio);
/** @brief Construct an internal audio instance. / 构造内部音频实例。 */
int qm_audio_create(const qm_audio_config_t *config, qm_audio_t **out_audio);
/** @brief Shut down an internal audio instance. / 关闭内部音频实例。 */
int qm_audio_shutdown(qm_audio_t *audio, uint32_t timeout_ms);
/** @brief Destroy a shut-down internal audio instance. / 销毁已关闭的内部音频实例。 */
int qm_audio_destroy(qm_audio_t *audio);
/** @brief Read an instance output format. / 读取实例输出格式。 */
int qm_audio_get_output_format_instance(qm_audio_t *audio, qm_audio_pcm_format_t *format);
/**
 * @brief Compile-time fallback defaults mirroring qm_kconfig. / 对应 qm_kconfig 的编译期回退默认值。
 *
 * qm_kconfig (consumed by `qdf.py menuconfig` / `genconfig`) is the authoritative
 * configuration source and emits CONFIG_QM_AUDIO_* into qm_config.h.  These
 * #ifndef guards only keep the module buildable standalone when a value is not
 * provided; every default below MUST stay in sync with the matching `config`
 * default in qm_kconfig.
 */
/** Service maximum sessions (maps to kconfig QM_AUDIO_SERVICE_MAX_SESSIONS). */
#ifndef CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS
#define CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS 8U
#endif
/** Control maximum operations (maps to kconfig QM_AUDIO_CONTROL_MAX_OPERATIONS). */
#ifndef CONFIG_QM_AUDIO_CONTROL_MAX_OPERATIONS
#define CONFIG_QM_AUDIO_CONTROL_MAX_OPERATIONS 8U
#endif
/** Control operation state-transition timeout / ms (maps to kconfig QM_AUDIO_CONTROL_OPERATION_TIMEOUT_MS). */
#ifndef CONFIG_QM_AUDIO_CONTROL_OPERATION_TIMEOUT_MS
#define CONFIG_QM_AUDIO_CONTROL_OPERATION_TIMEOUT_MS 1000U
#endif
/** Cloud player enable switch, 0 disabled / 1 enabled (maps to kconfig QM_AUDIO_CLOUD_SUPPORT). */
#ifndef CONFIG_QM_AUDIO_CLOUD_SUPPORT
#define CONFIG_QM_AUDIO_CLOUD_SUPPORT 0
#endif
/** Cloud worker queue length (maps to kconfig QM_AUDIO_CLOUD_QUEUE_LEN). */
#ifndef CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN
#define CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN 6U
#endif
/** Cloud maximum in-flight operations (maps to kconfig QM_AUDIO_CLOUD_MAX_OPERATIONS). */
#ifndef CONFIG_QM_AUDIO_CLOUD_MAX_OPERATIONS
#define CONFIG_QM_AUDIO_CLOUD_MAX_OPERATIONS 4U
#endif
/** Cloud task stack size / bytes (maps to kconfig QM_AUDIO_CLOUD_TASK_STACK_SIZE). */
#ifndef CONFIG_QM_AUDIO_CLOUD_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_CLOUD_TASK_STACK_SIZE 4096
#endif
/** Cloud task priority (maps to kconfig QM_AUDIO_CLOUD_TASK_PRIORITY). */
#ifndef CONFIG_QM_AUDIO_CLOUD_TASK_PRIORITY
#define CONFIG_QM_AUDIO_CLOUD_TASK_PRIORITY 20
#endif
/** Capture queue length (maps to kconfig QM_AUDIO_CAPTURE_QUEUE_LEN). */
#ifndef CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN
#define CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN 4U
#endif
/** Capture frame bytes (maps to kconfig QM_AUDIO_CAPTURE_FRAME_BYTES). */
#ifndef CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES
#define CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES 640U
#endif
/** Engine stop timeout / ms (maps to kconfig QM_AUDIO_ENGINE_STOP_TIMEOUT_MS). */
#ifndef CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS
#define CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS 1000U
#endif
/** Render PCM queue length (maps to kconfig QM_AUDIO_ENGINE_RENDER_QUEUE_LEN). */
#ifndef CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN
#define CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN 8U
#endif
#ifndef CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES
#define CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES 2048U
#endif
/** Render backend write timeout / ms (maps to kconfig QM_AUDIO_ENGINE_RENDER_WRITE_TIMEOUT_MS). */
#ifndef CONFIG_QM_AUDIO_ENGINE_RENDER_WRITE_TIMEOUT_MS
#define CONFIG_QM_AUDIO_ENGINE_RENDER_WRITE_TIMEOUT_MS 1000U
#endif
/** Source task stack size / bytes (maps to kconfig QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE). */
#ifndef CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE 4096
#endif
/** Source task priority (maps to kconfig QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY). */
#ifndef CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY
#define CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY 5
#endif
/** Shared event loop queue length (maps to kconfig QM_AUDIO_CONTROL_EVENT_QUEUE_LEN). */
#ifndef CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN
#define CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN 16U
#endif
/** Shared event task stack size / bytes (maps to kconfig QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE). */
#ifndef CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE 4096
#endif
/** Shared event task priority (maps to kconfig QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY). */
#ifndef CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY
#define CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY 10
#endif
/** Command-turn pending event slots (maps to kconfig QM_AUDIO_EVENT_PENDING_MAX). */
#ifndef CONFIG_QM_AUDIO_EVENT_PENDING_MAX
#define CONFIG_QM_AUDIO_EVENT_PENDING_MAX 8U
#endif
/** Pipeline source chunk bytes (maps to kconfig QM_AUDIO_PIPELINE_CHUNK_BYTES). */
#ifndef CONFIG_QM_AUDIO_PIPELINE_CHUNK_BYTES
#define CONFIG_QM_AUDIO_PIPELINE_CHUNK_BYTES 1024U
#endif
/** Pipeline source read timeout / ms (maps to kconfig QM_AUDIO_PIPELINE_READ_TIMEOUT_MS). */
#ifndef CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS
#define CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS 1000U
#endif
/** Built-in local file source enable switch, 0 disabled / 1 enabled
 * (maps to kconfig QM_AUDIO_SOURCE_FILE_SUPPORT). */
#ifndef CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
#define CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT 0
#endif
/** Built-in HTTP/HTTPS URI source enable switch, 0 disabled / 1 enabled
 * (maps to kconfig QM_AUDIO_SOURCE_URI_SUPPORT). */
#ifndef CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
#define CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT 0
#endif
/** Platform hardware decoder enable switch, 0 disabled / 1 enabled
 * (maps to kconfig QM_AUDIO_DECODER_SUPPORT). When enabled, the linked
 * platform must provide the decoder functions from adapter/qm_audio_codec.h. */
#ifndef CONFIG_QM_AUDIO_DECODER_SUPPORT
#define CONFIG_QM_AUDIO_DECODER_SUPPORT 0
#endif

/** @brief Engine lifecycle protected by its lock. / 由 Engine 锁保护的生命周期状态。 */
typedef enum {
    /** @brief Ready for use. / 可用。 */
    QM_AUDIO_ENGINE_STATE_OPEN = 0,
    /** @brief Cleanup is in progress. / 正在清理。 */
    QM_AUDIO_ENGINE_STATE_SHUTTING_DOWN,
    /** @brief Cleanup completed. / 清理完成。 */
    QM_AUDIO_ENGINE_STATE_SHUTDOWN,
} qm_audio_engine_state_t;

/** @brief Render-stream lifecycle advanced by the worker. / 由渲染工作任务推进的渲染流状态。 */
typedef enum {
    /** @brief Accepting PCM. / 接收 PCM。 */
    QM_AUDIO_RENDER_STATE_ACTIVE = 0,
    /** @brief Preserved without device writes. / 已保留，不写入设备。 */
    QM_AUDIO_RENDER_STATE_PAUSED,
    /** @brief Input sealed, output may remain. / 输入已封闭，输出可能仍有剩余。 */
    QM_AUDIO_RENDER_STATE_SEALED,
    /** @brief Stop has started. / 已开始停止。 */
    QM_AUDIO_RENDER_STATE_STOPPING,
    /** @brief Stopped and ready to close. / 已停止，可关闭。 */
    QM_AUDIO_RENDER_STATE_STOPPED,
    /** @brief Physically drained; stop still ends the device run. / 已物理排空，仍须 stop 结束设备运行。 */
    QM_AUDIO_RENDER_STATE_DRAINED,
} qm_audio_render_state_t;

/** @brief Worker-owned device control request. / 由渲染任务执行的设备控制请求。 */
typedef enum {
    QM_AUDIO_RENDER_ACTION_NONE = 0,
    QM_AUDIO_RENDER_ACTION_START,
    QM_AUDIO_RENDER_ACTION_RESUME,
    QM_AUDIO_RENDER_ACTION_PAUSE,
    QM_AUDIO_RENDER_ACTION_STOP,
} qm_audio_render_action_t;

/** @brief Render-worker device setting request. / 由渲染任务执行的设备设置请求。 */
typedef enum {
    QM_AUDIO_RENDER_SETTING_NONE = 0,
    QM_AUDIO_RENDER_SETTING_VOLUME,
    QM_AUDIO_RENDER_SETTING_MUTE,
} qm_audio_render_setting_t;

typedef struct qm_audio_render_packet qm_audio_render_packet_t;

/** @brief Internal event sink from lower layers to Control. / 下层模块到 Control 的内部事件接收端。 */
typedef struct {
    qm_audio_event_cb_t callback;
    void *context;
} qm_audio_event_sink_t;

/**
 * @brief Private Engine state for one native device route. / 一条原生设备路径的私有 Engine 状态。
 *
 * The worker serializes playback actions, writes, settings and position queries.
 * Drain reserves exclusive I/O on its caller; abort may overlap either path.
 * The lock protects lifecycle, stream ownership and queue accounting. Engine
 * owns the backend context until successful shutdown. / worker 串行执行播放动作、
 * 写入、设置和进度查询；调用方 drain 独占 I/O，abort 可与两条路径并发。锁保护
 * 生命周期、流所有权与队列计数；Engine 持有后端上下文直到 shutdown 成功。
 */
struct qm_audio_engine {
    /** Borrowed owner; remains valid while any subscription exists. */
    qm_audio_t *audio;
    void *backend_ctx;
    /** At most one Service may schedule this physical route. */
    qm_audio_service_t *service_owner;
    qm_audio_device_caps_t caps;
    qm_audio_stream_format_t input_format;
    qm_audio_stream_format_t output_format;
    /** Independent capture component; Engine only owns its lifetime. */
    qm_audio_capture_t *capture;
    qm_mutex_t lock;
    /** One waiter per purpose; notifications never substitute for state. / 每种用途一个等待者，通知不代替状态。 */
    qm_sem_t render_idle_sem;
    qm_sem_t render_action_sem;
    qm_sem_t render_setting_sem;
    qm_sem_t render_position_sem;
    qm_sem_t render_space_sem;
    qm_sem_t render_wake_sem;
    qm_sem_t render_stopped_sem;
    qm_queue_t render_queue;
    qm_bool render_task_started;
    qm_bool render_exit_requested;
    qm_task_t render_task;
    /** Queue entries plus a producer's reserved slot. / 队列项及生产者预留的槽位。 */
    uint32_t render_queued;
    uint32_t render_inflight;
    uint32_t render_leases;
    /** Durable volume/mute request, executed only by audio_render. */
    qm_audio_render_setting_t setting;
    uint8_t setting_value;
    qm_bool setting_pending;
    int setting_result;
    /** One shutdown owns state transition and backend cleanup at a time. */
    qm_bool shutdown_active;
    qm_audio_engine_state_t state;
    qm_audio_render_stream_t *render;
};

/** @brief Private render lease for one active session. / 一个活动 Service 会话持有的私有渲染租约。 */
struct qm_audio_render_stream {
    qm_audio_engine_t *engine;
    /** Borrowed until route close; Service owns progress-waiter fan-out. / 借用至路由关闭；Service 负责唤醒各进度等待者。 */
    qm_sem_t *service_progress_sem;
    qm_audio_delivery_t delivery;
    qm_audio_render_state_t state;
    qm_bool drain_active;
    uint32_t pending_packets;
    /** Route owner plus generation-bound producers. / 路由所有者及本代生产者引用。 */
    uint32_t references;
    uint64_t submitted_frames;
    uint64_t submitted_duration_ms;
    uint64_t submitted_packets;
    qm_bool submitted_duration_known;
    int result;
    /** Durable control completion, protected by Engine lock. / Engine 锁保护的持久控制结果。 */
    qm_audio_render_action_t action;
    qm_bool action_pending;
    int action_result;
    /** Observation must not overwrite an unread control result. / 观测不得覆盖尚未读取的控制结果。 */
    qm_bool position_pending;
    qm_bool position_waiting;
    int position_result;
    uint32_t cached_position_ms;
    qm_bool cached_position_valid;
};

/** @brief Publish an action; STOP also aborts I/O on the caller. / 提交动作；STOP 同时由调用方中断 I/O。 */
int qm_audio_engine_render_request(qm_audio_render_stream_t *stream, qm_audio_render_action_t action);
/** @brief Reserve a route and start it on the render worker. / 保留路由并交由渲染任务启动。 */
int qm_audio_engine_render_open_request(qm_audio_engine_t *engine, qm_audio_service_t *owner, const qm_audio_render_config_t *config, qm_audio_render_stream_t **out_stream);
/** @brief Open for the matching route owner and wait for start. / 为匹配的路由所有者打开并等待启动。 */
int qm_audio_engine_render_open_owned(qm_audio_engine_t *engine, qm_audio_service_t *owner, const qm_audio_render_config_t *config, qm_audio_render_stream_t **out_stream);
/** @brief Poll durable completion; -QM_EBUSY means pending. / 查询持久结果，-QM_EBUSY 表示未完成。 */
int qm_audio_engine_render_result(qm_audio_render_stream_t *stream, qm_audio_render_action_t action, int *result);
/** @brief Read the position sampled at a completed device action. / 读取设备动作完成时采样的位置。 */
int qm_audio_engine_render_cached_position(qm_audio_render_stream_t *stream, uint32_t *position_ms);
/**
 * @brief Bind a borrowed Service progress semaphore to this route. / 为本路由绑定借用的 Service 进度信号量。
 *
 * Binding emits a progress notification. Engine only wakes a driver; Service
 * broadcasts action completion to its waiters and hands off pending progress
 * when a waiter exits. The caller keeps the semaphore valid until route close
 * or explicit unbinding with NULL. / 绑定时立即发送进度通知。Engine 只唤醒驱动者；
 * Service 向各等待者广播动作完成，并在等待者退出时交接未完成动作的进度通知。
 * 调用方保持信号量有效，直到路由关闭或显式用 NULL 解除绑定。
 * @return QM_EOK on success, -QM_EINVAL for invalid input, or -QM_EINTR after
 * route replacement. / 成功返回 QM_EOK；参数无效返回 -QM_EINVAL；路由已替换返回
 * -QM_EINTR。
 */
int qm_audio_engine_render_set_service_progress(qm_audio_render_stream_t *stream,
                                               qm_sem_t *sem);
/** @brief Request a volume or mute update without device I/O on this caller. */
int qm_audio_engine_render_setting_request(qm_audio_engine_t *engine, qm_audio_render_setting_t setting, uint8_t value);
/** @brief Wait outside audio_event for a previously accepted setting request. */
int qm_audio_engine_render_setting_wait(qm_audio_engine_t *engine, qm_audio_render_setting_t setting, uint8_t value);
/** @brief Bind this Service as the sole route owner; -QM_EBUSY when already owned or route in use. / 绑定本 Service 为唯一路由所有者；已被占用时返回 -QM_EBUSY。 */
int qm_audio_engine_claim_owner(qm_audio_engine_t *engine, qm_audio_service_t *owner);
/** @brief Release the route owner if it is still this Service. / 若所有者仍为本 Service 则释放路由。 */
void qm_audio_engine_release_owner(qm_audio_engine_t *engine, qm_audio_service_t *owner);

/**
 * @brief Private Service scheduler state. / 私有 Service 调度器状态。
 *
 * `lock` serializes admission, active-route replacement, session state, and
 * callback state transitions. The Engine remains borrowed and outlives the
 * Service. Individual session handles remain valid after terminal events
 * until their explicit release succeeds.
 */
struct qm_audio_service {
    qm_audio_engine_t *engine;
    qm_mutex_t lock;
    qm_bool shutting_down;
    uint32_t max_sessions;
    uint32_t session_count;
    qm_audio_session_t **sessions;
    qm_audio_session_t *active;
    /**
     * Next per-Control request-id base handed out on Control create.
     *
     * Bases are disjoint 16-bit-shifted ranges so operations from different
     * Controls never collide in the shared session table.
     */
    uint32_t next_request_id_base;
    /** Waits until callbacks that borrowed a session's sink context return. */
    qm_sem_t event_idle_sem;
    /** Number of Service-to-Control callbacks currently executing. */
    uint32_t event_inflight;
};

/** @brief Service transition driven by the event task. / 事件任务推进的 Service 转换。 */
typedef enum {
    QM_AUDIO_SERVICE_ACTION_NONE = 0,
    QM_AUDIO_SERVICE_ACTION_START,
    QM_AUDIO_SERVICE_ACTION_RESUME,
    QM_AUDIO_SERVICE_ACTION_PAUSE,
    QM_AUDIO_SERVICE_ACTION_SUSPEND,
    QM_AUDIO_SERVICE_ACTION_REPOSITION,
    QM_AUDIO_SERVICE_ACTION_STOP,
    QM_AUDIO_SERVICE_ACTION_PREEMPT,
    /** Physical drain already acknowledged by Source. / Source 已确认物理 drain。 */
    QM_AUDIO_SERVICE_ACTION_FINISH,
} qm_audio_service_action_t;

/** @brief Private Service session protected by Service lock. / 由 Service 锁保护的私有会话。 */
struct qm_audio_session {
    qm_audio_service_t *service;
    qm_audio_session_request_t request;
    /**
     * Owning Control's notice sink, set at acquire.
     *
     * Lifecycle notices for this session are delivered only to this callback
     * and context, so one Service can route events to isolated Controls.
     */
    qm_audio_event_cb_t event_callback;
    void *event_context;
    qm_audio_session_state_t state;
    int result;
    qm_audio_render_stream_t *stream;
    qm_sem_t state_sem;
    /** Per-session Engine progress and action completion notification. / 会话独立的 Engine 进度与动作完成通知。 */
    qm_sem_t action_sem;
    uint32_t action_waiters;
    /** Wake release after an internal Service operation drops its pin. */
    qm_sem_t internal_ref_sem;
    qm_bool stream_closing;
    uint32_t stream_users;
    /** Pins held by Service scheduling paths outside Service::lock. */
    uint32_t internal_refs;
    /** Prevents new internal pins while public release is pending. */
    qm_bool release_pending;
    /** Shared action slot; STOP/PREEMPT reuse a superseded action's pin. / 统一动作槽；STOP/PREEMPT 复用被覆盖动作的引用。 */
    qm_audio_service_action_t action;
    qm_bool action_pending;
    qm_bool action_submitted;
    /** STOP submission can call backend abort outside Service lock. / STOP 提交可在 Service 锁外调用后端 abort。 */
    qm_bool action_submitting;
    qm_bool action_open;
    uint32_t action_value;
    int action_result;
    /** Physical drain result committed by FINISH after route cleanup. / FINISH 清理路由后提交的物理排空结果。 */
    int finish_result;
    /** Admission depends on this active route being revoked. / 准入依赖该活动路由被撤销。 */
    qm_audio_session_t *preempting;
    /** Cookie of the victim action this admission depends on. / 本次准入所依赖的被抢占动作 cookie。 */
    uint32_t preempt_cookie;
    /** Cookie for the current and completed closing action. / 当前及已完成关闭动作的 cookie。 */
    uint32_t action_cookie;
    qm_bool acquire_deadline_valid;
    qm_utils_time_t acquire_deadline;
    uint32_t generation;
    /** @brief Progress retained across output handoff. / 跨输出交接保留的媒体进度。 */
    uint32_t position_base_ms;
    uint32_t cached_position_ms;
    uint32_t continuation_position_ms;
    qm_bool position_valid;
    qm_bool origin_pending;
    /** Cached before detaching the Engine lease. / 解除 Engine 绑定前缓存。 */
    uint32_t submitted_ms;
    qm_bool submitted_valid;
};

/**
 * @brief Private Pipeline execution state. / 私有 Pipeline 执行状态。
 *
 * The worker owns source/decoder I/O. After it has joined, rewind/destroy may
 * retry a failed close before resetting or freeing the context.
 * File/URI strings, decoder settings and the codec hint are copied at create;
 * custom source and sink contexts remain borrowed until Pipeline destruction.
 */
struct qm_audio_pipeline {
    qm_bool packet_passthrough;
    qm_bool byte_passthrough;
    qm_bool needs_decoder;
    qm_bool packet_duration_known;
    void *encoder_ctx;
    uint8_t *packet_buffer;
    uint32_t packet_capacity;
    uint32_t packet_used;
    uint32_t packet_expected;
    uint64_t packet_time_ms;
    uint64_t packet_duration_total_ms;
    qm_audio_data_t packet_meta;
    /** Executor registration persists while paused; workers do not. */
    qm_audio_t *executor;
    qm_task_t *execution_task;
    qm_bool execution_active;
    qm_bool execution_started;
    uint32_t execution_stage;
    /** Source-worker rewind request and durable result. / Source 任务执行的回退请求及持久结果。 */
    qm_bool rewind_pending;
    qm_bool rewind_complete;
    /** Stop superseded this rewind; Source must leave a terminal Pipeline. */
    qm_bool rewind_cancelled;
    uint32_t rewind_position_ms;
    uint32_t rewind_generation;
    int rewind_result;
    uint8_t *chunk;
    uint32_t chunk_capacity;
    uint32_t chunk_size;
    uint32_t chunk_offset;
    uint32_t raw_carry;
    int read_result;
    qm_utils_time_t decoder_deadline;
    qm_bool decoder_budget_active;
    uint32_t decoder_pause_generation;
    /** @brief Reject events from a cancelled pre-seek run. / 拒绝已取消的 seek 前执行轮次产生的事件。 */
    uint32_t generation;
    uint64_t decoded_frames;
    uint64_t skip_frames;
    uint32_t seek_position_ms;
    uint32_t duration_ms;
    uint32_t progress_flags;
    qm_audio_pipeline_config_t config;
    qm_audio_decoder_config_t decoder_config;
    char *file_path;
    char *uri;
    /** @brief Strong validator retained across HTTP reopens. / 跨 HTTP 重开保留的强校验器。 */
    char *http_etag;
    uint32_t http_length;
    /** @brief Decode-resume record copied at create; consumed on the first open. / create 时复制的解码续播记录；首次打开时消费。 */
    qm_audio_resume_point_t resume_point;
    qm_bool resume_valid;
    /** @brief 续播点捕获进行中：阻止 decoder_close 释放其仍在引用的解码器上下文。 */
    qm_bool capture_active;
    /** @brief 解码器关闭窗口：置位期间拒绝新捕获（锁内访问）。 */
    qm_bool closing;
    qm_mutex_t lock;
    qm_sem_t state_sem;
    /** Resume/cancel wakeup, separate from state observers. */
    qm_sem_t wake_sem;
    /** @brief 捕获完成通知：decoder_close 等待进行中的捕获退出。 */
    qm_sem_t capture_sem;
    /** Execution lease including queued work and terminal callbacks. */
    qm_bool task_started;
    qm_bool cancel_requested;
    qm_bool paused;
    qm_bool pause_notified;
    uint32_t pause_waiters;
    uint32_t pause_generation;
    uint32_t emit_inflight;
    qm_bool resume_pending;
    qm_task_t task;
    qm_audio_pipeline_state_t state;
    int result;
    void *source_ctx;
    void *decoder_ctx;
    qm_bool source_open;
    qm_bool decoder_open;
    qm_audio_event_sink_t event_sink;
};

/**
 * @brief Bounded byte ring for a Control PUSH source. / Control PUSH 源使用的有界字节环。
 *
 * Writers copy data under lock and signal data_sem. The Pipeline source worker
 * consumes the same ring and signals space_sem after each read. The context is
 * freed only after Pipeline cancellation and destruction have quiesced.
 */
typedef struct {
    qm_mutex_t lock;
    qm_sem_t data_sem;
    qm_sem_t space_sem;
    qm_bool finished;
    qm_bool cancelled;
    qm_bool interrupted;
    uint8_t *buffer;
    uint32_t capacity;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;
} qm_audio_push_source_t;

/** @brief Private PUSH packet envelope; copied atomically with payload. */
typedef struct {
    uint32_t timestamp_ms;
    uint32_t size;
    uint32_t duration_ms;
    uint32_t sequence;
    uint32_t flags;
} qm_audio_packet_header_t;

/** @brief Private Control operation association. / 私有 Control 操作关联。 */
typedef struct qm_audio_control_operation qm_audio_control_operation_t;

/** @brief Producer that emitted a lower-layer event. / 产生下层事件的模块。 */
typedef enum {
    /** @brief Service emitted the event. / Service 产生事件。 */
    QM_AUDIO_LOWER_SERVICE = 0,
    /** @brief Pipeline emitted the event. / Pipeline 产生事件。 */
    QM_AUDIO_LOWER_PIPELINE,
    /** @brief Control public notification. / Control 公开通知。 */
    QM_AUDIO_LOWER_PUBLIC,
} qm_audio_lower_event_source_t;

/** @brief Lower-layer event plus its authoritative source. / 下层事件及其权威来源。 */
typedef struct {
    qm_audio_lower_event_source_t source;
    qm_audio_event_t event;
} qm_audio_lower_event_t;

/** @brief Lightweight business facade. / 轻量业务门面，执行和事件任务归 audio 实例所有。 */
struct qm_audio_control {
    /** Event-task registry link; detached by the EXIT barrier. / 事件任务链表，EXIT 屏障解除绑定。 */
    qm_audio_control_t *event_next;
    qm_audio_t *audio;
    qm_audio_service_t *service;
    qm_audio_engine_t *engine;
    qm_audio_stream_format_t output_format;
    qm_mutex_t lock;
    qm_bool shutting_down;
    /** Protect the operation table during a shutdown attempt. */
    qm_bool shutdown_active;
    uint32_t max_operations;
    qm_audio_operation_id_t next_operation_id;
    uint32_t operation_count;
    uint32_t operation_timeout_ms;
    qm_audio_control_operation_t **operations;
    /** Logical attachment to the instance's shared event task. */
    qm_bool event_attached;
    /** Wake shutdown after this Control's FIFO barrier is consumed. */
    qm_sem_t event_stopped_sem;
    /** Shutdown has closed admission and is draining earlier posts. */
    qm_bool event_exit_requested;
    /** The event task may accept commands and notifications. */
    qm_bool event_dispatch_active;
    /** A shutdown call is in flight; a concurrent call returns -QM_EBUSY. */
    qm_bool event_shutdown_active;
    /** Wake shutdown after the last producer finishes an event post. */
    qm_sem_t event_post_idle_sem;
    /** Producers between admission and completion of an event post. */
    uint32_t event_post_inflight;
    /**
     * Bounded event FIFO, drained only by the event task.
     *
     * Service callbacks raised on the command stack must not enqueue
     * behind the command itself (producer == consumer), so they are staged
     * here and projected before the command signals completion. Other tasks
     * use the same FIFO and wait for space; they never project facts inline.
     * The source tag preserves each fact's authoritative router. Pipeline
     * producers use the operation's durable fact slots instead of this FIFO;
     * public notifications use it when the instance queue is full.
     */
    qm_audio_lower_event_t event_pending[CONFIG_QM_AUDIO_EVENT_PENDING_MAX];
    uint32_t event_pending_count;
    qm_sem_t event_pending_space_sem;
    uint32_t event_pending_waiters;
    /** One queued wake covers all pending lower facts. */
    qm_bool event_lower_wake_pending;
    /**
     * Set while the event task drains one queued item (a "turn").
     *
     * Written by the event task under Control::lock and read by lower-event
     * posts on the command stack or a user task. A post re-checks it under the lock so
     * it stages facts and queues a wake only when the event task is idle.
     */
    qm_bool event_turn_active;
    /** Service-allocated base of this Control's operation-id range. */
    qm_audio_request_id_t request_id_base;
    qm_audio_subscription_t *subscriptions;
    uint32_t subscription_count;
};

/** @brief One Control event subscriber. / 一个 Control 事件订阅者。 */
struct qm_audio_subscription {
    qm_audio_control_t *control;
    /** Wake owner; NULL for Control subscriptions. / 唤醒所属实例，Control 订阅为空。 */
    qm_audio_t *audio;
    qm_audio_event_cb_t callback;
    void *context;
    qm_sem_t idle_sem;
    uint32_t callback_inflight;
    qm_bool active;
};

/**
 * @brief Private association of one Service session and Pipeline run. / 一个 Service 会话和一次 Pipeline 执行的私有关联。
 *
 * Session and Pipeline states are raw diagnostic inputs. `state` is the only
 * product-facing state and is projected under Control::lock. Completion and
 * resume flags prevent a transient lower-layer state from producing an
 * extra Control event. Release claims a terminal operation only when no API
 * or event handler retains it; Pipeline destruction quiesces data callbacks
 * before the operation is freed.
 */
typedef enum {
    QM_AUDIO_TRANSITION_NONE = 0,
    QM_AUDIO_TRANSITION_PAUSE,
    QM_AUDIO_TRANSITION_SUSPEND,
    QM_AUDIO_TRANSITION_SEEK,
    QM_AUDIO_TRANSITION_STOP,
    QM_AUDIO_TRANSITION_RESUME,
} qm_audio_transition_t;

struct qm_audio_control_operation {
    qm_audio_control_t *control;
    qm_audio_operation_id_t id;
    qm_audio_session_t *session;
    qm_audio_pipeline_t *pipeline;
    /** Stable until the source run quiesces; revocation does not free it. / 源静止前保持有效，撤销授权不释放。 */
    qm_audio_render_stream_t *output;
    /** Durable, coalesced Pipeline facts; no producer waits for queue space. / 持久 Pipeline 事实，生产者不等待队列空间。 */
    struct {
        uint32_t sequence;
        uint32_t generation;
        uint32_t timestamp_ms;
        uint32_t position_ms;
        uint32_t reason;
        int result;
    } facts[QM_AUDIO_EVENT_SEEKED + 1U];
    qm_audio_control_state_t state;
    qm_audio_session_state_t session_state;
    qm_audio_pipeline_state_t pipeline_state;
    uint32_t generation;
    uint32_t position_ms;
    uint32_t reason;
    int result;
    uint32_t pipeline_generation;
    /** Pending Source acknowledgement, advanced only by audio_event. / 仅由事件任务推进的 Source 确认。 */
    qm_audio_transition_t transition;
    qm_bool transition_service_started;
    uint32_t transition_position_ms;
    qm_bool transition_paused;
    qm_bool transition_output_closed;
    qm_bool repositioning;
    qm_bool seeking;
    qm_bool rewind_on_resume;
    qm_bool restart_pending;
    qm_bool service_ready;
    qm_bool pipeline_finished;
    qm_bool pipeline_start_requested;
    qm_bool resume_in_progress;
    qm_bool control_failed;
    qm_bool accepted_event_sent;
    qm_bool started_event_sent;
    qm_bool terminal_event_sent;
    /** @brief References held across Control unlocks. / 跨越 Control 解锁范围持有的引用计数。 */
    uint32_t api_users;
    /** Release has claimed the operation and rejects new users. */
    qm_bool releasing;
    /** Play setup is still constructing lower-layer handles. */
    qm_bool initializing;
    /** At most one blocking completion observer per operation. */
    qm_bool wait_active;
    qm_sem_t state_sem;
    /** @brief Private bounded input ring for QM_AUDIO_SOURCE_PUSH. / QM_AUDIO_SOURCE_PUSH 的私有有界输入环。 */
    qm_audio_push_source_t *push_source;
};

/** @brief Control command handled by the shared event task. / 由共享事件任务处理的 Control 命令。 */
typedef enum {
    /** @brief Start playback. / 启动播放。 */
    QM_AUDIO_COMMAND_PLAY = 0,
    /** @brief Pause playback. / 暂停播放。 */
    QM_AUDIO_COMMAND_PAUSE,
    /** @brief Yield output but retain context. / 让出输出但保留上下文。 */
    QM_AUDIO_COMMAND_SUSPEND,
    /** @brief Resume playback. / 恢复播放。 */
    QM_AUDIO_COMMAND_RESUME,
    /** @brief Stop playback. / 停止播放。 */
    QM_AUDIO_COMMAND_STOP,
    /** @brief Reposition playback. / 重定位播放位置。 */
    QM_AUDIO_COMMAND_SEEK,
    /** @brief Set shared volume. / 设置共享音量。 */
    QM_AUDIO_COMMAND_SET_VOLUME,
    /** @brief Set shared mute state. / 设置共享静音状态。 */
    QM_AUDIO_COMMAND_SET_MUTE,
} qm_audio_command_type_t;

/** @brief Command payload sent to the shared event task. / 发送到共享事件任务的命令载荷。 */
typedef struct {
    qm_audio_command_type_t type;
    qm_audio_control_play_request_t play_request;
    qm_audio_operation_id_t *operation_id;
    qm_audio_operation_id_t operation;
    uint32_t reason;
    uint32_t position_ms;
    uint8_t volume;
    qm_bool muted;
    qm_sem_t done;
    int result;
} qm_audio_command_t;

/** @brief Discriminator for a Control event-loop item. / Control 事件循环队列项的类型标识。 */
typedef enum {
    /** @brief Synchronous API command. / 同步 API 命令。 */
    QM_AUDIO_EVENT_MSG_COMMAND = 0,
    /** @brief Public event dispatch. / 公开事件分发。 */
    QM_AUDIO_EVENT_MSG_DISPATCH,
    /** @brief Frontend wake for instance subscribers. / 面向实例订阅者的前端唤醒。 */
    QM_AUDIO_EVENT_MSG_FRONTEND_WAKE,
    /** @brief Service or Pipeline fact. / Service 或 Pipeline 事实事件。 */
    QM_AUDIO_EVENT_MSG_LOWER,
    /** @brief Event-task shutdown barrier. / 事件任务关闭屏障。 */
    QM_AUDIO_EVENT_MSG_EXIT,
    /** @brief Wake to drain durable state and Capture rings. / 唤醒以清空持久状态和采集环形队列。 */
    QM_AUDIO_EVENT_MSG_WAKE,
} qm_audio_event_msg_type_t;

/** @brief One item consumed by the shared event task. / 由共享事件任务消费的单个队列项。 */
typedef struct {
    qm_audio_event_msg_type_t type;
    qm_audio_control_t *control;
    union {
        /** Caller-owned command with a completion semaphore. */
        qm_audio_command_t *command;
        /** Public notification copied into the queue. */
        qm_audio_event_t event;
        /** Lower-layer fact staged before the event-task wake is queued. */
        struct {
            qm_audio_lower_event_source_t source;
            qm_audio_event_t event;
        } lower;
    } u;
} qm_audio_event_msg_t;

/** @brief Post one message to the instance event task. / 向实例事件任务投递一条消息。 */
int qm_audio_event_post(qm_audio_t *audio, const qm_audio_event_msg_t *msg);

/** @name Internal Service, Pipeline, and event runtime interface / 内部 Service、Pipeline 与事件运行时接口
 * @{ */
/** @brief Pin a run's output until its producer quiesces. / 持有本轮输出直到生产者静止。 */
int qm_audio_engine_render_retain(qm_audio_render_stream_t *stream);
/** @brief Drop a producer or route reference. / 释放生产者或路由引用。 */
void qm_audio_engine_render_release(qm_audio_render_stream_t *stream);
/** @brief Snapshot accepted media time. / 读取已接收媒体的时长快照。 */
int qm_audio_engine_render_submitted(qm_audio_render_stream_t *stream, uint32_t *duration_ms);
/** @brief Bind a stable output lease after admission. / 准入后绑定稳定输出句柄。 */
int qm_audio_service_session_bind(qm_audio_session_t *session, qm_audio_render_stream_t **stream);
/** @brief Allocate a disjoint Control request-id range. / 分配互不重叠的 Control 请求 ID 范围。 */
int qm_audio_service_alloc_request_id_base(qm_audio_service_t *service, qm_audio_request_id_t *base);
/** @brief Wait for Service callback delivery to quiesce. / 等待 Service 回调分发静止。 */
int qm_audio_service_wait_event_idle(qm_audio_service_t *service, uint32_t timeout_ms);
/** @brief Install a Pipeline-to-Control event sink. / 安装 Pipeline 到 Control 的事件接收端。 */
int qm_audio_pipeline_set_event_sink(qm_audio_pipeline_t *pipeline, qm_audio_event_cb_t callback, void *context);
/** @brief Initialize a Control event runtime. / 初始化 Control 事件运行时。 */
int qm_audio_event_runtime_init(qm_audio_control_t *control);
/** @brief Shut down a Control event runtime. / 关闭 Control 事件运行时。 */
int qm_audio_event_runtime_shutdown(qm_audio_control_t *control, uint32_t timeout_ms);
/** @brief Destroy a stopped Control event runtime. / 销毁已停止的 Control 事件运行时。 */
int qm_audio_event_runtime_destroy(qm_audio_control_t *control);
/** @brief Test whether caller is the event task. / 判断调用方是否为事件任务。 */
qm_bool qm_audio_event_is_current(const qm_audio_control_t *control);
/** @brief Dispatch an event to subscribers. / 向订阅者分发事件。 */
void qm_audio_event_dispatch(qm_audio_control_t *control, const qm_audio_event_t *event);
/** @brief Queue a synchronous Control command. / 将同步 Control 命令入队。 */
int qm_audio_command_post(qm_audio_control_t *control, qm_audio_command_t *command);
/** @brief Queue a shared Engine setting command without a Control facade. */
int qm_audio_engine_command_post(qm_audio_t *audio, qm_audio_command_t *command);
/** @} */

/** @name Internal Control implementation interface / 内部 Control 实现接口
 * @{ */
/** @brief Implement play on the event-task turn. / 在事件任务轮次中执行播放。 */
int qm_audio_control_play_impl(qm_audio_control_t *control, const qm_audio_control_play_request_t *request, qm_audio_operation_id_t *operation_id);
/** @brief Implement preserving pause. / 执行保留上下文的暂停。 */
int qm_audio_control_pause_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id);
/** @brief Implement output-yield suspension. / 执行让出输出的挂起。 */
int qm_audio_control_suspend_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id);
/** @brief Advance worker acknowledgements without waiting. / 无等待地推进工作任务确认。 */
uint32_t qm_audio_control_advance(qm_audio_control_t *control);
/** @brief Implement resume. / 执行恢复。 */
int qm_audio_control_resume_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id);
/** @brief Implement seek and source reposition. / 执行 seek 与音频源重定位。 */
int qm_audio_control_seek_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t position_ms);
/**
 * @brief Reposition a Service session. / 重定位 Service 会话。
 *
 * Cancel the producer first; close output while retaining held ownership; join
 * the old run before resume opens its replacement stream. / 先取消生产者；在保留
 * 既有所有权时关闭输出；旧执行完成后，恢复操作才打开替换的输出流。
 */
int qm_audio_service_session_reposition(qm_audio_session_t *session, uint32_t position_ms);
/** @brief Register a nonblocking Service action; value is seek position or stop reason. / 登记非阻塞动作，value 为 seek 位置或 stop 原因。 */
int qm_audio_service_session_request(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value);
/** @brief Advance without waiting; -QM_EBUSY is pending, QM_EOK returns the durable result. / 非阻塞推进，-QM_EBUSY 表示未完成，QM_EOK 返回持久结果。 */
int qm_audio_service_session_result(qm_audio_session_t *session, qm_audio_service_action_t action, int *result);
/** @brief Admit without device I/O; STARTED arrives after asynchronous open. / 无设备 I/O 的准入，异步打开完成后发出 STARTED。 */
int qm_audio_service_session_acquire_async(qm_audio_service_t *service, const qm_audio_session_request_t *request, qm_audio_event_cb_t callback, void *context, qm_audio_session_t **out_session);
/** @brief Set the first PCM timestamp before a fresh run. / 在新的输出执行写入前设置首个 PCM 时间戳。 */
int qm_audio_service_session_set_origin(qm_audio_session_t *session, uint32_t position_ms);
/** @brief Rewind source and codec state. / 回退音频源与编解码状态。 */
int qm_audio_pipeline_rewind(qm_audio_pipeline_t *pipeline, uint32_t position_ms, uint32_t generation);
/** @brief Submit rewind cleanup to the shared Source worker. / 向共享 Source 任务提交回退清理。 */
int qm_audio_pipeline_rewind_request(qm_audio_t *audio, qm_audio_pipeline_t *pipeline, uint32_t position_ms, uint32_t generation);
/** @brief Poll a submitted rewind cleanup result. / 查询已提交回退清理的结果。 */
int qm_audio_pipeline_rewind_result(qm_audio_pipeline_t *pipeline, int *result);
/** @brief Cancel normal execution or a queued Source rewind for STOP. / 为 STOP 取消普通执行或已排队的 Source 回退。 */
int qm_audio_pipeline_cancel_for_stop(qm_audio_pipeline_t *pipeline);
/** @brief Read Pipeline duration and progress flags. / 读取 Pipeline 时长和进度标志。 */
void qm_audio_pipeline_get_progress(qm_audio_pipeline_t *pipeline, uint32_t *duration_ms, uint32_t *flags);
/** @brief Capture a decode-resume record from the running Pipeline. / 从运行中的 Pipeline 捕获解码续播记录。 */
int qm_audio_pipeline_capture_resume(qm_audio_pipeline_t *pipeline, qm_audio_resume_point_t *resume, uint32_t timeout_ms);

/** @brief Implement stop. / 执行停止。 */
int qm_audio_control_stop_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t reason);
/** @brief Implement shared-volume update. / 执行共享音量更新。 */
int qm_audio_control_set_volume_impl(qm_audio_control_t *control, uint8_t volume);
/** @brief Implement shared-mute update. / 执行共享静音更新。 */
int qm_audio_control_set_mute_impl(qm_audio_control_t *control, qm_bool muted);
/** @brief Consume a Service lifecycle event. / 消费 Service 生命周期事件。 */
void qm_audio_control_service_event_impl(qm_audio_control_t *control, const qm_audio_event_t *event);
/** @brief Consume a Pipeline lifecycle event. / 消费 Pipeline 生命周期事件。 */
void qm_audio_control_pipeline_event_impl(qm_audio_control_t *control, const qm_audio_event_t *event);
/** @brief Consume durable Pipeline facts across all Controls on one instance. / 事件任务按实例顺序消费全部 Control 的持久 Pipeline 事实。 */
void qm_audio_dispatch_pipeline_facts(qm_audio_t *audio);
/** @} */

/** @name Internal Pipeline worker interface / 内部 Pipeline 工作接口
 * @{ */
/** @brief Prepare Pipeline source and decoder. / 准备 Pipeline 音频源与解码器。 */
int qm_audio_pipeline_prepare(qm_audio_pipeline_t *pipeline);
/** @brief Execute one Pipeline work slice. / 执行一次 Pipeline 工作片段。 */
qm_bool qm_audio_pipeline_execute(qm_audio_pipeline_t *pipeline);
/** @brief Fail Pipeline and emit its terminal event. / 使 Pipeline 失败并发出终态事件。 */
void qm_audio_pipeline_fail(qm_audio_pipeline_t *pipeline, int result);
/** @brief Wait until Pipeline acknowledges pause. / 等待 Pipeline 确认暂停。 */
int qm_audio_pipeline_wait_paused(qm_audio_pipeline_t *pipeline, uint32_t timeout_ms);
/** @} */

/** @name Internal PUSH source interface / 内部 PUSH 音频源接口
 * @{ */
/** @brief Allocate a bounded PUSH byte ring. / 分配有界 PUSH 字节环。 */
int qm_audio_push_source_create(uint32_t capacity, qm_audio_push_source_t **out_source);
/** @brief Destroy a quiescent PUSH byte ring. / 销毁已静止的 PUSH 字节环。 */
void qm_audio_push_source_destroy(qm_audio_push_source_t *source);
/** @brief Read bytes from a PUSH byte ring. / 从 PUSH 字节环读取数据。 */
int qm_audio_push_source_read(qm_audio_push_source_t *source, void *data, uint32_t capacity, uint32_t timeout_ms, uint32_t *read_size);
/** @brief Cancel all PUSH readers and writers. / 取消所有 PUSH 读写操作。 */
void qm_audio_push_source_cancel(qm_audio_push_source_t *source);
/** @brief Wake a blocked read for pause acknowledgement. / 唤醒一个阻塞读，使 Pipeline 可确认保留式暂停。 */
void qm_audio_push_source_interrupt(qm_audio_push_source_t *source);
/** @brief Copy bytes into a PUSH byte ring. / 向 PUSH 字节环复制数据。 */
int qm_audio_push_source_write(qm_audio_push_source_t *source, const void *data, uint32_t size, uint32_t timeout_ms);
/** @brief Mark a PUSH source as end of input. / 将 PUSH 源标记为输入结束。 */
int qm_audio_push_source_finish(qm_audio_push_source_t *source);
int qm_audio_push_source_write_packet(qm_audio_push_source_t *source, const qm_audio_data_t *packet, uint32_t timeout_ms);
/** @} */

#include "qm_audio_format.h"

/** @brief Test whether a Service state is terminal. / 判断 Service 状态是否终态。 */
static inline qm_bool qm_audio_session_terminal(qm_audio_session_state_t state)
{
    return (state == QM_AUDIO_SESSION_FINISHED ||
            state == QM_AUDIO_SESSION_CANCELLED ||
            state == QM_AUDIO_SESSION_PREEMPTED ||
            state == QM_AUDIO_SESSION_ERROR) ? QM_TRUE : QM_FALSE;
}

/** @brief Test whether a Control state is terminal. / 判断 Control 状态是否终态。 */
static inline qm_bool qm_audio_control_terminal(qm_audio_control_state_t state)
{
    return (state == QM_AUDIO_CONTROL_FINISHED ||
            state == QM_AUDIO_CONTROL_CANCELLED ||
            state == QM_AUDIO_CONTROL_PREEMPTED ||
            state == QM_AUDIO_CONTROL_ERROR) ? QM_TRUE : QM_FALSE;
}

#endif /* QM_AUDIO_PRIV_H */
