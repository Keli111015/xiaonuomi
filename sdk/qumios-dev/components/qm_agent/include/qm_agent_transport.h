#ifndef QM_AGENT_TRANSPORT_H
#define QM_AGENT_TRANSPORT_H

/**
 * @file qm_agent_transport.h
 * @brief Transport adapter configuration and interface definitions.
 *
 * 传输适配器配置与接口定义。内置和自定义适配器都将供应商回调标准化为
 * qm_agent_event_t；输入数据只在回调期间借用，适配器必须在返回前消费或复制。
 */

#include "qm_agent_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Agora transport configuration.
 * Agora 传输配置。所有字符串均由调用方保持到会话销毁。
 */
typedef struct {
    /** @brief Agora application identifier. */
    const char *app_id;

    /** @brief Agora channel name. */
    const char *channel_name;

    /** @brief Agora channel token. */
    const char *token;

    /** @brief Agora license value, when required by the SDK. */
    const char *license;

    /** @brief Local Agora user id. */
    uint32_t uid;

    /** @brief Whether audio subscription is enabled. */
    qm_bool audio_enable;

    /** @brief Whether video subscription is enabled. */
    qm_bool video_enable;

    /** @brief Optional local video stream format. */
    qm_agent_video_format_t video_format;
} qm_agent_agora_config_t;

/**
 * @brief baidu_chat_agent transport configuration.
 * baidu_chat_agent 传输配置。认证信息通常由 qm_agent_auth 自动更新。
 */
typedef struct {
    /** @brief Query field `a`. */
    const char *app_id;

    /** @brief Query field `id`, required for qm_agent Baidu transport auth. */
    const char *instance_id;

    /** @brief Query field `t`, required for qm_agent Baidu transport auth. */
    const char *token;

    /** @brief Query field `ac`, typically sourced from local audio format or auth reply. */
    const char *audio_codec;

    /** @brief Query field `plen`, only used when qm_agent input audio is opus. */
    uint32_t packet_max_len;

    /** @brief Optional extra HTTP headers. */
    const char *headers;

    /** @brief Optional business user identifier for local correlation. */
    const char *user_id;

    /** @brief Optional business device identifier for local correlation. */
    const char *device_id;

    /** @brief Optional purchased license key used for Baidu `[LIC]:[ACTIVE]` activation. */
    const char *license_key;
} qm_agent_baidu_chat_agent_config_t;

/**
 * @brief JoyInside transport configuration.
 * JoyInside 传输配置。自动认证启用时，uid 会被 LJYD userId 覆盖。
 */
typedef struct {
    /** @brief JoyInside bot identifier. */
    const char *bot_id;

    /** @brief JoyInside access token. */
    const char *access_token;

    /** @brief End-user identifier. Auto auth overwrites this with LJYD userId. */
    const char *uid;

    /** @brief Chat role code. */
    const char *chat_role_code;

    /** @brief Request timeout in milliseconds. */
    uint32_t timeout_ms;
    /** @brief Optional stable session ID; NULL creates one. / 可选稳定会话 ID，NULL 自动生成。 */
    const char *session_id;
    /** @brief Optional feature and client type. / 可选功能及客户端类型。 */
    const char *feature;
    const char *client_type;
    /** @brief Optional connection location. / 可选建连位置。 */
    const char *location;
    /** @brief Request ASR start/partial events. / 请求 ASR 开始及中间事件。 */
    qm_bool need_asr_partial;
    /** @brief Request MP3 streaming output. / 请求 MP3 流式下发。 */
    qm_bool output_stream;
    /** @brief Token callback, called before each connection; buffer length includes NUL on input. / 每次连接前获取 Token；输入长度为含 NUL 容量，成功输出长度不含 NUL。 */
    int (*token_provider)(void *context, qm_bool force_refresh, char *token, uint32_t *token_len);
    /** @brief Borrowed token callback context. / 借用 Token 回调上下文。 */
    void *token_context;
} qm_agent_joyinside_config_t;

/**
 * @brief lgeine voice WebSocket transport configuration.
 *
 * Credential and identity pointers are borrowed for the session lifetime.
 *
 * Transport buffer, timeout, and task tunables (network/hello/reconnect
 * timeouts, WebSocket receive buffer, message size limit, task stack size,
 * priority, and send queue size) are intentionally not exposed here; they are
 * governed by the lgeine_agent component's `CONFIG_LGEINE_AGENT_*` Kconfig
 * defaults. Only connection identity and connection-behavior switches are
 * surfaced to the application.
 * 凭据和身份字符串由调用方保持到会话结束。缓冲区、超时和任务参数由 lgeine_agent 的
 * CONFIG_LGEINE_AGENT_* Kconfig 控制，本配置只暴露连接身份和行为开关。
 */
