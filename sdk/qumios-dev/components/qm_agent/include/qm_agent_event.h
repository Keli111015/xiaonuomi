#ifndef QM_AGENT_EVENT_H
#define QM_AGENT_EVENT_H

/**
 * @file qm_agent_event.h
 * @brief Transport event normalization and public qm_event notices.
 *
 * 传输事件标准化与公共 qm_event 通知。适配层和驱动层使用 qm_agent_event_t 上报
 * 短生命周期事件；应用通过 QM_AGENT_EVENT_BASE 接收不含借用指针的
 * qm_agent_event_notice_t 通知。
 */

#include "qm_agent_types.h"
#include "qm_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event identifiers emitted by qm_agent.
 * qm_agent 发出的事件标识。AUDIO 和 VIDEO 是高频借用回调；其他低频事件可转换为
 * QM_AGENT_EVENT_BASE 上的 qm_agent_event_notice_t。
 */
typedef enum {
    /** @brief No event. */
    QM_AGENT_EVENT_NONE = 0,

    /** @brief Session state changed. */
    QM_AGENT_EVENT_STATE_CHANGED,

    /** @brief Transport connection was established. */
    QM_AGENT_EVENT_CONNECTED,

    /** @brief Transport connection was closed. */
    QM_AGENT_EVENT_DISCONNECTED,

    /** @brief Audio data is available. */
    QM_AGENT_EVENT_AUDIO,

    /** @brief ASR text is available. */
    QM_AGENT_EVENT_ASR,

    /** @brief TTS text or metadata is available. */
    QM_AGENT_EVENT_TTS,

    /** @brief Agent text response is available. */
    QM_AGENT_EVENT_AGENT_TEXT,

    /** @brief Agent state changed. */
    QM_AGENT_EVENT_AGENT_STATE,

    /** @brief Agent output was aborted or interrupted. */
    QM_AGENT_EVENT_AGENT_ABORTED,

    /**
     * @brief Transport finished sending the current TTS stream.
     *
     * This only closes the network input. Local buffered audio, whether PCM or
 * encoded, and the speaker may still be playing; wait for
 * QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE before treating the reply as
 * audible-complete.
 * 该事件只关闭网络输入，PCM 或编码的本地缓冲音频及扬声器仍可能在播放；必须等待
 * QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE 才能视为用户实际听完。
     */
    QM_AGENT_EVENT_TTS_INPUT_COMPLETE,

    /** @brief Local TTS queue and playback device have fully drained. */
    QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE,

    /** @brief Local TTS playback started for one generation. */
    QM_AGENT_EVENT_TTS_PLAYBACK_STARTED,

    /** @brief TTS input, decode, admission or playback failed; code gives the cause. */
    QM_AGENT_EVENT_TTS_PLAYBACK_FAILED,

    /** @brief Runtime error was reported. */
    QM_AGENT_EVENT_ERROR,

    /** @brief Video data is available. */
    QM_AGENT_EVENT_VIDEO,

    /** @brief Direction-of-arrival data is available from a capable audio driver. */
    QM_AGENT_EVENT_DOA,

    /** @brief Remote agent user joined the RTC channel. */
    QM_AGENT_EVENT_REMOTE_USER_JOINED,

    /** @brief Remote agent user left the RTC channel. */
    QM_AGENT_EVENT_REMOTE_USER_LEFT,

    /** @brief Remote agent user muted audio. */
    QM_AGENT_EVENT_REMOTE_AUDIO_MUTED,

    /** @brief Remote agent user unmuted audio. */
    QM_AGENT_EVENT_REMOTE_AUDIO_UNMUTED,

    /** @brief Ordered start of a finite reply; data.audio.format gives its format.
     * This is a control notice, with no audio data pointer or bytes. */
    QM_AGENT_EVENT_TTS_INPUT_BEGIN,

    /** @brief Provider-neutral expression event. Agent 下发的供应商无关表情事件。 */
    QM_AGENT_EVENT_EXPRESSION,

    /** @brief Provider device-command document. / 供应商设备指令文档。 */
    QM_AGENT_EVENT_COMMAND,

} qm_agent_event_id_t;

