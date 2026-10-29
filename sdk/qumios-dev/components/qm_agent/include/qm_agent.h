#ifndef QM_AGENT_H
#define QM_AGENT_H

/**
 * @file qm_agent.h
 * @brief Public qm_agent session API.
 *
 * qm_agent 会话的公共接口。应用通过本头文件配置传输、音视频能力、认证与模块，
 * 并管理会话的创建、启动、停止和销毁。
 */

#include "qm_agent_log.h"
#include "qm_agent_event.h"
#include "qm_agent_transport.h"
#include "qm_agent_video_driver.h"
#include "qm_audio.h"

#ifndef CONFIG_QM_AGENT_MAX_MODULES
#define CONFIG_QM_AGENT_MAX_MODULES 8
#endif

/** @brief Maximum number of managed modules stored in one qm_agent_config_t. 单个 qm_agent_config_t 可保存的托管模块最大数。 */
#define QM_AGENT_MAX_MODULES ((uint32_t)CONFIG_QM_AGENT_MAX_MODULES)

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Playback admission policy copied when the Agent is created. Agent 创建时复制的播放准入策略。 */
typedef struct {
    /** @brief Relative priority on the shared Audio Service. */
    qm_audio_priority_t priority;
    /** @brief Behavior when another business owns the output. */
    qm_audio_admission_policy_t policy;
    /** @brief Admission deadline in ms, in 1..(INT32_MAX-1), or QM_WAIT_FOREVER. */
    uint32_t acquire_timeout_ms;
} qm_agent_audio_playback_config_t;

/**
 * @brief Audio Control borrowed by one qm_agent session.
 *
 * The application creates and owns the Control and its parent qm_audio_t.
 * qm_agent never shuts them down or destroys them. Playback and capture use
 * that same instance automatically. Dedicate this Control to this Agent;
 * other businesses use their own Controls on the shared audio instance.
 *
 * capture_enabled subscribes to Engine callback delivery. Agent borrows each
 * PCM frame or native encoded packet only during the callback; the transport
 * copies accepted bytes into its own bounded ingress. Agent has no upload
 * task or capture buffer.
 *
 * uplink_format describes bytes accepted by Transport. Automatic capture is
 * pass-through only: it must exactly match the Engine's PCM format or its
 * fixed native encoded profile. A zeroed descriptor uses the native Engine
 * profile. downlink_format describes received bytes before Pipeline processing;
 * a zeroed descriptor also uses the native output profile. Pipeline preserves
 * fixed Opus CBR packet boundaries and inserts only compiled decoder and
 * encoder stages when the source does not already match the
 * output device.
 * 应用创建并拥有 Control 及其 qm_audio_t；qm_agent 仅在会话期间借用。采集帧和下行媒体
 * 均不在本层缓存，传输适配器必须在回调返回前消费或复制数据。
 */
typedef struct {
    /** @brief Control facade used for all finite and streaming playback. */
    qm_audio_control_t *control;
    /** @brief Enable automatic Engine capture; QM_FALSE leaves playback available. */
    qm_bool capture_enabled;
    /** @brief Optional transport uplink format; zeroed means Engine PCM. */
    qm_agent_audio_format_t uplink_format;
    /** @brief Optional transport downlink format; zeroed means Engine PCM. */
    qm_agent_audio_format_t downlink_format;
    /**
     * @brief Optional policy copied during create; NULL selects HIGH,
     * PREEMPT_LOWER and CONFIG_QM_AGENT_AUDIO_ACQUIRE_TIMEOUT_MS. The descriptor
     * need only remain valid until qm_agent_create() returns.
     */
    const qm_agent_audio_playback_config_t *playback;
} qm_agent_audio_config_t;

/**
 * @brief qm_agent session configuration.
 *
 * String pointers, preload module name pointers, module config pointers, and
 * custom context pointers are borrowed by qm_agent or forwarded into module
 * callbacks. Unless a backend or module documents that it copies them
 * internally, keep them valid for the session lifetime.
 * 字符串、预加载模块名、模块配置和自定义上下文均由 qm_agent 借用或转交给模块回调；除非
 * 后端或模块明确会复制，调用方必须保持它们在会话整个生命周期内有效。
 */
