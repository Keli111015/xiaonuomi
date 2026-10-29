/**
 * @file joyinside_client_internal.h
 * @brief Internal cross-module state for JoyInside Client extensions. / JoyInside Client 扩展的跨模块内部状态。
 *
 * @warning This header is private to the component and does not define an
 * application API. / 本头文件仅供组件内部使用，不定义应用层 API。
 */

#ifndef JOYINSIDE_CLIENT_INTERNAL_H
#define JOYINSIDE_CLIENT_INTERNAL_H

#include "joyinside_client_audiobook.h"
#include "joyinside_client_print.h"
#include "qm_kernel.h"

/**
 * @brief Stored audiobook registration for one client. / 单个客户端保存的有声书注册信息。
 *
 * Registration is permitted only while stopped. stop preserves this context;
 * destroy releases it only after workers have exited. content_type in @p config
 * is owned by this context after registration, while callback and user_context
 * remain borrowed. 仅能在停止状态注册；stop 保留本上下文，destroy 会在工作线程
 * 退出后释放。注册完成后 config.content_type 由本上下文持有，回调和 user_context
 * 仍为借用数据。
 */
typedef struct {
    joyinside_client_handle_t handle;                 /*!< Owning client; valid until unregister/destroy. / 所属客户端，至注销或销毁前有效。 */
    joyinside_client_audiobook_config_t config;       /*!< Stored handler config; see ownership above. / 已保存的处理器配置，所有权见上文。 */
} joyinside_audiobook_handler_context_t;

/**
 * @brief Stored PRINT registration for one client. / 单个客户端保存的 PRINT 注册信息。
 *
 * The structure is copied; callback and user_context remain borrowed until
 * unregister or destroy. 结构会被复制；回调和 user_context 在注销或销毁前均为借用数据。
 */
typedef struct {
    joyinside_client_handle_t handle;                 /*!< Owning client; valid until unregister/destroy. / 所属客户端，至注销或销毁前有效。 */
    joyinside_client_print_config_t config;           /*!< Stored handler config with borrowed callback/context. / 包含借用回调与上下文的已保存配置。 */
} joyinside_print_handler_context_t;

/**
 * @brief Private extension state embedded in each client handle. / 嵌入每个客户端句柄的私有扩展状态。
 *
 * This object has the same lifetime as its client and is not independently
 * allocated. 本对象与 client 生命周期相同，不单独分配。
 */
typedef struct {
    joyinside_audiobook_handler_context_t audiobook;  /*!< AUDIO_BOOK registration. / AUDIO_BOOK 注册信息。 */
    joyinside_print_handler_context_t print;          /*!< PRINT registration. / PRINT 注册信息。 */
    void (*on_disconnect)(void *context);             /*!< Optional component-owned disconnect hook. / 可选的组件内部断连回调。 */
    void *media_context;                              /*!< Borrowed context paired with on_disconnect. / 与 on_disconnect 配对的借用上下文。 */
} joyinside_client_extensions_t;

/**
 * @brief Return the extension state embedded in @p handle. / 返回嵌入 @p handle 的扩展状态。
 *
 * @param[in] handle Client handle. / 客户端句柄。
 * @return Component-owned state, or NULL for NULL handle. The pointer expires
 *         when the client is destroyed. / 组件持有的状态；handle 为 NULL 时返回
 *         NULL。客户端销毁后该指针失效。
 */
joyinside_client_extensions_t *joyinside_client_extensions(joyinside_client_handle_t handle);

/**
 * @brief Return whether client runtime resources exist. / 返回客户端运行时资源是否已创建。
 *
 * This is an internal lifecycle guard for extension registration. It tests the
 * WebSocket object, not remote connection readiness. 用于扩展注册的内部生命周期
 * 保护；检查的是 WebSocket 对象，而非远端连接是否已就绪。
 *
 * @param[in] handle Client handle. / 客户端句柄。
 * @return QM_TRUE when runtime resources exist, otherwise QM_FALSE. / 运行时资源
 *         存在时返回 QM_TRUE，否则返回 QM_FALSE。
 */
