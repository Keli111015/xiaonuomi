/**
 * @file joyinside_client_audiobook.h
 * @brief AUDIO_BOOK protocol extension API. / AUDIO_BOOK 协议扩展 API。
 *
 * Incoming requests and their cJSON values are borrowed for the callback.
 * / 下行请求及其 cJSON 值均为回调期间借用的数据。
 */

#ifndef JOYINSIDE_CLIENT_AUDIOBOOK_H
#define JOYINSIDE_CLIENT_AUDIOBOOK_H

#include "joyinside_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JOYINSIDE_CLIENT_HANDLE_T_DEFINED
#define JOYINSIDE_CLIENT_HANDLE_T_DEFINED
typedef struct joyinside_client *joyinside_client_handle_t;
#endif

#define JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY "AUDIO_BOOK_PLAY"
#define JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP "AUDIO_BOOK_STOP"
#define JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PING "AUDIO_BOOK_PING"
#define JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PONG "AUDIO_BOOK_PONG"
#define JOYINSIDE_CLIENT_AUDIOBOOK_CONTENT_TYPE "AUDIO_BOOK"
#define JOYINSIDE_CLIENT_AUDIOBOOK_PING_INTERVAL_MAX_MS 2000U

/**
 * @brief Root metadata parsed from an audiobook message. / 从有声书消息解析出的根元数据。
 *
 * Matches the common JoyInside Client text message metadata. String pointers
 * are borrowed from the current JoyInside event and are valid only during the
 * callback.
 */
typedef joyinside_client_message_meta_t joyinside_client_audiobook_meta_t;

/**
 * @brief content-level audiobook fields. / content 层的有声书字段。
 */
typedef struct {
    const char *round_id;                                /*!< content.roundId */
    const char *event_type;                              /*!< content.eventType */
} joyinside_client_audiobook_content_t;

/**
 * @brief content.eventData audiobook fields. / content.eventData 中的有声书字段。
 */
typedef struct {
    const char *book_id;                                 /*!< eventData.bookId */
    const char *book_name;                               /*!< eventData.bookName */
    const char *chapter;                                 /*!< eventData.chapter */
    const char *chapter_id;                              /*!< eventData.chapterId */
    const char *chapter_name;                            /*!< eventData.chapterName */
    const char *audio_url;                               /*!< eventData.audioURL */
    const char *image_url;                               /*!< eventData.imageURL */
    qm_bool progress_present;                            /*!< QM_TRUE when eventData.progress is present */
    uint64_t progress;                                   /*!< eventData.progress, 0 if absent */
    uint64_t total_length;                               /*!< eventData.totalLength, 0 if absent */
    qm_bool include_tts;                                 /*!< eventData.includeTTS, QM_FALSE when absent */
} joyinside_client_audiobook_event_data_t;

/**
 * @brief Normalized audiobook request passed to the application media layer. / 传递给应用媒体层的标准化有声书请求。
 *
 * String and cJSON pointers are borrowed from the current JoyInside event and
 * are valid only during the callback.
 *
 * @par 中文说明
 * 字符串和 cJSON 指针借用自当前 JoyInside 事件，仅在回调期间有效；入队异步处理前必须复制。
 */
typedef struct {
    joyinside_client_handle_t handle;                    /*!< JoyInside Client handle */
    joyinside_client_audiobook_meta_t meta;              /*!< Root message fields */
    joyinside_client_audiobook_content_t content_info;   /*!< content fields */
    joyinside_client_audiobook_event_data_t event_data_info; /*!< content.eventData fields */
    const struct cJSON *content;                         /*!< Borrowed content object */
    const struct cJSON *event_data;                      /*!< Borrowed content.eventData object */
} joyinside_client_audiobook_request_t;

/**
 * @brief Application-owned audiobook content callback. / 应用持有的有声书内容回调。
 *
 * The callback runs in the JoyInside Client event path. The request and all
 * nested pointers are borrowed for the callback; copy data before queuing work.
 * Return quickly if player operations can block.
 *
 * @par 中文说明
 * 回调运行在 JoyInside 事件路径。request 及嵌套指针都是借用数据；发送到工作队列前必须复制。
 * 若播放器操作可能阻塞，回调必须快速返回。
 */
