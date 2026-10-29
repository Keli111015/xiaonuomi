/**
 * @file qm_iot_subdev.h
 * @brief subdev模块头文件, 提供子设备管理的能力
 *
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 */
#ifndef __QM_IOT_DEV_H__
#define __QM_IOT_DEV_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "qm_iot_api.h"
#include "qm_iot_mqtt.h"
#include "qm_iot_common.h"
#include "qm_iot_config.h"

#ifndef CONFIG_QM_IOT_DEV_IR_SUPPORT
#define CONFIG_QM_IOT_DEV_IR_SUPPORT           (0)
#endif

#ifndef CONFIG_QM_IOT_DEV_INFO_SUPPORT
#define CONFIG_QM_IOT_DEV_INFO_SUPPORT         (0)
#endif

#define  QM_IOT_DEV_COMM_REPLY_PAYLOAD_FMT     "{\"method\":\"%s\",\"id\":%u,\"code\":%d}"

typedef struct {
    uint32_t did;
    int state;
} qm_iot_dev_state_t;

/**
 * @brief dev模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    QM_IOT_DEVRECV_NONE,
    QM_IOT_DEVRECV_IR_NOTIFY,
    QM_IOT_DEVRECV_RESET_REPLY,
    QM_IOT_DEVRECV_BIND_NOTIFY,
    QM_IOT_DEVRECV_UNBIND_NOTIFY,
    QM_IOT_DEVRECV_REBOOT_NOTIFY,
    QM_IOT_DEVRECV_MAX
} qm_iot_dev_recv_type_t;

typedef struct {
    uint32_t             msg_id;
    int                  msg_len;
    union 
    {
        char             *msg;
    }data;
} qm_iot_dev_ir_notify_t;

typedef struct {
    qm_iot_code_t        code;
    uint32_t             msg_id;
} qm_iot_dev_reset_reply_t;

typedef struct {
    uint32_t            msg_id;
    union 
    {
        uint8_t         re;
    }data;
} qm_iot_dev_unbind_notify_t;

typedef struct {
    uint32_t            msg_id;
} qm_iot_dev_bind_notify_t;

typedef struct {
    uint32_t            msg_id;
} qm_iot_dev_reboot_notify_t;

/**
 * @brief dev模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_dev_recv_type_t
     */
    qm_iot_dev_recv_type_t type;
    union {

        qm_iot_dev_ir_notify_t  ir_notify;
        /**
         * @brief 从设备发起解绑请求消息后，收到的云端的应答
         */
        qm_iot_dev_reset_reply_t reset_reply;
        /**
         * @brief 收到的云端的解绑通知
         */
        qm_iot_dev_unbind_notify_t unbind_notify;
        /**
         * @brief 收到的云端的解绑通知
         */
        qm_iot_dev_bind_notify_t bind_notify;
        /**
         * @brief
         */
        qm_iot_dev_reboot_notify_t reboot_notify;
    } data;
} qm_iot_dev_recv_t;

/**
 * @brief dev模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle dev会话句柄
 * @param[in] packet dev消息结构体, 存放收到的dev报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_dev_recv_handler_t)(void *handle, const qm_iot_dev_recv_t *packet, void *user_data);

/**
 * @brief @ref qm_iot_dev_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_dev_setopt 中, data参数的数据类型
 *
 * 1. data的数据类型是char *时, 以配置@ref QM_IOT_SUBDEVOPT_MQTT_HANDLE 为例:
 *
 *    void *mqtt_handle = qm_iot_mqtt_init();
 *    qm_iot_dev_setopt(dev_handle, QM_IOT_DEVOPT_MQTT_HANDLE, mqtt_handle);
 */
