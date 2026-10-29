/**
 * @file qm_iot_mqtt.h
 * @brief MQTT模块头文件, 提供用MQTT协议连接物联网平台的能力
 * @date 2024-8-27
 *
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 * @details
 *
 * MQTT模块用于建立与物联网平台的连接, API使用流程如下:
 *
 * 1. 调用 @ref qm_iot_mqtt_init 初始化MQTT会话, 获取会话句柄
 *
 * 2. 调用 @ref qm_iot_mqtt_setopt 配置MQTT会话的参数, 常用配置项见 @ref qm_iot_mqtt_setopt 的说明
 *
 * 3. 调用 @ref qm_iot_mqtt_connect 建立与阿里云物联网平台的连接
 *
 * 4. 启动一个线程, 线程中间歇性调用 @ref qm_iot_mqtt_process 处理心跳和QoS1的消息
 *
 * 5. 启动一个线程, 线程中持续调用 @ref qm_iot_mqtt_recv 接收网络上的MQTT报文
 *
 *    + 当接收到一条报文时, 按以下顺序检查当前MQTT会话的参数, 当满足某条的描述时, 会通过对应的回调函数进行通知, 并停止检查
 *
 *      + 检查收到的报文topic是否已经通过 @ref qm_iot_mqtt_setopt 的 @ref QM_IOT_MQTTOPT_APPEND_TOPIC_MAP 参数配置回调函数
 *
 *      + 检查收到的报文topic是否已经通过 @ref qm_iot_mqtt_sub API配置回调函数
 *
 *      + 检查是否通过 @ref qm_iot_mqtt_setopt 的 @ref QM_IOT_MQTTOPT_RECV_HANDLER 参数配置默认回调函数
 *
 * 6. 经过以上步骤后, MQTT连接已建立并能保持与物联网平台的连接, 接下来按自己的场景用 @ref qm_iot_mqtt_sub 和 @ref qm_iot_mqtt_pub 等API实现业务逻辑即可
 *
 */

#ifndef _QM_IOT_MQTT_H_
#define _QM_IOT_MQTT_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_types.h"

typedef enum {
    /** Network unavailable; pause I/O and reconnect, retain the session. */
    QM_IOT_NETWORK_DOWN = 0,
    /** Network available; resume I/O and reconnect. */
    QM_IOT_NETWORK_UP,
} qm_iot_network_state_t;

typedef enum{
    QM_IOT_MQTT_RES_OK,
    QM_IOT_MQTT_RES_SUB_ERR,
    QM_IOT_MQTT_RES_SUB_TIMEOUT,
    QM_IOT_MQTT_RES_UNSUB_TIMEOUT,
    QM_IOT_MQTT_RES_PUB_TIMEOUT,
}qm_iot_mqtt_res_t;

/**
 * @brief MQTT报文类型
 *
 * @details
 *
 * 传入@ref qm_iot_mqtt_recv_handler_t 的MQTT报文类型
 */
typedef enum {
    /**
     * @brief MQTT PUBLISH报文
     */
    QM_IOT_MQTTRECV_PUB,

    /**
     * @brief MQTT SUBACK报文
     */
    QM_IOT_MQTTRECV_SUB_ACK,

    /**
     * @brief MQTT UNSUB报文
     */
    QM_IOT_MQTTRECV_UNSUB_ACK,

    /**
     * @brief MQTT PUBACK报文
     */
    QM_IOT_MQTTRECV_PUB_ACK,

} qm_iot_mqtt_recv_type_t;

