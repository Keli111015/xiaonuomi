#ifndef __QM_IOT_PUB_H__
#define __QM_IOT_PUB_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_types.h"
#include "qm_spec_api.h"

/**
 * @brief @ref qm_iot_pub_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_pub_setopt 中, data参数的数据类型
 *
 */
typedef enum {
    /**
     * @brief dev会话 需要的mqtt句柄, 需要先建立MQTT连接，再设置MQTT句柄
     * 
     * @details
     * 
     * 数据类型: (void *)
     */
    QM_IOT_PUBOPT_MQTT_HANDLE,

    /**
     * @brief pub会话更新时间
     * 
     * @details
     * 
     * 数据类型: (void *)
     */
    QM_IOT_PUBOPT_DELAY_TIMEOUT,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户, 数据类型为(qm_iot_shadow_recv_handler_t)
     */
    QM_IOT_PUBOPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文, 数据类型为(void *)
     *
     * @details 这个上下文指针会在 QM_IOT_SHADOWOPT_RECV_HANDLER 设置的回调被调用时, 由SDK传给用户
     */
    QM_IOT_PUBOPT_USERDATA,

    /**
     * @brief 重置 MQTT 预订阅门闩, 允许再次按当前 pid/did 注册下行 topic
     *
     * @details
     * 清除 mqtt_pre_sub_done，并把内部 mqtt_handle 置空；不立刻订阅。
     * 典型用法：MQTT 会话销毁重建后，先 RESET_SUB，再重新 setopt MQTT_HANDLE / USERDATA / RECV_HANDLER。
     * data 可传 NULL。
     */
    QM_IOT_PUBOPT_RESET_SUB,

    /**
     * @brief 配置选项数量最大值, 不可用作配置参数
     */
    QM_IOT_PUBOPT_MAX,
} qm_iot_pub_option_t;


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
    QM_IOT_PUB_TYPE_SHADOW,

    /**
     * @brief 消息数量最大值, 不可用作消息类型
     */
    QM_IOT_PUB_TYPE_MAX,
} qm_iot_pub_type_t;

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
    QM_IOT_PUBMSG_SHADOW_UPDATE,


    /**
     * @brief 更新设备qm 标准协议的reported值
     *
     */
    QM_IOT_PUBMSG_UPDATE,

    /**
     * @brief 更新设备qm 标准协议的get值
     *
     */
    QM_IOT_PUBMSG_GET_REQ,

    /**
     * @brief 更新设备qm 标准协议的get值
     *
     */
    QM_IOT_PUBMSG_GET_RSP,

    /**
     * @brief 消息数量最大值, 不可用作消息类型
     */
    QM_IOT_PUBMSG_MAX,
} qm_iot_pub_msg_type_t;


/**
 * @brief 用于<b>更新设备影子中的reported数据</b>的消息结构体
 */
typedef struct {
    qm_spec_property_operation_t *property_operation;
} qm_iot_pub_msg_common_update_t;

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
    qm_iot_pub_msg_type_t type;
    /**
     * @brief 消息数据联合体, 不同的消息类型将使用不同的消息结构体
     */
    union {
        qm_iot_pub_msg_common_update_t   update;
    } data;
} qm_iot_pub_msg_t;

/**
 * @brief shadow模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    /**
     * @brief 设备在线时, 云端自动下发的影子内容, 消息数据结构体参考 @ref qm_iot_shadow_recv_set_t
     */
    QM_IOT_PUBRECV_SHADOW_SET,

    QM_IOT_PUBRECV_PROPERTY_SET,

    QM_IOT_PUBRECV_PROPERTY_GET_REQ,

    QM_IOT_PUBRECV_PROPERTY_GET_RSP,
} qm_iot_pub_recv_type_t;

/**
 * @brief 如果设备在线, 用户应用调用云端API后云端下推的消息
 */
typedef struct {
    qm_spec_property_operation_t *operation;
}qm_iot_pub_recv_set_t;

/**
 * @brief 如果设备在线, 用户应用调用云端API后云端下推的消息
 */
typedef struct {
    qm_spec_property_operation_t *operation;
}qm_iot_pub_recv_get_t;


/**
 * @brief pub模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 消息所属设备的did
     */
    uint32_t did;
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_pub_recv_type_t
     */
    qm_iot_pub_recv_type_t  type;
    /**
     * @brief 消息数据联合体, 不同的消息类型将使用不同的消息结构体
     */
    union {
        qm_iot_pub_recv_set_t prop_set;
        qm_iot_pub_recv_get_t prop_get;
    } data;
} qm_iot_pub_recv_t;

/**
 * @brief pub模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle shadow会话句柄
 * @param[in] recv shadow接受消息结构体, 存放收到的shadow报文内容
 * @param[in] userdata 指向用户上下文数据的指针, 这个指针由用户通过调用@ref qm_iot_shadow_setopt 配置@ref QM_IOT_SHADOWOPT_USERDATA 选项设置
 *
 * @return void
 */
typedef void (*qm_iot_pub_recv_handler_t)(void *handle, qm_iot_pub_recv_t *recv, void *userdata);

/**
 * @brief 创建pub会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL shadow实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_pub_init(void);

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
int32_t qm_iot_pub_setopt(void *handle, qm_iot_pub_option_t option, void *data);

/**
 * @brief 向服务器发送shadow消息请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 * @param[in] force  置1为强制上报
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_send(void *handle, qm_iot_pub_msg_t *msg, int force);

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_sub(void *handle, qm_iot_pub_type_t pub_type, uint32_t did);

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_all_unsub(void *handle, qm_iot_pub_type_t pub_type);

/**
 * @brief 向服务器发送shadow订阅请求
 *
 * @param[in] handle shadow会话句柄
 * @param[in] msg    消息结构体, 可指定发送设备did, 消息类型, 消息数据等, 更多信息请参考@ref qm_iot_pub_msg_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_unsub(void *handle, qm_iot_pub_type_t pub_type, uint32_t did);

/**
 * @brief 结束shadow会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向shadow会话句柄的指针
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_pub_deinit(void **handle);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_SHADOW_H__ */