typedef struct {
    /** @brief lgeine WebSocket endpoint (wss://). When NULL, the endpoint is
     *         built from `env`. */
    const char *uri;

    /**
     * @brief lgeine deployment environment used to build the endpoint when
     *         `uri` is NULL.
     *
     * Integer values match the lgeine_agent environment enum: 0 = default
     * (resolved to production internally), 1 = dev, 2 = test, 3 = prod.
     * Values outside 0..3 are rejected by the lgeine adapter before start.
     */
    int32_t env;

    /** @brief Product identifier sent in the WebSocket header. Required. */
    const char *pid;

    /** @brief Device serial number sent in the WebSocket header. Required. */
    const char *sn;

    /** @brief Bearer token sent in the Authorization header. Required. */
    const char *access_token;

    /** @brief Disable automatic reconnect after transport or handshake failure. */
    qm_bool disable_auto_reconnect;

    /** @brief Disable WebSocket ping keepalive. */
    qm_bool disable_ping;
} qm_agent_lgeine_config_t;

/**
 * @brief Transport configuration.
 * 传输配置。type 选择 union 中有效的配置成员，CUSTOM 的 custom 指针属于应用。
 */
typedef struct {
    /** @brief Selected transport type. */
    qm_agent_transport_type_t type;

    /** @brief Transport-specific configuration. */
    union {
        /** @brief Agora transport configuration. */
        qm_agent_agora_config_t agora;

        /** @brief baidu_chat_agent transport configuration. */
        qm_agent_baidu_chat_agent_config_t baidu_chat_agent;

        /** @brief JoyInside transport configuration. */
        qm_agent_joyinside_config_t joyinside;

        /** @brief lgeine voice WebSocket transport configuration. */
        qm_agent_lgeine_config_t lgeine;

        /** @brief Application-owned custom transport configuration. */
        void *custom;
    } data;
} qm_agent_transport_config_t;

/**
 * @brief Callback used by transport adapters to emit normalized events.
 *
 * Finite TTS streams should emit TTS_INPUT_BEGIN before their first AUDIO,
 * with data.audio.format set and the remaining audio fields zeroed. This
 * schedules admission early; request_tts() already admits its PUSH before
 * invoking the provider. Adapters must preserve these boundaries and discard
 * old reply packets after an interrupt acknowledgement or a new reply begins.
 * Streams without a begin marker use nonblocking first-frame admission and
 * can lose frames until the PUSH is ready; do not claim lossless playback.
 * Finite TTS streams must emit TTS_INPUT_COMPLETE after their final AUDIO
 * event, in the same ordered delivery path. Text completion and remote dialog
 * state do not imply that local playback has completed. Continuous RTC audio
 * must not synthesize input completion from independently delivered subtitles.
 * 有限 TTS 流必须在首个 AUDIO 前发布 TTS_INPUT_BEGIN，并在最后一个 AUDIO 后按相同顺序
 * 发布 TTS_INPUT_COMPLETE。文本完成和远端对话状态不表示本地播放完成；连续 RTC 音频不得
 * 根据独立到达的字幕伪造输入完成事件。
 *
 * @param event Borrowed event and payload pointers, valid only during the call.
 * @param user_ctx User context passed to qm_agent_transport_if_t::init.
 */
typedef void (*qm_agent_transport_event_cb_t)(const qm_agent_event_t *event,
                                           void *user_ctx);

/**
 * @brief Transport adapter interface.
 * 传输适配器接口。实现应将供应商事件转换为 qm_agent_event_t，严格遵守回调借用期与
 * init/start/stop/deinit 生命周期。stop 与 deinit 只会在已关闭新数据准入后调用。
 */