typedef struct {
    /**
     * @brief MQTT报文类型, 更多信息请参考@ref qm_iot_mqtt_recv_type_t
     */
    qm_iot_mqtt_recv_type_t type;
    /**
     * @brief MQTT报文联合体, 内容根据type进行选择
     */
    union {
        /**
         * @brief MQTT PUBLISH报文
         */
        struct {
            uint8_t qos;
            char *topic;
            uint16_t topic_len;
            uint8_t *payload;
            uint32_t payload_len;
        } pub;
        /**
         * @brief QM_IOT_MQTTRECV_SUB_ACK
         */
        struct {
            qm_iot_mqtt_res_t res;
            uint16_t packet_id;
        } sub_ack;
        /**
         * @brief QM_IOT_MQTTRECV_UNSUB_ACK
         */
        struct {
            qm_iot_mqtt_res_t res;
            uint16_t packet_id;
        } unsub_ack;
        /**
         * @brief QM_IOT_MQTTRECV_PUB_ACK
         */
        struct {
            qm_iot_mqtt_res_t res;
            uint16_t packet_id;
        } pub_ack;
    } data;
} qm_iot_mqtt_recv_t;

/**
 * @brief MQTT报文接收回调函数原型
 *
 * 
 * @param[in] handle MQTT实例句柄
 * @param[in] packet MQTT报文结构体, 存放收到的MQTT报文
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (*qm_iot_mqtt_recv_handler_t)(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata);

/**
 * @brief MQTT内部事件类型
 */
typedef enum {
    /**
     * @brief 当MQTT实例第一次连接网络成功时, 触发此事件
     */
    QM_IOT_MQTTEVT_CONNECT,
    /**
     * @brief 当MQTT实例断开网络连接后重连成功时, 触发此事件
     */
    QM_IOT_MQTTEVT_RECONNECT,
    /**
     * @brief 当MQTT实例断开网络连接时, 触发此事件
     */
    QM_IOT_MQTTEVT_DISCONNECT
} qm_iot_mqtt_event_type_t;

/**
 * @brief MQTT内部事件
 */
typedef struct {
    /**
     * @brief MQTT内部事件类型. 更多信息请参考@ref qm_iot_mqtt_event_type_t
     *
     */
    qm_iot_mqtt_event_type_t type;
    /**
     * @brief MQTT事件数据联合体
     */
    union {

    } data;
} qm_iot_mqtt_event_t;

/**
 * @brief MQTT事件回调函数
 *
 * @details
 *
 * 当MQTT内部事件被触发时, 调用此函数. 如连接成功/断开连接/重连成功
 *
 */
typedef void (*qm_iot_mqtt_event_handler_t)(void *handle, const qm_iot_mqtt_event_t *event, void *userdata);


typedef struct {
    char   *server_crt;            /* 必须位于静态存储区, SDK内部不做拷贝 */
    char   *client_crt;            /* 必须位于静态存储区, SDK内部不做拷贝 */
    char   *client_privkey;         /* 必须位于静态存储区, SDK内部不做拷贝 */
} qm_iot_mqtt_network_cred_t;

/**
 * @ingroup mqtt_enum_types
 * @brief MQTT Quality of Service values.
 */
typedef enum
{
    QM_MQTT_QoS0 = 0, /**< Delivery at most once. */
    QM_MQTT_QoS1 = 1, /**< Delivery at least once. */
    QM_MQTT_QoS2 = 2  /**< Delivery exactly once. */
} qm_iot_mqtt_qos_t;

/**
 * @ingroup mqtt_struct_types
 * @brief MQTT SUBSCRIBE packet parameters.
 */
typedef struct
{
    /**
     * @brief Quality of Service for subscription.
     */
    qm_iot_mqtt_qos_t qos;

    /**
     * @brief Topic filter to subscribe to.
     */
    char * topic;

    /**
     * @brief recv handler of subscription topic.
     */
    qm_iot_mqtt_recv_handler_t handler;

    /**
     * @brief user data
     */
    void *userdata;
} qm_iot_mqtt_sub_info_t;

/**
 * @ingroup mqtt_struct_types
 * @brief MQTT PUBLISH packet parameters.
 */
