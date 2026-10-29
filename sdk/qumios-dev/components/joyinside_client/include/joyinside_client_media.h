/**
 * @file joyinside_client_media.h
 * @brief Asynchronous AUDIO_BOOK media controller API. / 异步 AUDIO_BOOK 媒体控制器 API。
 *
 * The controller serializes backend operations on its media worker and uses a
 * generation value to reject stale backend notifications.
 *
 * @par 中文说明
 * 控制器在独立媒体工作线程中串行调用播放器后端，并使用 generation 拒绝过期的后端通知。
 */

#ifndef JOYINSIDE_CLIENT_MEDIA_H
#define JOYINSIDE_CLIENT_MEDIA_H

#include "joyinside_client_audiobook.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque per-client AUDIO_BOOK controller; owns no decoder or device. / 每客户端一个的 AUDIO_BOOK 控制器；不拥有解码器或音频设备。 */
typedef struct joyinside_client_media joyinside_client_media_t;

typedef enum {
    JOYINSIDE_CLIENT_MEDIA_IDLE = 0, /*!< No active resource. */
    JOYINSIDE_CLIENT_MEDIA_BRIDGE_WAIT, /*!< Waiting for actual bridge TTS playback drain. */
    JOYINSIDE_CLIENT_MEDIA_STARTING, /*!< Player accepted a resource, awaiting STARTED. */
    JOYINSIDE_CLIENT_MEDIA_PLAYING, /*!< Resource is audible. */
    JOYINSIDE_CLIENT_MEDIA_STOPPING, /*!< Awaiting a terminal player notification. */
} joyinside_client_media_state_t;

/** @brief Backend request; strings are borrowed through start() return only. / 播放器后端请求；字符串仅保证在 start() 返回前有效。 */
typedef struct {
    const char *audio_url; /*!< Complete MP3 URL, never truncated. */
    uint64_t progress_sec; /*!< Required absolute start position in seconds. */
    uint64_t total_length_sec; /*!< Resource length supplied by the server. */
} joyinside_client_media_play_t;

/**
 * @brief Nonblocking player operations, called only from the media worker. / 仅由媒体工作线程调用的非阻塞播放器操作。
 *
 * Successful start/stop means accepted, not completed. Report completion with
 * media_notify(). A failed start/wait_bridge must leave no playback or callbacks active.
 * A terminal notification guarantees the backend is quiescent for that generation.
 * A failed stop retains ownership and may be retried. Context outlives destroy.
 * Never destroy the controller from a component callback.
 *
 * @par 中文说明
 * start/stop 成功只表示后端接受请求，完成状态必须通过 media_notify() 回报。终态通知表示该 generation
 * 后端已静止；stop 失败时仍由后端持有资源，可重试。context 必须存活到 destroy 成功，禁止在组件回调中销毁控制器。
 */
typedef struct {
    int (*start)(void *context, uint64_t generation, const joyinside_client_media_play_t *play);
    int (*stop)(void *context, uint64_t generation);
    /** @brief Request a bridge-drain notification. Include server TTS completion
     * AND local speaker drain; an empty queue alone is insufficient. Optional
     * only when the service never sends includeTTS=true.
     * / 请求串场音频排空通知，必须同时包含服务端 TTS 完成和本地扬声器排空；仅当服务端永不发送
     * includeTTS=true 时可省略。 */
    int (*wait_bridge)(void *context, uint64_t generation);
    void *context;
} joyinside_client_media_backend_t;

/** @brief Player observations; progress is absolute resource seconds. / 播放器观察事件；progress 为资源的绝对秒数。 */
typedef enum {
    JOYINSIDE_CLIENT_MEDIA_BRIDGE_DRAINED = 0,
    JOYINSIDE_CLIENT_MEDIA_STARTED,
    JOYINSIDE_CLIENT_MEDIA_PROGRESS,
    JOYINSIDE_CLIENT_MEDIA_FINISHED, /*!< Natural completion; backend is quiescent. */
    JOYINSIDE_CLIENT_MEDIA_STOPPED, /*!< Stop/preemption complete; backend is quiescent. */
    JOYINSIDE_CLIENT_MEDIA_FAILED, /*!< Playback failed; backend is quiescent. */
} joyinside_client_media_event_t;

