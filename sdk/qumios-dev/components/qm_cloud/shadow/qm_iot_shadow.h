/**
 * @file qm_iot_shadow.h
 * @brief shadow模块头文件, 提供更新, 获取设备影子的能力
 * @date 2024-8-27
 * 
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 * @details
 *
 * 请按照以下流程使用API
 *
 * 1. 在使用设备影子模块前, 用户应首先创建好一个MQTT实例
 *
 * 2. 调用`qm_iot_shadow_init`创建一个设备影子实例, 保存实例句柄
 *
 * 3. 调用`qm_iot_shadow_setopt`配置`QM_IOT_SHADOWOPT_DEVICE_ID`选项以设置DID, 此选项为强制配置选项
 * 
 * 4. 调用`qm_iot_shadow_setopt`配置`QM_IOT_SHADOWOPT_MQTT_HANDLE`选项以设置MQTT句柄, 此选项为强制配置选项
 *
 * 5. 调用`qm_iot_shadow_setopt`配置`QM_IOT_SHADOWOPT_RECV_HANDLER`和`QM_IOT_SHADOWOPT_USERDATA`选项以注册数据接受回调函数和用户上下文数据指针
 *
 * 6. 在使用`qm_iot_shadow_send`发送消息前, 应先完成MQTT实例的建连
 *
 * 7. 调动`qm_iot_shadow_send`发送更新设备影子, 获取设备影子等消息到云平台, 在注册的回调函数中处理各种类型的云端应答消息或主动下推消息
 *
 */

#ifndef __QM_IOT_SHADOW_H__
#define __QM_IOT_SHADOW_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_types.h"

/**
 * @brief @ref qm_iot_shadow_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_shadow_setopt 中, data参数的数据类型
 *
 */
typedef enum {
    /**
     * @brief 模块依赖的MQTT句柄
     *
     * @details
     *
     * shadow模块依赖底层的MQTT模块, 用户必需配置正确的MQTT句柄, 否则无法正常工作, 数据类型为(void *)
     */
    QM_IOT_SHADOWOPT_MQTT_HANDLE,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户, 数据类型为(qm_iot_shadow_recv_handler_t)
     */
    QM_IOT_SHADOWOPT_RECV_HANDLER,

    /**
     * @brief 设备的product id, 可从物联网平台控制台</a>获取
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_SHADOWOPT_PRODUCT_ID,

    /**
     * @brief 用户需要SDK暂存的上下文, 数据类型为(void *)
     *
     * @details 这个上下文指针会在 QM_IOT_SHADOWOPT_RECV_HANDLER 设置的回调被调用时, 由SDK传给用户
     */
    QM_IOT_SHADOWOPT_USERDATA,

    /**
     * @brief 重置 MQTT 预订阅门闩, 允许再次按当前 pid/did 注册下行 topic
     *
     * @details
     * 清除 mqtt_pre_sub_done，并把内部 mqtt_handle 置空；不立刻订阅。
     * 典型用法：MQTT 会话销毁重建后，先 RESET_SUB，再重新 setopt MQTT_HANDLE / USERDATA / RECV_HANDLER。
     * data 可传 NULL。
     */
    QM_IOT_SHADOWOPT_RESET_SUB,

    /**
     * @brief 配置选项数量最大值, 不可用作配置参数
     */
    QM_IOT_SHADOWOPT_MAX,
} qm_iot_shadow_option_t;

/**
 * @brief shadow模块发送消息类型
 *
 * @details
 *
 * 这个枚举类型包括了shadow模块支持发送的所有数据类型, 不同的消息类型将对于不同的消息结构体
 *
 */
typedef enum {
    /**
     * @brief 更新设备影子中的reported值, 消息结构体参考@ref qm_iot_shadow_msg_update_t
     *
     */
    QM_IOT_SHADOWMSG_UPDATE,


    /**
     * @brief 获取设备影子, 消息结构体参考@ref qm_iot_shadow_msg_get_t
     *
     */
    QM_IOT_SHADOWMSG_GET,

    /**
     * @brief 消息数量最大值, 不可用作消息类型
     */
    QM_IOT_SHADOWMSG_MAX,
} qm_iot_shadow_msg_type_t;


/**
 * @brief 用于<b>更新设备影子中的reported数据</b>的消息结构体
 */
typedef struct {
    /**
     * @brief 设备影子reported object字符串, <b>必须为以结束符'\0'结尾的字符串</b>, 如"{\"LightSwitch\": 1}"
     */
    char *reported;

} qm_iot_shadow_msg_update_t;


/**
 * @brief 用于<b>获取设备影子</b>的消息结构体, 消息内容为空
 */
typedef struct {
    /**
     * @brief 保留字段
     */
    uint32_t resevered;
} qm_iot_shadow_msg_get_t;

/**
 * @brief data-model模块发送消息的消息结构体
 */