typedef struct
{
    /**
     * @brief Quality of Service for message.
     */
    int qos;

    /**
     * @brief Whether this is a retained message.
     */
    bool_t retain;

    /**
     * @brief Whether this is a duplicate publish message.
     */
    bool_t dup;

    /**
     * @brief Topic name on which the message is published.
     */
    char * topic;

    /**
     * @brief Length of topic name.
     */
    uint16_t topic_len;

    /**
     * @brief Message payload.
     */
    void * payload;

    /**
     * @brief Message payload length.
     */
    uint32_t payload_len;
} qm_mqtt_pub_info_t;

/**
 * @brief @ref qm_iot_mqtt_setopt 函数的option参数. 对于下文每一个选项中的数据类型, 指的是@ref qm_iot_mqtt_setopt 中的data参数的数据类型
 *
 * @details
 *
 * 1. data的数据类型是char *时, 以配置@ref QM_IOT_MQTTOPT_HOST 为例:
 *
 *    char *host = "xxx";
 *
 *    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_HOST, host);
 *
 * 2. data的数据类型是其他数据类型时, 以配置@ref QM_IOT_MQTTOPT_PORT 为例:
 *
 *    uint16_t port = 443;
 *
 *    qm_iot_mqtt_setopt(mqtt_handle, QM_IOT_MQTTOPT_PORT, (void *)&port);
 */
typedef enum {
    /**
     * @brief MQTT 服务器的域名地址或者ip地址
     *
     * @details
     *
     * 联网平台域名地址:
     *
     * 数据类型: (char *)
     */
    QM_IOT_MQTTOPT_HOST,

    /**
     * @brief MQTT 服务器的端口号
     *
     * @details
     *
     * 数据类型: (uint16_t *)
     */
    QM_IOT_MQTTOPT_PORT,

    /**
     * @brief 使用自定义连接凭据连接mqtt服务器时, 凭据的username
     *
     * @details
     *
     * 默认为空
     * 
     * 数据类型: (char *)
     */
    QM_IOT_MQTTOPT_USERNAME,

    /**
     * @brief 使用自定义连接凭据连接mqtt服务器时, 凭据的password
     *
     * @brief
     * 
     * 默认为空
     *
     * 数据类型: (char *)
     */
    QM_IOT_MQTTOPT_PASSWORD,

    /**
     * @brief 使用自定义连接凭据连接mqtt服务器时, 凭据的clientid
     *
     * @details
     *
     * 默认为设备did信息
     * 
     * 数据类型: (char *)
     */
    QM_IOT_MQTTOPT_CLIENTID,


    /**
     * @brief MQTT建联时, CONNECT报文中的心跳间隔参数
     *
     * @details
     *
     * 受物联网平台限制, 取值范围为30 ~ 1200s
     *
     * 1. 如果设置的值小于30, mqtt建联会被云端拒绝,  @ref qm_iot_mqtt_connect 函数会返回@ref STATE_MQTT_CONNACK_RCODE_SERVER_UNAVAILABLE 错误
     *
     * 2. 如果设置的值大于1200, mqtt连接仍然可以建立, 但此参数会被服务器覆盖为1200
     *
     * 数据类型: (uint16_t *) 取值范围: 30 ~ 1200s 默认值: 1200s
     */
    QM_IOT_MQTTOPT_KEEPALIVE_SEC,

    /**
     * @brief MQTT建联时, CONNECT报文中的clean session参数
     *
     * @details
     *
     * 1. 设备上线时如果clean session为0, 那么上线前服务器推送QoS1的消息会在此时推送给设备
     *
     * 2. 设备上线时如果clean session为1, 那么上线前服务器推送的QoS1的消息会被丢弃
     *
     * 数据类型: (uint8_t *) 取值范围: 0, 1 默认值: 1
     */
    QM_IOT_MQTTOPT_CLEAN_SESSION,

    /**
     * @brief MQTT建联时, 网络使用的安全凭据
     *
     * @details
     *
     * 该配置项用于为底层网络配置@ref qm_iot_network_cred_t 安全凭据数据
     *
     * 1. 若该选项不配置, 那么MQTT将以tcp方式直接建联
     *
     * 2. 若@ref qm_iot_network_cred_t 中option配置为@ref QM_IOT_NETWORK_CRED_NONE , MQTT将以tcp方式直接建联
     *
     * 3. 若@ref qm_iot_network_cred_t 中option配置为@ref QM_IOT_NETWORK_CRED_SVRCERT_CA , MQTT将以tls方式建联
     *
     * 数据类型: (qm_iot_network_cred_t *)
     */
    QM_IOT_MQTTOPT_NETWORK_CRED,

    /**
     * @brief QoS1消息重发间隔
     *
     * @details
     *
     * 当发送qos1 MQTT PUBLISH报文后, 如果在@ref QM_IOT_MQTTOPT_REPUB_TIMEOUT_MS 时间内未收到mqtt PUBACK报文,
     * @ref qm_iot_mqtt_process 会重新发送此qo1 MQTT PUBLISH报文, 直到收到PUBACK报文为止
     *
     * 数据类型: (uint32_t *) 默认值: (3 * 1000) ms
     */
    QM_IOT_MQTTOPT_REPUB_TIMEOUT_MS,

    /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details
     *
     * 这个上下文指针会在 QM_IOT_MQTTOPT_RECV_HANDLER 和 QM_IOT_MQTTOPT_EVENT_HANDLER 设置的回调被调用时, 由SDK传给用户
     *
     * 数据类型: (void *)
     */
    QM_IOT_MQTTOPT_USERDATA,

    /**
     * @brief 从MQTT服务器收取的数据从此默认回调函数进行通知
     *
     * @details
     *
     * 1. 若没有配置该回调函数, 当有消息到达但找不到对应的已注册topic时, 消息会被丢弃
     *
     * 2. 若已配置该回调函数, 当有消息到达但找不到对应的已注册topic时, 消息从此默认回调函数进行通知
     *
     * 
     * 数据类型: ( @ref qm_iot_mqtt_recv_handler_t )
     */
    QM_IOT_MQTTOPT_RECV_HANDLER,

    /**
     * @brief MQTT客户端内部发生的事件会从此回调函数进行通知, 如上线/断线/重新上线
     *
     * @details
     *
     * 数据类型: ( @ref qm_iot_mqtt_event_handler_t )
     */
    QM_IOT_MQTTOPT_EVENT_HANDLER,

    QM_IOT_MQTTOPT_MAX
} qm_iot_mqtt_option_t;