typedef enum {
    /**
     * @brief dev会话 需要的MQTT句柄, 需要先建立MQTT连接，再设置MQTT句柄
     * 
     * @details
     * 
     * 数据类型: (void *)
     */
    QM_IOT_DEVOPT_MQTT_HANDLE,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户, 数据类型为(qm_iot_dev_recv_handler_t)
     * 
     * @details
     * 
     * 数据类型: (qm_iot_dev_recv_handler_t)
     */
    QM_IOT_DEVOPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文, 数据类型为(void *)
     *
     * @details 这个上下文指针会在 QM_IOT_DEVOPT_RECV_HANDLER 设置的回调被调用时, 由SDK传给用户
     * 
     * 数据类型: (void *)
     */
    QM_IOT_DEVOPT_USERDATA,

    /**
     * @brief 重置 MQTT 预订阅门闩, 允许再次按当前 pid/did 注册下行 topic
     *
     * @details
     * 清除 mqtt_pre_sub_done，并把内部 mqtt_handle 置空；不立刻订阅。
     * 典型用法：MQTT 会话销毁重建后，先 RESET_SUB，再重新 setopt MQTT_HANDLE。
     * data 可传 NULL。
     */
    QM_IOT_DEVOPT_RESET_SUB,

    QM_IOT_DEVOPT_MAX
} qm_iot_dev_option_t;

/**
 * @brief 创建dev会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL dev实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_dev_init(void);

/**
 * @brief 配置subdev会话
 *
 * @param[in] handle dev会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_subdev_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_subdev_option_t
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_dev_setopt(void *handle, qm_iot_dev_option_t option, void *data);

/**
 * @brief 结束subdev会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向subdev会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_dev_deinit(void **handle);

/**
 * @brief 向物联网平台上报设备解绑
 *
 * @param handle subdev会话句柄
 * @param subdev_state 子设备状态数组
 * @param dev_num 子设备数组中的子设备数量
 * 
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_dev_reset_request(void *handle);

/**
 * @brief 向物联网平台上报设备信息
 *
 * @param handle dev会话句柄
 * 
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_dev_send_devinfo(void *handle, qm_iot_devinfo_t *param);

/**
 * @brief 向物联网平台上报设备 MQTT 在线绑定准备完成状态。
 *
 * @param handle dev 会话句柄，需已配置 MQTT handle 与 IOT session userdata。
 *
 * @return int32_t
 * @retval QM_EOK 发布请求已发起。
 * @retval <QM_EOK 执行失败。
 */
int32_t qm_iot_dev_send_device_bind_online(void *handle);

/**
 * @brief 经设备上行 Topic 向云端上报 JSON（method / id，可选 params）。
 *
 * @param[in] handle dev 会话句柄（需已 @ref qm_iot_dev_setopt 配置 MQTT）。
 * @param[in] param  JSON 中 method 字段对应的字符串（与云端约定一致）。
 * @param[in] msg_id 报文 id；为 0 时由 SDK 内部按会话自增生成。
 * @param[in] payload 可选，嵌入 JSON 的 params 片段；可为 NULL。
 * @param[in] payload_len @p payload 长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK     发布请求已发起。
 * @retval -QM_EINVAL 参数非法。
 * @retval -QM_ENOMEM 内存分配失败。
 */
int32_t qm_iot_dev_up_report(void *handle, char *param, uint32_t msg_id, char *payload, int payload_len);

/**
 * @brief 经设备上行 Topic 向云端上报 JSON（method / subMethod / id，可选 params）。
 *
 * @param[in] handle dev 会话句柄（需已 @ref qm_iot_dev_setopt 配置 MQTT）。
 * @param[in] param JSON 中 method 字段对应的字符串（与云端约定一致）。
 * @param[in] submethod 可选 subMethod 字段；NULL 或空字符串时不写入。
 * @param[in] msg_id 报文 id；为 0 时由 SDK 内部按会话自增生成。
 * @param[in] payload 可选，嵌入 JSON 的 params 片段；可为 NULL。
 * @param[in] payload_len @p payload 长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK 发布请求已发起。
 * @retval -QM_EINVAL 参数非法。
 * @retval -QM_ENOMEM 内存分配失败。
 */
int32_t qm_iot_dev_up_report_ex(void *handle,
                                char *param,
                                char *submethod,
                                uint32_t msg_id,
                                char *payload,
                                int payload_len);