typedef struct {
    /** @brief Human-readable adapter name. */
    const char *name;

    /**
     * @brief Initialize a transport instance.
     *
     * @param transport_ctx Output transport context.
     * @param config Transport configuration.
     * @param input_format Wire codec and actual send_audio payload format. The
     * shared Engine supplies PCM; an adapter without an encoder must reject a
     * different wire codec. Unsupported geometry must fail before start.
     * @param output_format Output audio format received from the backend.
     * @param input_video_format Input video format sent to the backend.
     * @param output_video_format Output video format received from the backend.
     * @param event_cb Event callback into qm_agent core.
     * @param user_ctx User context passed back with event_cb.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*init)(void **transport_ctx,
                const qm_agent_transport_config_t *config,
                const qm_agent_audio_format_t *input_format,
                const qm_agent_audio_format_t *output_format,
                const qm_agent_video_format_t *input_video_format,
                const qm_agent_video_format_t *output_video_format,
                qm_agent_transport_event_cb_t event_cb,
                void *user_ctx);

    /**
     * @brief Deinitialize a transport instance.
     *
 * Success guarantees no further callbacks or borrowed-data access. On
 * failure the context remains valid and owned by the caller for retry.
 * 成功后必须保证不再发起回调或访问借用数据；失败时调用方仍持有有效上下文，可重试。
     *
     * @param transport_ctx Transport context from init.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*deinit)(void *transport_ctx);

    /**
     * @brief Start the transport session.
     *
     * @param transport_ctx Transport context from init.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*start)(void *transport_ctx);

    /**
     * @brief Stop the transport session.
     *
     * @param transport_ctx Transport context from init.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*stop)(void *transport_ctx);


    /**
     * @brief Admit upstream audio without waiting for network or queue space.
     *
     * Called from Engine capture context. Copy or consume the whole frame in
     * bounded time, otherwise return -QM_EBUSY/-QM_EFULL. Do not invoke the
     * event callback inline or retain borrowed data. Encoding and network
 * writes belong to the provider's execution context. Stop/deinit run only
 * after admission is closed and active calls return.
 * 此调用来自音频采集上下文，必须在有界时间内消费或复制完整帧；不得内联发布事件或保存
 * 借用指针。编码和网络发送应在供应商任务中完成。
     *
     * @param transport_ctx Transport context from init.
     * @param data Borrowed bytes in input_format.data_type. Consume or copy
     * them before returning; asynchronous sends must not retain this pointer.
     * @param len Audio byte length.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*send_audio)(void *transport_ctx, const void *data, uint32_t len);

    /**
     * @brief Send upstream video to the transport.
     *
     * Called from the video driver's frame-push callback. Consume or copy the
     * complete borrowed frame in bounded time. Do not invoke the transport
     * event callback or re-enter any qm_agent API inline, because the Agent
 * holds its transport-admission lock during this call. Stop/deinit run only
 * after admitted sends return.
 * 此调用来自视频驱动推帧回调，必须在有界时间内消费或复制完整帧；不得内联触发事件或
 * 重入 qm_agent API，因为调用期间 Agent 持有传输准入锁。
     *
     * @param transport_ctx Transport context from init.
     * @param data Borrowed video bytes; do not retain after returning.
     * @param len Video byte length.
     * @param format Video frame format.
     * @return QM_EOK on success, otherwise a negative qm_errno value.
     */
    int (*send_video)(void *transport_ctx,
                      const void *data,
                      uint32_t len,
                      const qm_agent_video_format_t *format);

    /**
     * @brief Send user text input to the transport.
     *
     * @param transport_ctx Transport context from init.
     * @param text Null-terminated UTF-8 text.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*send_text)(void *transport_ctx, const char *text);

    /**
     * @brief Request standalone TTS output.
     *
     * @param transport_ctx Transport context from init.
     * @param text Null-terminated UTF-8 text to synthesize.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*request_tts)(void *transport_ctx, const char *text);

    /**
     * @brief Interrupt current agent output or request.
     *
     * @param transport_ctx Transport context from init.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*interrupt)(void *transport_ctx);

    /**
     * @brief Start transport-managed long-form ASR mode.
     *
     * @param transport_ctx Transport context from init.
     * @param timeout_ms Queue wait timeout in milliseconds. Zero uses the
     * transport default when supported.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*asr_start_longform)(void *transport_ctx, uint32_t timeout_ms);

    /**
     * @brief Stop transport-managed long-form ASR mode.
     *
     * @param transport_ctx Transport context from init.
     * @param timeout_ms Queue wait timeout in milliseconds. Zero uses the
     * transport default when supported.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*asr_stop_longform)(void *transport_ctx, uint32_t timeout_ms);

    /**
     * @brief Enable or disable transport-managed realtime ASR feedback.
     *
     * @param transport_ctx Transport context from init.
     * @param enabled QM_TRUE enables realtime ASR feedback, QM_FALSE disables it.
     * @param timeout_ms Queue wait timeout in milliseconds. Zero uses the
     * transport default when supported.
     * @return QM_EOK on success, negative qm_errno value on failure.
     */
    int (*asr_set_realtime_enabled)(void *transport_ctx,
                                    qm_bool enabled,
                                    uint32_t timeout_ms);

    /**
     * @brief Optional network notification. Must not dispatch callbacks or wait
     * for connection shutdown. Cache the state for subsequent starts.
     * 可选网络状态通知。不得分发回调或等待连接关闭，应缓存状态供后续启动使用。
     */
    int (*network_update)(void *transport_ctx, qm_agent_network_state_t state);
} qm_agent_transport_if_t;

/**
 * @brief Get the built-in transport adapter for a type.
 * 获取指定类型的内置传输适配器；当前构建未启用时返回 NULL。
 *
 * @param type Transport type.
 * @return Transport interface, or NULL if no built-in adapter exists.
 */
const qm_agent_transport_if_t *qm_agent_transport_get_default(qm_agent_transport_type_t type);

/**
 * @brief Get the Agora transport adapter.
 * 获取 Agora 传输适配器。
 *
 * @return Agora transport interface.
 */
const qm_agent_transport_if_t *qm_agent_transport_agora(void);

/**
 * @brief Get the baidu_chat_agent transport adapter.
 * 获取 baidu_chat_agent 传输适配器。
 *
 * @return baidu_chat_agent transport interface.
 */
const qm_agent_transport_if_t *qm_agent_transport_baidu_chat_agent(void);

/**
 * @brief Get the JoyInside transport adapter.
 * 获取 JoyInside 传输适配器。
 *
 * @return JoyInside transport interface.
 */
const qm_agent_transport_if_t *qm_agent_transport_joyinside(void);

/**
 * @brief Get the lgeine transport adapter.
 * 获取 lgeine 传输适配器。
 *
 * @return lgeine transport interface.
 */
const qm_agent_transport_if_t *qm_agent_transport_lgeine(void);

#ifdef __cplusplus
}
#endif

#endif