/**
 * @brief 初始化mqtt实例并设置默认参数
 *
 * @return void*
 * @retval 非NULL MQTT实例句柄
 * @retval NULL 初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_mqtt_init(void);

/**
 * @brief 设置mqtt参数
 *
 * @details
 *
 * 下面列出常用的配置选项, 至少需要配置以下选项才可使用MQTT的基本功能
 *
 * 其余配置选项均设有默认值, 可按业务需要进行调整
 *
 * + `QM_IOT_MQTTOPT_HOST`: 配置连接的MQTT站点地址
 *
 * + `QM_IOT_MQTTOPT_PORT`: 配置连接的MQTT站点端口号
 *
 * + `QM_IOT_MQTTOPT_PRODUCT_ID`: 配置设备的 productId
 *
 * + `QM_IOT_MQTTOPT_DEVICE_ID`: 配置设备的 deviceId
 *
 * + `QM_IOT_MQTTOPT_NETWORK_CRED`: 配置建立MQTT连接时的安全凭据
 *
 * + `QM_IOT_MQTTOPT_RECV_HANDLER`: 配置默认的数据接收回调函数
 *
 * + `QM_IOT_MQTTOPT_EVENT_HANDLER`: 配置MQTT事件通知回调函数
 *
 * @param[in] handle mqtt句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_mqtt_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_mqtt_option_t
 *
 * @return int32_t
 * @retval <QM_EOK 参数设置失败
 * @retval >=QM_EOK 参数设置成功
 *
 */
int32_t qm_iot_mqtt_setopt(void *handle, qm_iot_mqtt_option_t option, void *data);