/**
 * @brief Provider-neutral facial expression for application rendering. 面向应用渲染的供应商无关面部表情。
 *
 * A transport maps its provider-specific code to this vocabulary before it
 * emits QM_AGENT_EVENT_EXPRESSION. UNKNOWN preserves forward compatibility
 * for a code that has no semantic mapping yet; applications may inspect the
 * accompanying provider_code for diagnostics or a product-specific fallback.
 * 适配层在发布 QM_AGENT_EVENT_EXPRESSION 前将供应商编码映射到本枚举。
 * 无法映射的新编码使用 UNKNOWN，应用可通过 provider_code 诊断或按产品需求回退。
 */
typedef enum {
    /** @brief No expression or an unknown provider code. 未提供表情或供应商编码未知。 */
    QM_AGENT_EXPRESSION_UNKNOWN = 0,
    /** @brief Neutral expression. 中性表情。 */
    QM_AGENT_EXPRESSION_NEUTRAL,
    /** @brief Happy or pleased. 开心或愉悦。 */
    QM_AGENT_EXPRESSION_HAPPY,
    /** @brief Sad or sorrowful. 悲伤或悲痛。 */
    QM_AGENT_EXPRESSION_SAD,
    /** @brief Angry. 愤怒。 */
    QM_AGENT_EXPRESSION_ANGRY,
    /** @brief Fearful. 恐惧。 */
    QM_AGENT_EXPRESSION_FEAR,
    /** @brief Surprised or shocked. 惊讶或震惊。 */
    QM_AGENT_EXPRESSION_SURPRISED,
    /** @brief Anxious. 焦虑。 */
    QM_AGENT_EXPRESSION_ANXIOUS,
    /** @brief Excited. 兴奋。 */
    QM_AGENT_EXPRESSION_EXCITED,
    /** @brief Expectant. 期待。 */
    QM_AGENT_EXPRESSION_EXPECTANT,
    /** @brief Curious or inquisitive. 好奇。 */
    QM_AGENT_EXPRESSION_CURIOUS,
    /** @brief Confused. 困惑。 */
    QM_AGENT_EXPRESSION_CONFUSED,
    /** @brief Hesitant. 犹豫。 */
    QM_AGENT_EXPRESSION_HESITANT,
    /** @brief Shy. 害羞。 */
    QM_AGENT_EXPRESSION_SHY,
    /** @brief Affectionate or coquettish. 亲昵或撒娇。 */
    QM_AGENT_EXPRESSION_AFFECTIONATE,
    /** @brief Disappointed. 失望。 */
    QM_AGENT_EXPRESSION_DISAPPOINTED,
    /** @brief Disgusted or hateful. 厌恶或反感。 */
    QM_AGENT_EXPRESSION_DISGUSTED,
    /** @brief Calm or peaceful. 平静。 */
    QM_AGENT_EXPRESSION_CALM,
    /** @brief Unwell or sick. 难受或身体不适。 */
    QM_AGENT_EXPRESSION_UNWELL,
    /** @brief Bored or weary of an activity. 厌倦或无聊。 */
    QM_AGENT_EXPRESSION_BORED,
} qm_agent_expression_t;

/**
 * @brief Broad semantic category of a provider-neutral expression event. 供应商无关表情事件的一级语义分类。
 *
 * This category lets an application choose an animation group without
 * depending on a provider's vocabulary. Transports should set it when a
 * source code is known to belong to a category but has no concrete QUMI enum
 * yet. qm_agent derives it from a known concrete enum when it is omitted.
 * 一级分类让应用选择动画组。新供应商码没有具体 QUMI 枚举但已知分类时，
 * 适配层应填写本字段；已知具体枚举且未填写时由 qm_agent 自动推导。
 */