/**
 * @brief Media controller configuration. / 媒体控制器配置。
 *
 * The structure is copied at create time. Backend and callback contexts remain
 * borrowed and must outlive joyinside_client_media_destroy().
 *
 * @par 中文说明
 * create 时复制本结构；后端与回调上下文仍为借用数据，必须存活至 joyinside_client_media_destroy() 成功。
 */
typedef struct {
    joyinside_client_handle_t client; /*!< Stopped client; one media controller per client. */
    joyinside_client_media_backend_t backend; /*!< Required start/stop implementation. */
    /** @brief Optional WS-worker policy gate; return false to reject promptly. / 可选 WebSocket 工作线程策略门；返回 false 立即拒绝播放。 */
    qm_bool (*can_play)(const joyinside_client_audiobook_request_t *request, void *context);
    /** @brief Optional media-worker notification, including the current playback generation. / 可选媒体工作线程状态通知，携带当前播放 generation。 */
    void (*on_state)(joyinside_client_media_state_t state, uint64_t generation, void *context);
    /** @brief Optional media-worker result; error is negative on failure/interruption. / 可选媒体工作线程结果通知；失败或打断时 error 为负值。 */
    void (*on_finished)(uint64_t progress_sec, qm_bool finish, int error, void *context);
    void *context;
    uint32_t task_stack_size; /*!< Zero selects Kconfig default. */
    uint32_t task_priority; /*!< Zero selects Kconfig default. */
    uint32_t queue_len; /*!< Zero selects Kconfig default. */
} joyinside_client_media_config_t;

/**
 * @brief Attach a controller to a stopped client. / 为已停止的客户端挂载媒体控制器。
 *
 * @param[in] config Controller and backend configuration.
 * @param[out] out Created controller on success.
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_media_create(const joyinside_client_media_config_t *config,
                                  joyinside_client_media_t **out);

/**
 * @brief Stop and join the controller. / 停止并等待控制器工作线程退出。
 *
 * Stop the JoyInside client first to quiesce downlink callbacks.
 * Timeout/error retains a valid handle: complete backend stop and retry. Success
 * invalidates the handle. No concurrent calls except final backend notifications.
 *
 * @par 中文说明
 * 应先停止 JoyInside 客户端以静默下行回调。超时或失败时句柄仍有效，后端完成停止后可重试；成功后句柄失效。
 * 除最终后端通知外，不允许并发调用。
 *
 * @param[in] media Media controller.
 * @param[in] timeout_ms Wait limit for worker exit.
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_media_destroy(joyinside_client_media_t *media, uint32_t timeout_ms);

/**
 * @brief Asynchronously cancel current and pending media. / 异步取消当前和待播放的媒体。
 *
 * @param[in] media Media controller.
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_media_interrupt(joyinside_client_media_t *media);

/**
 * @brief Post a backend observation, including from start or stop callbacks. / 上报播放器后端观察事件，可从 start 或 stop 回调调用。
 * Stale generations return -QM_ENOENT. Queue-full returns -QM_EFULL: retry
 * terminal notifications. Never access this controller after destroy succeeds.
 *
 * @par 中文说明
 * 过期 generation 返回 -QM_ENOENT；队列满返回 -QM_EFULL，终态通知必须重试。destroy 成功后禁止再次访问控制器。
 *
 * @param[in] media Media controller.
 * @param[in] generation Generation supplied to the backend operation.
 * @param[in] event Backend observation.
 * @param[in] progress_sec Absolute playback progress in seconds.
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_media_notify(joyinside_client_media_t *media, uint64_t generation,
                                  joyinside_client_media_event_t event, uint64_t progress_sec);

/**
 * @brief Read a thread-safe state snapshot. / 读取线程安全的状态快照。
 *
 * @param[in] media Media controller.
 * @param[out] generation Optional current generation.
 * @return Current media state, or JOYINSIDE_CLIENT_MEDIA_IDLE for NULL.
 */
joyinside_client_media_state_t joyinside_client_media_get_state(
    joyinside_client_media_t *media, uint64_t *generation);

#ifdef __cplusplus
}
#endif
#endif