/**
 * @brief 释放mqtt实例句柄的资源
 *
 * @param[in] handle 指向mqtt实例句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_mqtt_deinit(void **handle);
/**
 * @brief Pause/resume network I/O without destroying the MQTT session.
 *
 * @param[in] handle MQTT句柄
 * @param[in] state 网络状态，只能为 @ref QM_IOT_NETWORK_UP 或
 *                  @ref QM_IOT_NETWORK_DOWN
 */
int32_t qm_iot_mqtt_network_update(void *handle, qm_iot_network_state_t state);

/**
 * @brief 此函数用于处理定时心跳发送和qos1消息的重传逻辑
 *
 * @details
 *
 * 1. 发送心跳至mqtt broker以维护mqtt连接, 心跳发送间隔由@ref QM_IOT_MQTTOPT_KEEPALIVE_SEC 配置项控制
 *
 * 2. 如果一条qos1的mqtt PUBLISH报文在@ref QM_IOT_MQTTOPT_REPUB_TIMEOUT_MS 时间内没有收到mqtt PUBACK应答报文, 该函数会重发此消息, 直到成功为止
 *
 * @param[in] handle MQTT实例句柄
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 * @note
 *
 * 该函数为非阻塞, 需要间歇性被调用, 调用间隔应当小于@ref QM_IOT_MQTTOPT_KEEPALIVE_SEC 和@ref QM_IOT_MQTTOPT_REPUB_TIMEOUT_MS 时间内没有收到mqtt的最小值,
 *
 * 以确保心跳发送和QoS1消息的重传逻辑正常工作
 */
int32_t qm_iot_mqtt_process(void *handle);

/**
 * @brief 发送一条PUBLISH报文到MQTT服务器, QoS为0, 用于发布指定的消息
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT PUBLISH报文的topic
 * @param[in] payload 指定MQTT PUBLISH报文的payload
 * @param[in] payload_len 指定MQTT PUBLISH报文的payload_len
 * @param[in] qos 指定mqtt的qos值, 仅支持qos0和qos1
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pub(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos);

/**
 * @brief 发送一条PUBLISH报文到MQTT服务器, QoS为0, 用于发布指定的消息
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT PUBLISH报文的topic
 * @param[in] payload 指定MQTT PUBLISH报文的payload
 * @param[in] payload_len 指定MQTT PUBLISH报文的payload_len
 * @param[in] qos 指定mqtt的qos值, 仅支持qos0和qos1
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pub_wait_ack(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos);

/**
 * @brief 发送一条PUBLISH报文到MQTT服务器, QoS为0, 用于发布指定的消息
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT PUBLISH报文的topic
 * @param[in] payload 指定MQTT PUBLISH报文的payload
 * @param[in] payload_len 指定MQTT PUBLISH报文的payload_len
 * @param[in] qos 指定mqtt的qos值, 仅支持qos0和qos1
 * @param[in] handler 当有消息发布成功或者超时，通过此回调通知
 * 
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pub_with_callback(void *handle, char *topic, uint8_t *payload, uint32_t payload_len, qm_iot_mqtt_qos_t qos, qm_iot_mqtt_recv_handler_t handler);

/**
 * @brief 发送一条mqtt SUBSCRIBE报文到MQTT服务器, 用于订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT SUBSCRIBE报文的top
 * ic
 * @param[in] handler 与topic对应的MQTT PUBLISH报文回调函数, 当有消息发布到topic时, 该回调函数被调用
                    若handler为NULL传入, 则SDK调用@ref QM_IOT_MQTTOPT_RECV_HANDLER 配置的回调函数
 * @param[in] qos 指定topic期望mqtt服务器支持的最大qos值, 仅支持qos0和qos1
 * @param[in] userdata 可让SDK代为保存的用户上下文, 当回调函数被调用时, 此上下文会通过handler传回给用户
 *                  若未指定该上下文, 那么通过@ref AIOT_MQTTOPT_USERDATA 配置的上下文会通过handler传回给用户
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos);

/**
 * @brief 用于预订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT SUBSCRIBE报文的top
 * ic
 * @param[in] handler 与topic对应的MQTT PUBLISH报文回调函数, 当有消息发布到topic时, 该回调函数被调用
                    若handler为NULL传入, 则SDK调用@ref QM_IOT_MQTTOPT_RECV_HANDLER 配置的回调函数
 * @param[in] qos 指定topic期望mqtt服务器支持的最大qos值, 仅支持qos0和qos1
 * @param[in] userdata 可让SDK代为保存的用户上下文, 当回调函数被调用时, 此上下文会通过handler传回给用户
 *                  若未指定该上下文, 那么通过@ref AIOT_MQTTOPT_USERDATA 配置的上下文会通过handler传回给用户
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pre_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos, void *userdata);

/**
 * @brief 同步向平台订阅
 *
 * @param[in] handle MQTT实例句柄
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pre_sub_start(void *handle);

/**
 * @brief 用于预取消订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT SUBSCRIBE报文的top
 * ic
 * @param[in] handler 与topic对应的MQTT PUBLISH报文回调函数, 当有消息发布到topic时, 该回调函数被调用
                    若handler为NULL传入, 则SDK调用@ref QM_IOT_MQTTOPT_RECV_HANDLER 配置的回调函数
 * @param[in] qos 指定topic期望mqtt服务器支持的最大qos值, 仅支持qos0和qos1
 * @param[in] userdata 可让SDK代为保存的用户上下文, 当回调函数被调用时, 此上下文会通过handler传回给用户
 *                  若未指定该上下文, 那么通过@ref AIOT_MQTTOPT_USERDATA 配置的上下文会通过handler传回给用户
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_pre_unsub(void *handle, char *topic);

/**
 * @brief 本地自动订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT SUBSCRIBE报文的top
 * ic
 * @param[in] handler 与topic对应的MQTT PUBLISH报文回调函数, 当有消息发布到topic时, 该回调函数被调用
                    若handler为NULL传入, 则SDK调用@ref QM_IOT_MQTTOPT_RECV_HANDLER 配置的回调函数
 * @param[in] userdata 可让SDK代为保存的用户上下文, 当回调函数被调用时, 此上下文会通过handler传回给用户
 *                  若未指定该上下文, 那么通过@ref AIOT_MQTTOPT_USERDATA 配置的上下文会通过handler传回给用户
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功,消息ID
 */