typedef struct {
    /** @brief Transport configuration. */
    qm_agent_transport_config_t transport;

    /** @brief Optional custom transport interface. */
    const qm_agent_transport_if_t *transport_if;

    /** @brief Video format sent from the device to the backend. */
    qm_agent_video_format_t input_video;

    /** @brief Video format received from the backend. */
    qm_agent_video_format_t output_video;

    /** @brief New qm_audio handles borrowed for the session lifetime. */
    qm_agent_audio_config_t audio;

    /**
     * @brief Microphone policy while transport audio is playing.
     *
     * AUTO preserves the existing half-duplex behavior. KEEP is intended for
     * audio drivers with hardware or system voice processing such as macOS
     * VoiceProcessingIO.
     */
    qm_agent_capture_policy_t transport_capture_policy;

    /**
     * @brief Video driver interface. NULL disables board video capture.
     *
     * A configured driver pushes borrowed frames through the callback supplied
     * to its open() method; it must quiesce callbacks before stop()/close()
     * returns.
     */
    const qm_agent_video_driver_if_t *video_driver;

    /**
     * @brief Optional qm_iot_init() session handle.
     *
     * When set, qm_agent waits for an auth token before starting transport.
     */
    void *iot_handle;

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
    /** @brief Optional HTTP auth configuration used when that channel is built. */
    qm_agent_auth_http_config_t auth_http;
#endif

    /**
     * @brief Optional preload built-in module names loaded during qm_agent_create().
     *
     * Runtime loading through qm_agent_load_module() is preferred for new
     * code. This field remains available for compatibility when applications
     * want qm_agent_create() to preload a static module list.
     */
    const char * const *module_names;

    /**
     * @brief Optional preload module configuration list matched by module_names.
     *
     * Each entry is forwarded to the matching module init callback. Pass NULL
     * when no preload module needs extra configuration.
     */
    const void * const *module_configs;

    /** @brief Number of entries in module_names and optional module_configs. */
    uint32_t module_count;

    /**
     * @brief Internal module name storage used by qm_agent_config_add_module().
     *
     * Applications should ignore this field.
     */
    const char *managed_module_names[QM_AGENT_MAX_MODULES];

    /**
     * @brief Internal module config storage used by qm_agent_config_add_module().
     *
     * Applications should ignore this field.
     */
    const void *managed_module_configs[QM_AGENT_MAX_MODULES];
} qm_agent_config_t;

/**
 * @brief Initialize a qm_agent_config_t with default values.
 *
 * Pass a zero-initialized config, or one previously initialized by this
 * helper, before preserving caller-selected fields.
 *
 * The helper first keeps any caller-selected transport type, custom transport
 * interface, shared audio handle, video drivers, and existing module list
 * fields. When the transport fields are not set, it falls back to the
 * transports available in the current build and leaves the audio handles
 * unchanged. Audio stream formats are derived from the Engine during
 * qm_agent_create(). This helper currently fills recommended video defaults
 * only.
 * 此辅助函数保留调用方已选择的传输、音频、视频和模块字段；缺省传输从当前构建中选择，
 * 音频格式在 qm_agent_create() 时由引擎推导，目前仅补充推荐的视频默认值。
 *
 * @param config Configuration object to initialize.
 */
void qm_agent_config_init_defaults(qm_agent_config_t *config);

/**
 * @brief Append one managed module to qm_agent_config_t.
 *
 * The helper appends one preload module name into the internal storage owned
 * by qm_agent_config_t and increments module_count. Applications only need to
 * add the modules they want qm_agent_create() to preload for one session; the
 * internal capacity is controlled by QM_AGENT_MAX_MODULES.
 * 本函数将一个预加载模块名写入 qm_agent_config_t 自有存储并递增 module_count；容量由
 * QM_AGENT_MAX_MODULES 限制。
 *
 * @param config Configuration object to update.
 * @param module_name Built-in module name to append.
 * @param module_config Optional module-specific init configuration.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_config_add_module(qm_agent_config_t *config,
                               const char *module_name,
                               const void *module_config);

/**
 * @brief Create and initialize a qm_agent session.
 * 创建并初始化 qm_agent 会话。成功后应在同一应用任务中启动、停止及销毁该会话。
 *
 * @param config Session configuration. Must not be NULL and must provide
 *               config->audio.control when audio is
 *               enabled.
 * @return Session handle on success, NULL on failure.
 */