/**
 * @brief 经 device/up 向云端回复下行 JSON（method / id / code，可选 params）。
 *
 * @details
 * 转调 @ref qm_iot_dev_reply_publish ，不带顶层 subMethod。
 * @p param 传入下行原始 method（如 `"bindDevNotify"`），SDK 写入 JSON 前自动追加
 * `"Reply"` 后缀；若 @p param 已以 `"Reply"` 结尾则保持不变。
 *
 * @param[in] handle      dev 会话句柄（需已 @ref qm_iot_dev_setopt 配置 MQTT）。
 * @param[in] param       下行原始 method 字符串；非完整 Reply method 名。
 * @param[in] msg_id      报文 id，通常与下行请求 id 一致；为 0 时由 SDK 内部自增。
 * @param[in] code        业务结果码，写入 JSON 的 code 字段。
 * @param[in] payload     可选，嵌入 JSON 的 params 片段；可为 NULL。
 * @param[in] payload_len @p payload 长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK     发布请求已发起。
 * @retval -QM_EINVAL 参数非法或 method 过长。
 * @retval -QM_ENOMEM 内存分配失败。
 */
int32_t qm_iot_dev_up_report_reply(void *handle, char *param, uint32_t msg_id, int code, char *payload, int payload_len);

/**
 * @brief 为下行设备 Topic 上的 JSON method 注册处理函数。
 *
 * @param[in] handle      dev 会话句柄。
 * @param[in] param     与下行报文中 method 值匹配的字符串；超长部分按实现截断存储。
 * @param[in] recv_handler 收到匹配 method 时调用的回调，签名为 @ref qm_iot_mqtt_recv_handler_t；首参为 dev 句柄，次参为 MQTT 收包，再次参为 @p user_data。
 * @param[in] user_data 用户上下文，随回调原样传入 @p recv_handler。
 *
 * @return int32_t
 * @retval QM_EOK     注册成功或已更新同 method 的回调。
 * @retval -QM_EINVAL 参数非法。
 * @retval -QM_ENOMEM 动态分配失败。
 */
int32_t qm_iot_dev_down_register(void *handle, char *param, qm_iot_mqtt_recv_handler_t recv_handler, void *user_data);

/**
 * @brief 取消已注册的下行 method 处理函数。
 *
 * @param[in] handle dev 会话句柄。
 * @param[in] param  已注册的 method 字符串。
 *
 * @return int32_t
 * @retval QM_EOK     取消成功。
 * @retval -QM_EINVAL 参数非法。
 */
int32_t qm_iot_dev_down_unregister(void *handle, char *param);

#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT
/**
 * @brief 向 /{pid}/{did}/device/log 上报 logInfo（SDK 组装 params.payload 数组）。
 *
 * @param[in] handle    dev 会话句柄。
 * @param[in] log_level logLevel 字符串，见 @ref QM_IOT_COMMON_LOG_LEVEL_* 。
 * @param[in] items     日志项数组。
 * @param[in] item_num  数组元素个数，须大于 0。
 * @param[in] msg_id    报文 id；0 时自增。
 *
 * @return int32_t QM_EOK 成功；负值为参数/长度/内存错误。
 */
int32_t qm_iot_dev_log_push(void *handle, const char *log_level,
                            const qm_iot_log_item_t *items, uint8_t item_num,
                            uint32_t msg_id);

/**
 * @brief 向 /{pid}/{did}/device/log 上报 logInfo（外部已拼好 payload 数组 JSON）。
 *
 * @param[in] handle              dev 会话句柄。
 * @param[in] log_level           logLevel 字符串。
 * @param[in] payload_array_json  仅 JSON 数组片段，如 "[{...}]"。
 * @param[in] msg_id              报文 id；0 时自增。
 *
 * @return int32_t QM_EOK 成功；负值为参数/解析/长度/内存错误。
 */
int32_t qm_iot_dev_log_push_raw(void *handle, const char *log_level,
                                const char *payload_array_json, uint32_t msg_id);

#endif /* CONFIG_QM_IOT_LOG_PUSH_SUPPORT */

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_SUBDEV_API_H__ */