int32_t qm_iot_mqtt_auto_sub(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler);

/**
 * @brief 用于本地自动取消订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT UNSUBSCRIBE报文的topic
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_auto_unsub(void *handle, char *topic);

/**
 * @brief 发送一条mqtt SUBSCRIBE报文到MQTT服务器, 用于订阅指定的topic,等到ACK成功
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT SUBSCRIBE报文的top
 * ic
 * @param[in] handler 与topic对应的MQTT PUBLISH报文回调函数, 当有消息发布到topic时, 该回调函数被调用
                    若handler为NULL传入, 则SDK调用@ref QM_IOT_MQTTOPT_RECV_HANDLER 配置的回调函数
 * @param[in] qos 指定topic期望mqtt服务器支持的最大qos值, 仅支持qos0和qos1
 * @param[in] userdata 可让SDK代为保存的用户上下文, 当回调函数被调用时, 此上下文会通过handler传回给用户
 *                  若未指定该上下文, 那么通过@ref AIOT_MQTTOPT_USERDATA 配置的上下文会通过handler传回给用户
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_sub_wait_ack(void *handle, char *topic, qm_iot_mqtt_recv_handler_t handler, qm_iot_mqtt_qos_t qos);

/**
 * @brief 发送一条mqtt UNSUBSCRIBE报文到MQTT服务器, 用于取消订阅指定的topic
 *
 * @param[in] handle MQTT实例句柄
 * @param[in] topic 指定MQTT UNSUBSCRIBE报文的topic
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_mqtt_unsub(void *handle, char *topic);


#if defined(__cplusplus)
}
#endif

#endif