typedef int (*joyinside_client_audiobook_command_cb_t)(
    const joyinside_client_audiobook_request_t *request,
    void *user_context);

/**
 * @brief Optional built-in audiobook skill handler configuration. / 可选的内建有声书技能处理器配置。
 */
typedef struct {
    joyinside_client_audiobook_command_cb_t on_command; /*!< Called for matched audiobook content */
    void *user_context;                                /*!< User context passed to on_command */
    const char *content_type;                          /*!< Expected contentType, NULL uses AUDIO_BOOK */
} joyinside_client_audiobook_config_t;

/**
 * @brief Register an audiobook handler for a JoyInside Client handle. / 为 JoyInside 客户端注册有声书处理器。
 *
 * The configuration is copied by the audiobook module, including content_type;
 * on_command and user_context remain borrowed. Call this after
 * joyinside_client_init() while the client is stopped, before start().
 * A media controller owns this registration until media_destroy() succeeds.
 *
 * @par 中文说明
 * 配置及 content_type 会被复制，on_command 和 user_context 为借用数据。仅能在客户端停止状态下、
 * start 前注册。媒体控制器在 destroy 成功前独占该注册。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] config Audiobook handler configuration
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_audiobook_register(joyinside_client_handle_t handle,
                                        const joyinside_client_audiobook_config_t *config);

/**
 * @brief Unregister the audiobook handler for a JoyInside Client handle. / 注销 JoyInside 客户端的有声书处理器。
 *
 * Also stops the internal AUDIO_BOOK_PING schedule for this handle.
 * Register/unregister only while stopped with no media controller attached;
 * otherwise return -QM_EBUSY.
 * stop preserves registrations; destroy releases them.
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_audiobook_unregister(joyinside_client_handle_t handle);

/**
 * @brief Send an AUDIO_BOOK uplink event. / 发送 AUDIO_BOOK 上行事件。
 *
 * The payload shape is:
 * {"mid":"...","contentType":"AUDIO_BOOK","t":timestamp_ms,
 *  "content":{"eventType":event_type,"eventData":event_data_json}}
 *
 * AUDIO_BOOK_PLAY requires bookId and chapterId in eventData.
 * AUDIO_BOOK_STOP requires bookId, chapterId, progress and finish in eventData.
 * AUDIO_BOOK_PING accepts an empty eventData object and is used by the
 * explicit heartbeat schedule. Sending any event does not start or stop that
 * schedule.
 *
 * @par 中文说明
 * PLAY 必须包含 bookId 和 chapterId；STOP 必须包含 bookId、chapterId、progress、finish。
 * PING 可使用空 eventData，但发送任意事件都不会自动改变媒体心跳状态。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] event_type AUDIO_BOOK_PLAY, AUDIO_BOOK_STOP, or internal AUDIO_BOOK_PING
 * @param[in] event_data_json JSON object string used as content.eventData, NULL sends {}
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_audiobook_send_event(joyinside_client_handle_t handle,
                                          const char *event_type,
                                          const char *event_data_json,
                                          uint32_t timeout_ms);

/**
 * @brief Start the internal AUDIO_BOOK_PING heartbeat schedule. / 启动内部 AUDIO_BOOK_PING 心跳调度。
 *
 * The interval is clamped to JOYINSIDE_CLIENT_AUDIOBOOK_PING_INTERVAL_MAX_MS.
 * Passing 0 uses JOYINSIDE_CLIENT_AUDIOBOOK_PING_INTERVAL_MAX_MS. The first
 * heartbeat is sent immediately, then maintained by the client core worker.
 * Start on receipt of PLAY, before bridge TTS. Periodic sends never block.
 *
 * @par 中文说明
 * 收到 PLAY 后、串场 TTS 前启动。周期发送不会阻塞调用线程，间隔会被限制在协议允许的最大值内。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] interval_ms Heartbeat interval in milliseconds
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_audiobook_start_ping(joyinside_client_handle_t handle,
                                          uint32_t interval_ms,
                                          uint32_t timeout_ms);

/**
 * @brief Stop the internal AUDIO_BOOK_PING heartbeat schedule for this handle. / 停止该客户端的内部 AUDIO_BOOK_PING 心跳。
 *
 * @param[in] handle JoyInside Client handle
 */
void joyinside_client_audiobook_stop_ping(joyinside_client_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