qm_agent_handle_t qm_agent_create(const qm_agent_config_t *config);

/**
 * @brief Load one managed module into an existing qm_agent session.
 *
 * Call this after qm_agent_create() and before qm_agent_start(). The module
 * runtime is initialized immediately, while start callbacks and cloud method
 * registration still follow the normal qm_agent_start() path.
 * 在 qm_agent_create() 后、qm_agent_start() 前调用。模块运行时立即初始化，启动回调和云端
 * 方法注册仍遵循正常 qm_agent_start() 路径。
 *
 * @param handle Session handle.
 * @param module_name Built-in module name.
 * @param module_config Optional module-specific runtime configuration passed to
 * the resolved module init callback. Pass NULL when the module does not
 * require extra settings.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_load_module(qm_agent_handle_t handle,
                         const char *module_name,
                         const void *module_config);

/**
 * @brief Destroy a qm_agent session.
 *
 * Stop external API calls and network notifications before calling this API.
 * Do not call it from an Agent event, TTS, module, or transport callback.
 * A failure leaves the handle valid for a subsequent destroy retry; no new
 * session work may be started after teardown has begun. The borrowed Audio
 * handles and callback contexts must remain valid until destruction succeeds.
 * 调用前应停止外部 API 和网络通知，且不得从 Agent 事件、TTS、模块或传输回调中调用。
 * 失败时句柄仍有效并可重试销毁；成功前借用的音频句柄和回调上下文必须继续有效。
 *
 * @param handle Session handle, or NULL for a no-op.
 * @return QM_EOK after all owned resources have been released. Only this
 *         result invalidates the handle; negative qm_errno means retry.
 */
int qm_agent_destroy(qm_agent_handle_t handle);

/**
 * @brief Start a qm_agent session.
 *
 * Serialize lifecycle calls from one application task. Event callbacks must
 * request lifecycle changes through that task rather than call them inline.
 * 生命周期调用应由一个应用任务串行执行。事件回调必须将后续生命周期操作投递回该任务，
 * 不得内联调用。
 *
 * @param handle Session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_start(qm_agent_handle_t handle);

/**
 * @brief Stop a qm_agent session.
 *
 * On failure the session remains STOPPING and owns its resources. Retry stop
 * before restarting or destroying it. Capture and upload stop before the
 * transport is stopped.
 * 失败时会话保持 STOPPING 并继续拥有资源，应在重启或销毁前重试 stop。采集和上传会先于
 * 传输停止。
 *
 * @param handle Session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_stop(qm_agent_handle_t handle);

/**
 * @brief Send user text input to the active transport.
 * 向活动传输发送 UTF-8 用户文本。
 *
 * @param handle Session handle.
 * @param text Null-terminated UTF-8 text.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_send_text(qm_agent_handle_t handle, const char *text);

/** @brief Terminal or progress state reported for one active TTS request. 单个活动 TTS 请求的进度或终态。 */
typedef enum {
    QM_AGENT_TTS_EVENT_STARTED = 0,
    QM_AGENT_TTS_EVENT_COMPLETED,
    QM_AGENT_TTS_EVENT_CANCELLED,
    QM_AGENT_TTS_EVENT_TIMEOUT,
    QM_AGENT_TTS_EVENT_FAILED,
} qm_agent_tts_event_t;

/**
 * @brief Callback for one active TTS request.
 *
 * The callback runs on the reporting transport, audio, timeout, or API task.
 * Keep it short and do not call lifecycle or TTS request APIs inline. Queue
 * follow-up commands to the application's owning task. The context remains
 * borrowed until the terminal callback returns.
 * 回调可运行在传输、音频、超时或 API 任务中，应保持短小；不得内联调用生命周期或 TTS
 * 请求接口，而应投递给应用拥有任务。上下文借用至终态回调返回。
 */