typedef struct {
    /**
     * @brief 消息所属设备的did, 若为NULL则使用通过qm_iot_shadow_setopt配置的did \n
     * 在网关子设备场景下, 可通过指定为子设备的did来发送子设备的消息到云端
     */
    uint32_t did;
    /**
     * @brief 消息类型, 可参考@ref qm_iot_shadow_msg_type_t
     */
    qm_iot_shadow_msg_type_t type;
    /**
     * @brief 消息数据联合体, 不同的消息类型将使用不同的消息结构体
     */
    union {
        qm_iot_shadow_msg_update_t            update;
        qm_iot_shadow_msg_get_t               get;
    } data;
} qm_iot_shadow_msg_t;


/**
 * @brief shadow模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    /**
     * @brief 设备发送 @ref QM_IOT_SHADOWMSG_UPDATE, 类型消息后, 云端返回的应答消息, \n
     * 消息数据结构体参考 @ref qm_iot_shadow_recv_report_reply_t
     */
    QM_IOT_SHADOWRECV_REPORT_REPLY,

    /**
     * @brief 主动获取设备影子内容云端返回的影子内容, 设备发送 @ref QM_IOT_SHADOWMSG_GET, 类型消息后, 云端返回的应答消息, \n
     * 消息数据结构体参考 @ref qm_iot_shadow_recv_get_reply_t
     */
    QM_IOT_SHADOWRECV_GET_REPLY,

    /**
     * @brief 设备在线时, 云端自动下发的影子内容, 消息数据结构体参考 @ref qm_iot_shadow_recv_set_t
     */
    QM_IOT_SHADOWRECV_SET,
} qm_iot_shadow_recv_type_t;

/**
 * @brief 设备发送 @ref QM_IOT_SHADOWMSG_UPDATE, 类型消息后, 云端返回的应答消息
 */
typedef struct {
    /**
     * @brief 指向应答数据的指针
     */
    char *payload;

    /**
     * @brief 应答数据长度
     */
    uint32_t payload_len;

    /**
     * @brief 设备影子版本
     */
    int64_t version;

    /**
     * @brief 应答报文对应的时间戳
     */
    uint64_t timestamp;
} qm_iot_shadow_recv_report_reply_t;

/**
 * @brief 如果设备在线, 用户应用调用云端API后云端下推的消息
 */
typedef struct {
    /**
     * @brief 指向设备影子数据的指针
     */
    char *payload;

    /**
     * @brief 设备影子数据长度
     */
    uint32_t payload_len;

    /**
     * @brief 设备影子版本
     */
    uint64_t version;

    /**
     * @brief 报文对应的时间戳
     */
    uint64_t timestamp;
} qm_iot_shadow_recv_set_t;

/**
 * @brief 设备发送 @ref QM_IOT_SHADOWMSG_GET 类型消息后, 云端返回的设备影子数据
 */
typedef struct {
    /**
     * @brief 指向设备影子数据的指针
     */
    char *payload;

    /**
     * @brief 设备影子数据长度
     */
    uint32_t payload_len;

    /**
     * @brief 设备影子版本号
     */
    uint64_t version;

    /**
     * @brief 报文对应的时间戳
     */
    uint64_t timestamp;
} qm_iot_shadow_recv_get_reply_t;

/**
 * @brief shadow模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 消息所属设备的did
     */
    uint32_t did;
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_shadow_recv_type_t
     */
    qm_iot_shadow_recv_type_t  type;
    /**
     * @brief 消息数据联合体, 不同的消息类型将使用不同的消息结构体
     */
    union {
        qm_iot_shadow_recv_report_reply_t report_reply;
        qm_iot_shadow_recv_set_t set;
        qm_iot_shadow_recv_get_reply_t get_reply;
    } data;
} qm_iot_shadow_recv_t;

/**
 * @brief shadow模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle shadow会话句柄
 * @param[in] recv shadow接受消息结构体, 存放收到的shadow报文内容
 * @param[in] userdata 指向用户上下文数据的指针, 这个指针由用户通过调用@ref qm_iot_shadow_setopt 配置@ref QM_IOT_SHADOWOPT_USERDATA 选项设置
 *
 * @return void
 */
typedef void (*qm_iot_shadow_recv_handler_t)(void *handle, qm_iot_shadow_recv_t *recv, void *userdata);

/**
 * @brief 创建shadow会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL shadow实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_shadow_init(void *iot_handle);

/**
 * @brief 配置shadow会话
 *
 * @param[in] handle shadow会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_shadow_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_shadow_option_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_setopt(void *handle, qm_iot_shadow_option_t option, void *data);

/**
 * @brief 
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_sub(void *handle, uint32_t did);

/**
 * @brief 取消所有子设备的订阅
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_unsub_all(void *handle);

/**
 * @brief 
 *
 * @param[in] handle shadow会话句柄
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_unsub(void *handle, uint32_t did);

/**
 * @brief 向服务器发送shadow消息请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_shadow_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_send(void *handle, qm_iot_shadow_msg_t *msg);

/**
 * @brief 结束shadow会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向shadow会话句柄的指针
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_shadow_deinit(void **handle);



#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_SHADOW_H__ */