typedef enum {
    /** @brief Missing or unrecognized expression. 缺失或无法识别的表情。 */
    QM_AGENT_EXPR_CATEGORY_UNKNOWN = 0,
    /** @brief Non-emotional facial baseline. 非情感性的面部基线状态。 */
    QM_AGENT_EXPR_CATEGORY_FACIAL_STATE,
    /** @brief Basic affect, such as HAPPY, SAD, or ANGRY. 基础情感，如开心、悲伤或愤怒。 */
    QM_AGENT_EXPR_CATEGORY_AFFECT,
    /** @brief Social or cognitive signal, such as CURIOUS or SHY. 社会认知信号，如好奇或害羞。 */
    QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE,
    /** @brief Physical state, currently UNWELL. 生理身体状态，如难受不适。 */
    QM_AGENT_EXPR_CATEGORY_PHYSICAL_STATE,
    /** @brief Gesture or pose action, such as NOD. 姿态或动作，如点头。 */
    QM_AGENT_EXPR_CATEGORY_GESTURE,
} qm_agent_expression_category_t;

/**
 * @brief Return a known facial expression's broad semantic category. 返回已知面部表情的一级语义分类。
 *
 * @param[in] expression Provider-neutral facial expression. 供应商无关的面部表情。
 * @return Stable category, or UNKNOWN for an unrecognized value. 稳定一级分类；无法识别时返回 UNKNOWN。
 */
qm_agent_expression_category_t qm_agent_expression_classify(
    qm_agent_expression_t expression);

/**
 * @brief Provider-neutral gesture accompanying an expression event. 表情事件附带的供应商无关姿态动作。
 *
 * Gestures remain separate from facial expressions because applications often
 * render them through a body/action animation rather than an expression asset.
 * 动作独立于面部表情，因为应用通常以肢体或姿态动画渲染动作。
 */
typedef enum {
    /** @brief No gesture was supplied. 未提供姿态动作。 */
    QM_AGENT_GESTURE_NONE = 0,
    /** @brief Nodding gesture. 点头动作。 */
    QM_AGENT_GESTURE_NOD,
    /** @brief Head-shaking gesture. 摇头动作。 */
    QM_AGENT_GESTURE_SHAKE_HEAD,
} qm_agent_gesture_t;

/**
 * @brief Normalized terminal reason for an agent text stream.
 *
 * Non-agent text events always use QM_AGENT_TEXT_FINISH_REASON_NONE.
 * Agent 文本流的标准化结束原因。非 Agent 文本事件始终使用
 * QM_AGENT_TEXT_FINISH_REASON_NONE。
 */
typedef enum {
    /** @brief The text stream is not terminal. */
    QM_AGENT_TEXT_FINISH_REASON_NONE = 0,
    /** @brief The agent completed normally. */
    QM_AGENT_TEXT_FINISH_REASON_STOP,
    /** @brief The agent stopped after a provider-side error. */
    QM_AGENT_TEXT_FINISH_REASON_ERROR,
    /** @brief The input or output was rejected by audit policy. */
    QM_AGENT_TEXT_FINISH_REASON_AUDIT,
    /** @brief An ACTIVITY response was rejected by content filtering. */
    QM_AGENT_TEXT_FINISH_REASON_CONTENT_FILTER,
    /** @brief A provider reported an unrecognized non-empty terminal reason. */
    QM_AGENT_TEXT_FINISH_REASON_UNKNOWN,
} qm_agent_text_finish_reason_t;

/**
 * @brief Event producer category.
 * 事件产生方分类，用于区分核心、传输、音频引擎和板级驱动。
 */
typedef enum {
    /** @brief Event originated from qm_agent core. */
    QM_AGENT_EVENT_SOURCE_CORE = 0,

    /** @brief Event originated from a transport adapter. */
    QM_AGENT_EVENT_SOURCE_TRANSPORT,

    /** @brief Event originated from the audio engine. */
    QM_AGENT_EVENT_SOURCE_AUDIO,

    /** @brief Event originated from an audio driver. */
    QM_AGENT_EVENT_SOURCE_DRIVER,
} qm_agent_event_source_t;

/**
 * @brief Default qm_event base used for public Agent notifications.
 *
 * Applications register a handler on this base with qm_event_handler_register().
 * The handler event_id is a qm_agent_event_id_t value and event_data points to a
 * qm_agent_event_notice_t value.
 * 应用使用 qm_event_handler_register() 注册该 event base。处理器的 event_id 为
 * qm_agent_event_id_t，event_data 指向 qm_agent_event_notice_t。
 */