typedef void (*qm_agent_tts_cb_t)(qm_agent_handle_t handle,
                                  uint32_t request_id,
                                  qm_agent_tts_event_t event,
                                  int code,
                                  void *user_ctx);

/** @brief One-shot active TTS request owned by qm_agent. 由 qm_agent 管理的一次性活动 TTS 请求。 */
typedef struct {
    /** @brief Null-terminated UTF-8 text; copied before request_tts returns. */
    const char *text;
    /** @brief End-to-end timeout, zero disables the timeout. */
    uint32_t timeout_ms;
    /** @brief Optional lifecycle callback. */
    qm_agent_tts_cb_t callback;
    /** @brief Opaque callback context borrowed until the request completes. */
    void *user_ctx;
} qm_agent_tts_request_t;

/**
 * @brief Start one active TTS transaction.
 *
 * qm_agent owns request serialization, request IDs, timeout, cancellation,
 * and completion. Only one active request is allowed per session; callers
 * must cancel or wait for completion before starting another request.
 * qm_agent 负责请求串行化、请求 ID、超时、取消和完成。每个会话同时只允许一个活动请求；
 * 再次请求前必须取消或等待前一请求完成。
 */
int qm_agent_tts_request(qm_agent_handle_t handle,
                         const qm_agent_tts_request_t *request,
                         uint32_t *request_id);

/**
 * @brief Cancel one active TTS transaction.
 *
 * Closes local input and cancels the request immediately. An Audio operation
 * still completing its callback is retained for cleanup on qm_agent_ctl.
 * 立即关闭本地输入并取消请求；正在完成回调的音频操作会保留到 qm_agent_ctl 中清理。
 *
 * @param handle Session handle.
 * @param request_id ID returned by qm_agent_tts_request().
 * @return QM_EOK when cancellation was accepted, negative qm_errno otherwise.
 */
int qm_agent_tts_cancel(qm_agent_handle_t handle, uint32_t request_id);

/**
 * @brief Send one upstream audio frame to the active transport.
 *
 * This API is intended for applications that manage capture or encoding
 * outside qm_agent and need to inject already prepared audio frames into the
 * current backend session. Bytes must match the negotiated uplink format;
 * this API neither encodes nor plays the supplied data.
 * Automatic capture must be disabled. Admission never waits for queue space;
 * accepted bytes are consumed or copied before return. Do not retry a rejected
 * frame from a capture callback.
 * 此接口面向应用自行采集或编码的场景。数据必须符合协商上行格式，自动采集必须关闭；
 * 成功接收的数据会在返回前被消费或复制，采集回调中被拒绝的帧不得立即重试。
 *
 * @param handle Session handle.
 * @param data Audio frame bytes.
 * @param len Audio byte length.
 * @return QM_EOK after acceptance; -QM_EBUSY if automatic capture is enabled,
 * admission is closed, or a lock is contended; -QM_EFULL on queue pressure;
 * another negative qm_errno value on failure.
 */
int qm_agent_send_audio(qm_agent_handle_t handle,
                        const void *data,
                        uint32_t len);

