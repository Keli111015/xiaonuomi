/**
 * @file joyinside_client_print.h
 * @brief PRINT protocol extension API. / PRINT 协议扩展 API。
 *
 * Incoming requests and their cJSON values are borrowed for the callback.
 * / 下行请求及其 cJSON 值均为回调期间借用的数据。
 */

#ifndef JOYINSIDE_CLIENT_PRINT_H
#define JOYINSIDE_CLIENT_PRINT_H

#include "joyinside_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JOYINSIDE_CLIENT_HANDLE_T_DEFINED
#define JOYINSIDE_CLIENT_HANDLE_T_DEFINED
typedef struct joyinside_client *joyinside_client_handle_t;
#endif

#define JOYINSIDE_CLIENT_PRINT_CONTENT_TYPE "PRINT"
/* Server downlink eventType values. */
#define JOYINSIDE_CLIENT_PRINT_EVENT_START "PRINT_START"
#define JOYINSIDE_CLIENT_PRINT_EVENT_IMAGE_DISPLAY "IMAGE_DISPLAY"
#define JOYINSIDE_CLIENT_PRINT_EVENT_STOP "PRINT_STOP"
#define JOYINSIDE_CLIENT_PRINT_EVENT_CONFIG "PRINT_CONFIG"
#define JOYINSIDE_CLIENT_PRINT_EVENT_STATUS_QUERY "PRINT_STATUS_QUERY"
/* Device uplink eventType values. */
#define JOYINSIDE_CLIENT_PRINT_EVENT_STATUS "PRINT_STATUS"
#define JOYINSIDE_CLIENT_PRINT_EVENT_SUCCESS "PRINT_SUCCESS"
#define JOYINSIDE_CLIENT_PRINT_EVENT_IMAGE "PRINT_IMAGE"

/**
 * @brief Normalized magic print request passed to the application. / 传递给应用的标准化魔法打印请求。
 *
 * String and cJSON pointers are borrowed from the current JoyInside event and
 * are valid only during the callback.
 *
 * @par 中文说明
 * 字符串和 cJSON 指针借用自当前 JoyInside 事件，仅在回调期间有效；异步处理前必须复制。
 */
typedef struct {
    joyinside_client_handle_t handle;                    /*!< JoyInside Client handle */
    joyinside_client_message_meta_t meta;                /*!< Root message fields, includes uid */
    const char *round_id;                                /*!< content.roundId */
    const char *event_type;                              /*!< content.eventType */
    const char *print_task_id;                           /*!< eventData.printTaskId */
    const char *print_thickness;                         /*!< eventData.printThickness */
    const char *paper_type;                              /*!< eventData.paperType */
    const char *print_direction;                         /*!< eventData.printDirection */
    const char *print_mode;                              /*!< eventData.printMode, PRINT_CONFIG */
    const char *img_url;                                 /*!< eventData.imgUrl, IMAGE_DISPLAY */
    const char *image_id;                                /*!< eventData.imageId, IMAGE_DISPLAY */
    const struct cJSON *img_url_list;                    /*!< eventData.imgUrlList array, PRINT_START */
    uint32_t img_count;                                  /*!< Number of imgUrlList entries */
    const struct cJSON *content;                         /*!< Borrowed content object */
    const struct cJSON *event_data;                      /*!< Borrowed content.eventData object */
} joyinside_client_print_request_t;

/**
 * @brief Application-owned magic print content callback. / 应用持有的魔法打印内容回调。
 *
 * The callback runs in the JoyInside Client event path. The request and all
 * nested pointers are borrowed for the callback; copy data before queuing work.
 * Return quickly if printer operations can block.
 *
 * @par 中文说明
 * 回调运行在 JoyInside 事件路径。request 及嵌套指针都是借用数据；发送到工作队列前必须复制。
 * 若打印机操作可能阻塞，回调必须快速返回。
 */
typedef int (*joyinside_client_print_command_cb_t)(
    const joyinside_client_print_request_t *request,
    void *user_context);

/**
 * @brief Magic print handler configuration. / 魔法打印处理器配置。
 */
typedef struct {
    joyinside_client_print_command_cb_t on_command;      /*!< Called for matched PRINT content */
    void *user_context;                                  /*!< User context passed to on_command */
} joyinside_client_print_config_t;

/**
 * @brief Register a magic print handler for a JoyInside Client handle. / 为 JoyInside 客户端注册魔法打印处理器。
 *
 * The configuration is copied by the print module; on_command and user_context
 * remain borrowed. Call this after joyinside_client_init() while the client is
 * stopped, before joyinside_client_start().
 *
 * @par 中文说明
 * 配置结构会被复制，on_command 和 user_context 为借用数据。仅能在客户端停止状态、start 前注册。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] config Print handler configuration
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_print_register(joyinside_client_handle_t handle,
                                    const joyinside_client_print_config_t *config);

/**
 * @brief Unregister the magic print handler for a JoyInside Client handle. / 注销 JoyInside 客户端的魔法打印处理器。
 *
 * Register/unregister only on a stopped client; running clients return
 * -QM_EBUSY.
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_print_unregister(joyinside_client_handle_t handle);

/**
 * @brief Send a PRINT uplink event. / 发送 PRINT 上行事件。
 *
 * The payload shape is:
 * {"mid":"...","uid":"...","contentType":"PRINT","t":timestamp_ms,
 *  "content":{"eventType":event_type,"eventData":event_data_json}}
 *
 * PRINT_STATUS requires battery and printerStatus[] in eventData.
 * PRINT_SUCCESS requires printTaskId and printSuccess (0/1).
 * PRINT_IMAGE requires printTaskId and imageId.
 *
 * @par 中文说明
 * PRINT_STATUS 的 eventData 必须包含 battery 和 printerStatus[]；PRINT_SUCCESS 必须包含
 * printTaskId、printSuccess；PRINT_IMAGE 必须包含 printTaskId、imageId。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] event_type PRINT_STATUS, PRINT_SUCCESS or PRINT_IMAGE
 * @param[in] event_data_json JSON object string used as content.eventData
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_print_send_event(joyinside_client_handle_t handle,
                                      const char *event_type,
                                      const char *event_data_json,
                                      uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