QM_EVENT_DECLARE_BASE(QM_AGENT_EVENT_BASE);

#ifndef CONFIG_QM_AGENT_EVENT_TEXT_BYTES
/** @brief Default fixed text snapshot capacity, including the NUL terminator. 默认固定文本快照容量，包含 NUL 终止符。 */
#define CONFIG_QM_AGENT_EVENT_TEXT_BYTES 192U
#endif

#if CONFIG_QM_AGENT_EVENT_TEXT_BYTES < 2
#error "CONFIG_QM_AGENT_EVENT_TEXT_BYTES must reserve one character and a NUL terminator"
#endif

/** @brief Capacity of a copied text snapshot in qm_agent_event_notice_t. qm_agent_event_notice_t 中复制文本快照的容量。 */
#define QM_AGENT_EVENT_TEXT_BYTES CONFIG_QM_AGENT_EVENT_TEXT_BYTES

/**
 * @brief Fixed-size text snapshot carried by a low-rate public notice.
 *
 * text contains at most QM_AGENT_EVENT_TEXT_BYTES - 1 bytes plus a NUL
 * terminator. text_length is the number of bytes copied, and truncated is set
 * when the source text did not fit. The struct contains no borrowed pointer.
 * text 最多包含 QM_AGENT_EVENT_TEXT_BYTES - 1 个字节及 NUL 终止符；text_length 为
 * 已复制字节数，源文本放不下时 truncated 置位。该结构不含任何借用指针。
 */
typedef struct {
    /** @brief NUL-terminated copied text, or an empty string when unavailable. */
    char text[QM_AGENT_EVENT_TEXT_BYTES];
    /** @brief Number of non-NUL bytes copied into text. */
    uint32_t text_length;
    /** @brief Source text exceeded the fixed snapshot capacity. */
    qm_bool truncated;
    /** @brief Final-result flag for ASR, TTS, and agent text notifications. */
    qm_bool final;
    /** @brief Agent stream terminal reason; NONE for other text notifications. */
    qm_agent_text_finish_reason_t finish_reason;
    /** @brief Optional duration reported with a TTS notification. */
    uint32_t duration_ms;
} qm_agent_event_text_notice_t;

/**
 * @brief Pointer-free payload delivered through QM_AGENT_EVENT_BASE.
 *
 * qm_event copies this value before queuing it. The payload does not contain a
 * qm_agent handle, internal object, audio bytes, or video bytes, so it remains
 * safe to deliver after the originating Agent session has been destroyed.
 *
 * The event_id passed to the qm_event handler selects the active data member.
 * QM_AGENT_EVENT_AUDIO and QM_AGENT_EVENT_VIDEO are intentionally never posted
 * to this base; they remain borrowed high-rate data-plane callbacks inside
 * qm_agent and its adapters.
 * qm_event 在入队前复制此值，因此它不含 qm_agent 句柄、内部对象、音频或视频字节，
 * 原始会话销毁后仍可安全投递。AUDIO 和 VIDEO 不会投递到该 base，而保留为组件内部的
 * 高频借用数据面回调。
 */