/**
 * @brief Ask the active transport to enter long-form ASR mode.
 * 请求活动传输进入长语音识别模式。
 *
 * @param handle Session handle.
 * @param timeout_ms Transport queue wait timeout in milliseconds. Zero uses
 * the transport default when supported.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_asr_start_longform_recognition(qm_agent_handle_t handle,
                                            uint32_t timeout_ms);

/**
 * @brief Ask the active transport to exit long-form ASR mode.
 * 请求活动传输退出长语音识别模式。
 *
 * @param handle Session handle.
 * @param timeout_ms Transport queue wait timeout in milliseconds. Zero uses
 * the transport default when supported.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_asr_stop_longform_recognition(qm_agent_handle_t handle,
                                           uint32_t timeout_ms);

/**
 * @brief Enable or disable transport-managed realtime ASR feedback.
 * 启用或关闭由传输管理的实时 ASR 回传。
 *
 * @param handle Session handle.
 * @param enabled QM_TRUE enables realtime ASR feedback, QM_FALSE disables it.
 * @param timeout_ms Transport queue wait timeout in milliseconds. Zero uses
 * the transport default when supported.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_asr_set_realtime_enabled(qm_agent_handle_t handle,
                                      qm_bool enabled,
                                      uint32_t timeout_ms);

/**
 * @brief Send a video frame to the active transport.
 * 向活动传输发送视频帧。data 只在本次调用期间借用。
 *
 * @param handle Session handle.
 * @param data Video frame bytes, borrowed only for the duration of the call.
 * @param len Video byte length.
 * @param format Optional frame format override. Passing NULL uses config.input_video.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_send_video(qm_agent_handle_t handle,
                     const void *data,
                     uint32_t len,
                     const qm_agent_video_format_t *format);

/**
 * @brief Interrupt current agent output or request.
 * 中断当前 Agent 输出或请求。
 *
 * @param handle Session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_interrupt(qm_agent_handle_t handle);

/**
 * @brief Set persistent user intent for microphone upload; enabled by default.
 *
 * Disabling immediately closes transport admission. Engine subscription
 * gating is applied by qm_agent_ctl. Reconnect, playback completion, interrupt
 * and stop/start preserve this setting. Internal dialog/capture policy may
 * still suppress upload while the user setting is enabled.
 * 关闭时立即关闭传输准入，Engine 订阅门控由 qm_agent_ctl 执行。重连、播放完成、中断和
 * 停止/启动都保留此设置；即使用户设置为开启，内部对话或采集策略仍可暂时抑制上传。
 */
void qm_agent_set_upload_enabled(qm_agent_handle_t handle, qm_bool enabled);

/**
 * @brief Copy the current LJYD resource token into caller-owned storage.
 * Serialized with authentication refresh on the Agent control task.
 * Returns ENOENT without a token, EFULL if capacity (including NUL) is too small.
 * 将当前 LJYD resource token 拷贝到调用方存储。该操作与控制任务中的认证刷新串行化；
 * 无 token 返回 ENOENT，容量（含 NUL）不足返回 EFULL。
 */
int qm_agent_copy_resource_token(qm_agent_handle_t handle, char *token,
                                  uint32_t capacity);

/**
 * @brief Get current session state.
 * 获取当前会话状态；NULL 句柄返回 QM_AGENT_STATE_NONE。
 *
 * @param handle Session handle.
 * @return Current state, or QM_AGENT_STATE_NONE if handle is NULL.
 */
qm_agent_state_t qm_agent_get_state(qm_agent_handle_t handle);

/**
 * @brief Get the immutable identifier carried by public Agent event notices.
 *
 * Register QM_AGENT_EVENT_BASE before creating a session when the initial
 * READY notification is needed. The returned identifier remains valid only
 * as an opaque value for correlating copied qm_agent_event_notice_t payloads;
 * it does not grant access to a session handle.
 * 如需初始 READY 通知，应在创建会话前注册 QM_AGENT_EVENT_BASE。返回值只能作为关联已复制
 * qm_agent_event_notice_t 的不透明标识，不能用于访问会话句柄。
 *
 * @param handle Session handle.
 * @return Nonzero session identifier, or zero when handle is NULL.
 */
uint32_t qm_agent_get_id(qm_agent_handle_t handle);

/**
 * @brief Cache IP network availability and notify a supported transport.
 * May be called before start and concurrently with transport start/stop.
 * Does not change the desired agent lifecycle or create a stopped session.
 * Defaults to UP for callers that do not supply network notifications.
 * Callers must stop notifications before destroying the agent handle.
 * 可在启动前调用，并可与传输启动/停止并发。它不改变期望生命周期，也不会创建已停止会话；
 * 未上报时默认网络为 UP。销毁句柄前调用方必须停止网络通知。
 * @return QM_EOK on success; -QM_EINVAL for an invalid handle or state.
 */
int qm_agent_network_update(qm_agent_handle_t handle, qm_agent_network_state_t state);

#ifdef __cplusplus
}
#endif

#endif