qm_bool joyinside_client_running(joyinside_client_handle_t handle);

/**
 * @brief Validate and dispatch a received AUDIO_BOOK message.
 * / 校验并派发一条收到的 AUDIO_BOOK 消息。
 *
 * Called only by the client downlink dispatcher. The JSON tree is borrowed for
 * this call; AUDIO_BOOK_PONG is delivered through the registered callback. Only
 * the callback result is returned; it must not retain JSON pointers. 仅由客户端
 * 下行派发器调用。JSON 树仅在本调用内借用；AUDIO_BOOK_PONG 通过已注册回调交付。
 * 仅返回回调结果，回调不得保留 JSON 指针。
 *
 * @param[in] handle JoyInside Client handle. / JoyInside Client 句柄。
 * @param[in] root Borrowed complete downlink JSON object. / 借用的完整下行 JSON 对象。
 * @return QM_EOK if ignored or dispatched, negative error code on invalid data.
 *         / 忽略或成功派发返回 QM_EOK，数据非法时返回负错误码。
 */
int joyinside_client_audiobook_handle_message(joyinside_client_handle_t handle,
                                              const struct cJSON *root);

/**
 * @brief Validate and dispatch a received PRINT message.
 * / 校验并派发一条收到的 PRINT 消息。
 *
 * Called only by the client downlink dispatcher. The JSON tree is borrowed for
 * this call; registered callbacks must copy values before asynchronous work.
 * 仅由客户端下行派发器调用。JSON 树仅在本调用内借用；已注册回调若需异步处理必须复制数据。
 *
 * @param[in] handle JoyInside Client handle. / JoyInside Client 句柄。
 * @param[in] root Borrowed complete downlink JSON object. / 借用的完整下行 JSON 对象。
 * @return QM_EOK if ignored or dispatched, negative error code on invalid data.
 *         / 忽略或成功派发返回 QM_EOK，数据非法时返回负错误码。
 */
int joyinside_client_print_handle_message(joyinside_client_handle_t handle,
                                          const struct cJSON *root);

/**
 * @brief Schedule AUDIO_BOOK PING on the core worker. / 在核心工作线程调度 AUDIO_BOOK PING。
 *
 * A zero period or NULL callback cancels the schedule. send_ping remains
 * borrowed while the schedule is active and runs on the core worker, so it must
 * not block or call stop/destroy. period 为零或回调为 NULL 时取消调度。调度期间
 * send_ping 为借用数据，且运行在核心工作线程，不得阻塞或调用 stop/destroy。
 *
 * @param[in] handle Client handle. / 客户端句柄。
 * @param[in] period_ms Period in milliseconds; zero cancels. / 周期毫秒数；零表示取消。
 * @param[in] send_ping Borrowed PING sender, or NULL to cancel. / 借用的 PING 发送函数；NULL 表示取消。
 * @return QM_EOK or a negative error. / 成功或负错误码。
 */
int joyinside_client_schedule_audiobook_ping(joyinside_client_handle_t handle,
                                             uint32_t period_ms,
                                             int (*send_ping)(joyinside_client_handle_t));

/**
 * @brief Snapshot WS identity and Bearer header into an HTTP request. / 快照 WS 身份及 Bearer 请求头。
 *
 * This helper copies current identity under the client lock. root is owned by
 * the caller on entry and return; authorization is allocated only on success.
 * 本函数在客户端锁内复制当前身份。root 的所有权始终归调用方；仅成功时分配
 * authorization。
 *
 * @param[in] handle Connected client. / 已连接客户端。
 * @param[in,out] root JSON object receiving botId, sessionId and requestId. / 接收会话字段的 JSON 对象。
 * @param[out] authorization Owned header; caller frees with qm_free. / 调用方使用 qm_free 释放的请求头。
 * @return QM_EOK or a negative error. / 成功或负错误码。
 */
int joyinside_client_http_context(joyinside_client_handle_t handle,
                                 struct cJSON *root, char **authorization);
#endif