typedef struct {
    /** @brief Nonzero opaque Agent session identifier for notice correlation. */
    uint32_t agent_id;
    /** @brief Active TTS request identifier when the notification has one. */
    uint32_t request_id;
    /** @brief Active TTS/audio generation when the notification has one. */
    uint32_t generation;
    /** @brief Producer category copied from the normalized internal event. */
    qm_agent_event_source_t source;
    /** @brief Backend-specific status or error code. */
    int code;
    union {
        /** @brief Valid for QM_AGENT_EVENT_STATE_CHANGED. */
        struct {
            qm_agent_state_t old_state;
            qm_agent_state_t new_state;
        } state;
        /** @brief Valid for ASR, TTS, AGENT_TEXT, and ERROR notifications. */
        qm_agent_event_text_notice_t text;
        /**
         * @brief Command snapshots for diagnostics or small commands. / 诊断或短指令快照。
         * Never execute truncated JSON. Modules receive the complete borrowed
         * document and may clone it for asynchronous work.
         * 禁止执行已截断 JSON；模块可接收完整借用文档并克隆后异步处理。
         */
        struct {
            qm_agent_event_text_notice_t domain; /*!< Command domain. / 指令领域。 */
            qm_agent_event_text_notice_t json;   /*!< Full provider envelope when not truncated. / 未截断时为完整供应商报文。 */
        } command;
        /** @brief Valid for QM_AGENT_EVENT_EXPRESSION. 仅 QM_AGENT_EVENT_EXPRESSION 有效。 */
        struct {
            /** @brief Provider-neutral facial expression. 供应商无关的面部表情。 */
            qm_agent_expression_t visual;
            /** @brief Provider-neutral body/action gesture. 供应商无关的姿态动作。 */
            qm_agent_gesture_t gesture;
            /** @brief Broad category of the visual expression or gesture. 面部表情或动作的一级分类。 */
            qm_agent_expression_category_t category;
            /** @brief Provider-neutral speech expression when supplied. 可选的供应商无关语音表情。 */
            qm_agent_expression_t speech_expression;
            /** @brief Broad category of the optional speech expression. 可选语音表情的一级分类。 */
            qm_agent_expression_category_t speech_category;
            /** @brief Original provider visual-expression code snapshot. 原始供应商面部表情编码快照。 */
            qm_agent_event_text_notice_t provider_code;
            /** @brief Original provider speech-expression code snapshot. 原始供应商语音表情编码快照。 */
            qm_agent_event_text_notice_t provider_speech_code;
        } expression;
        /** @brief Valid for QM_AGENT_EVENT_AGENT_STATE. */
        struct {
            qm_agent_dialog_state_t state;
            qm_agent_event_text_notice_t detail;
        } agent_state;
        /** @brief Valid for QM_AGENT_EVENT_DOA. */
        qm_agent_doa_info_t doa;
        /** @brief Valid for remote-user notifications. */
        struct {
            uint32_t uid;
            int reason;
            qm_bool muted;
        } remote_user;
    } data;
} qm_agent_event_notice_t;

/**
 * @brief Normalized transport or driver event.
 *
 * This is the internal adapter-to-Agent contract. Pointer payloads are borrowed
 * by qm_agent only for the duration of the transport or driver callback. It is
 * not the public default-loop payload; applications receive the pointer-free
 * qm_agent_event_notice_t through QM_AGENT_EVENT_BASE instead.
 * 这是适配器到 Agent 的内部契约。指针载荷只在传输或驱动回调期间由 qm_agent 借用；它不是
 * 公共默认事件循环载荷，应用应通过 QM_AGENT_EVENT_BASE 接收无指针的通知副本。
 */
typedef struct {
    /** @brief Event identifier. */
    qm_agent_event_id_t id;

    /** @brief Event producer category. */
    qm_agent_event_source_t source;

    /** @brief Optional backend-specific status or error code. */
    int code;

    /** @brief Event-specific payload. */
    union {
        /**
         * @brief State change payload.
         */
        struct {
            /** @brief Previous session state. */
            qm_agent_state_t old_state;

            /** @brief New session state. */
            qm_agent_state_t new_state;
        } state;

        /**
         * @brief Audio payload.
         */
        struct {
            /** @brief Audio bytes. */
            const uint8_t *data;

            /** @brief Audio byte length. */
            uint32_t len;

            /** @brief Audio format. */
            qm_agent_audio_format_t format;
        } audio;

        /**
         * @brief Video payload.
         */
        struct {
            /** @brief Video bytes. */
            const uint8_t *data;

            /** @brief Video byte length. */
            uint32_t len;

            /** @brief Video format. */
            qm_agent_video_format_t format;
        } video;

        /**
         * @brief Direction-of-arrival payload.
         */
        struct {
            /** @brief Normalized DOA information. */
            qm_agent_doa_info_t info;
        } doa;

        /** @brief Complete borrowed command document. / 完整借用指令文档。 */
        struct {
            const char *domain; /*!< Optional domain. / 可选领域。 */
            const char *json;   /*!< Provider JSON, valid during callback. / 仅回调期间有效的供应商 JSON。 */
        } command;
        /**
         * @brief ASR payload.
         */
        struct {
            /** @brief ASR text. */
            const char *text;

            /** @brief Whether this is a final ASR result. */
            qm_bool final;
        } asr;

        /**
         * @brief TTS payload.
         */
        struct {
            /** @brief TTS text or metadata. */
            const char *text;

            /** @brief Optional TTS duration in milliseconds. */
            uint32_t duration_ms;

            /** @brief Whether this is a final TTS result. */
            qm_bool final;
        } tts;

        /**
         * @brief Local TTS playback lifecycle payload.
         *
         * generation is a qm_agent-local monotonically increasing value. It
         * lets applications ignore notifications from an interrupted reply.
         * generation 是 qm_agent 本地单调递增值，应用可据此忽略被中断回复的旧通知。
         */
        struct {
            uint32_t generation;
        } tts_playback;

        /**
         * @brief Agent text payload.
         * Agent 文本载荷。
         */
        struct {
            /** @brief Agent text content. */
            const char *text;

            /** @brief Whether this is a final agent text result. */
            qm_bool final;

            /** @brief Provider terminal reason when final is QM_TRUE. */
            qm_agent_text_finish_reason_t finish_reason;
        } agent_text;

        /**
         * @brief Provider-neutral expression payload. 供应商无关的表情事件载荷。
         *
         * visual and gesture drive application assets. A transport sets
         * category for a future provider code that has no concrete visual or
         * gesture enum; qm_agent derives it for known concrete enums when the
         * field is omitted. speech_expression describes the optional voice
         * style. The provider fields preserve opaque source values for
         * diagnostics and product-specific fallback.
         * visual 和 gesture 用于选择应用动画。未来供应商码没有具体枚举时，
         * 适配层填写 category；已知枚举且未填写时由 qm_agent 推导。
         * provider 字段保留原始编码，用于诊断和产品定制回退。
         */
        struct {
            /** @brief Provider-neutral facial expression. 供应商无关的面部表情。 */
            qm_agent_expression_t visual;
            /** @brief Provider-neutral body/action gesture. 供应商无关的姿态动作。 */
            qm_agent_gesture_t gesture;
            /** @brief Broad category of the visual expression or gesture. 面部表情或动作的一级分类。 */
            qm_agent_expression_category_t category;
            /** @brief Provider-neutral speech expression when supplied. 可选的供应商无关语音表情。 */
            qm_agent_expression_t speech_expression;
            /** @brief Broad category of the optional speech expression. 可选语音表情的一级分类。 */
            qm_agent_expression_category_t speech_category;
            /** @brief Borrowed provider visual-expression code. 借用的原始供应商面部表情编码。 */
            const char *provider_code;
            /** @brief Borrowed provider speech-expression code. 借用的原始供应商语音表情编码。 */
            const char *provider_speech_code;
        } expression;

        /**
         * @brief Agent state payload.
         * Agent 状态载荷。
         */
        struct {
            /** @brief Normalized agent state. */
            qm_agent_dialog_state_t state;

            /** @brief Optional state detail text. */
            const char *text;
        } agent_state;

        /**
         * @brief Error payload.
         * 错误载荷。
         */
        struct {
            /** @brief Error message. */
            const char *message;
        } error;

        /**
         * @brief Remote RTC user payload.
         * 远端 RTC 用户载荷。
         */
        struct {
            /** @brief Remote RTC user identifier. */
            uint32_t uid;

            /** @brief Backend-specific leave reason when available. */
            int reason;

            /** @brief Whether the remote user's audio is muted. */
            qm_bool muted;
        } remote_user;
    } data;
} qm_agent_event_t;

/**
 * @brief Deep-copy an event and all pointer payloads owned by qm_agent.
 * 深拷贝事件及 qm_agent 所有的全部指针载荷。dst 必须随后由 qm_agent_event_free() 释放。
 *
 * @param dst Destination event.
 * @param src Source event.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_event_clone(qm_agent_event_t *dst, const qm_agent_event_t *src);

/**
 * @brief Release payload memory held by a cloned event.
 * 释放克隆事件持有的载荷内存，并清零事件。
 *
 * @param event Event to release and clear.
 */
void qm_agent_event_free(qm_agent_event_t *event);

#ifdef __cplusplus
}
#endif

#endif
