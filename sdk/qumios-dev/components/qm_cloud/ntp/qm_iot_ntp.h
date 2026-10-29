/**
 * @file qm_iot_ntp.h
 * @brief ntp模块头文件, 提供获取utc时间的能力
 *
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 * @details
 *
 * NTP模块用于从物联网平台上获取UTC时间, API的使用流程如下:
 *
 * 1. 首先参考 @ref qm_iot_api.h 的说明, 保证成功建立与物联网平台的云连接
 *
 * 2. 调用 @ref qm_iot_ntp_init 初始化ntp会话, 获取会话句柄
 *
 * 3. 调用 @ref qm_iot_ntp_setopt 配置NTP会话的参数, 常用配置项见 @ref qm_iot_ntp_setopt 的说明
 *
 * 4. 调用 @ref qm_iot_ntp_request 发送NTP请求
 *
 * 5. 收到的UTC时间经SDK处理后会调用由 @ref qm_iot_ntp_setopt 配置的 @ref QM_IOT_NTPOPT_RECV_HANDLER 回调函数, 通知用户当前的时间
 *
 */
#ifndef __QM_IOT_NTP_API_H__
#define __QM_IOT_NTP_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_types.h"

/**
 * @brief ntp模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    QM_IOT_NTPRECV_LOCAL_TIME
} qm_iot_ntp_recv_type_t;

/**
 * @brief ntp模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_ntp_recv_type_t
     */
    qm_iot_ntp_recv_type_t  type;
    union {
        /**
         * @brief utc事件戳以及时区换算后的日期, 以 @ref QM_IOT_NTPOPT_TIME_ZONE 设置的时区为准
         */
        struct {
            uint64_t timestamp;
            uint16_t year;
            uint8_t mon;
            uint8_t day;
            uint8_t hour;
            uint8_t min;
            uint8_t sec;
            uint16_t msec;
        } local_time;
    } data;
} qm_iot_ntp_recv_t;

/**
 * @brief ntp模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle ntp会话句柄
 * @param[in] packet ntp消息结构体, 存放收到的ntp报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_ntp_recv_handler_t)(void *handle, const qm_iot_ntp_recv_t *packet, void *userdata);

/**
 * @brief @ref qm_iot_ntp_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_ntp_setopt 中, data参数的数据类型
 *
 * 1. data的数据类型是char *时, 以配置@ref QM_IOT_NTPOPT_IOT_HANDLE 为例:
 *
 *    void *iot_handle = qm_iot_init();
 *    qm_iot_ntp_setopt(ntp_handle, QM_IOT_NTPOPT_IOT_HANDLE, iot_handle);
 *
 * 2. data的数据类型是其他数据类型时, 以配置@ref QM_IOT_NTPOPT_TIME_ZONE 为例:
 *
 *    int8_t time_zone = 8;
 *    qm_iot_mqtt_setopt(ntp_handle, QM_IOT_NTPOPT_TIME_ZONE, (void *)&time_zone);
 */
typedef enum {
    /**
     * @brief IOT 会话句柄, NTP 通过该句柄复用 device/up 与 device/down 通道
     *
     * @details
     *
     * 数据类型: (void *)
     */
    QM_IOT_NTPOPT_IOT_HANDLE,

    /**
     * @brief ntp会话 获取到utc时间后会根据此时区值转换成本地时间, 再通过 @ref qm_iot_ntp_recv_handler_t 通知
     *
     * @details
     *
     * 取值示例: 东8区, 取值为8; 西3区, 取值为-3
     *
     * 数据类型: (int8_t *)
     */
    QM_IOT_NTPOPT_TIME_ZONE,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户
     * 
     * @details
     * 
     * 数据类型: ( @ref qm_iot_ntp_recv_handler_t )
     */
    QM_IOT_NTPOPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details 这个上下文指针会在 QM_IOT_NTPOPT_RECV_HANDLER 设置的回调被调用时, 由SDK传给用户
     * 
     * 数据类型: (void *)
     */
    QM_IOT_NTPOPT_USERDATA,

    /**
     * @brief 销毁ntp实例时, 等待其他api执行完毕的时间
     *
     * @details
     *
     * 当调用@ref qm_iot_ntp_deinit 销毁NTP实例时, 若继续调用其他qm_iot_ntp_xxx API, API会返回@ref -QM_ERROR 错误
     *
     * 此时, 用户应该停止调用其他qm_iot_ntp_xxx API
     *
     * 数据类型: (uint32_t *) 默认值: (2 * 1000) ms
     */
    QM_IOT_NTPOPT_DEINIT_TIMEOUT_MS,
    QM_IOT_NTPOPT_MAX
} qm_iot_ntp_option_t;

/**
 * @brief 创建ntp会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL ntp实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_ntp_init(void);

/**
 * @brief 配置ntp会话
 *
 * @details
 *
 * 常见的配置项如下
 *
 * + `QM_IOT_NTPOPT_IOT_HANDLE`: IOT 会话句柄, NTP 将通过该句柄使用 device/up 与 device/down 通道
 *
 * + `QM_IOT_NTPOPT_TIME_ZONE`: 时区设置, SDK会将收到的UTC时间按配置的时区进行转换
 *
 * + `QM_IOT_NTPOPT_RECV_HANDLER`: 时间数据接收回调函数, SDK将UTC时间转换完成后, 通过此回调函数输出

 * + `QM_IOT_NTPOPT_USERDATA`: 用户回调上下文, SDK 在调用接收回调时原样传回
 *
 * @param[in] handle ntp会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_ntp_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_ntp_option_t
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_ntp_setopt(void *handle, qm_iot_ntp_option_t option, void *data);

/**
 * @brief 结束ntp会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向ntp会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_ntp_deinit(void **handle);

/**
 * @brief 向ntp服务器发送ntp消息请求
 *
 * @details
 *
 * 发送NTP请求。当前实现通过 device/up 上报 `timeSync`，并通过 device/down 接收 `timeSyncReply`。
 * 然后SDK会调用通过 @ref qm_iot_ntp_setopt 配置的 @ref QM_IOT_NTPOPT_RECV_HANDLER 回调函数, 通知用户当前的时间
 *
 * @param handle ntp会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_ntp_request(void *handle);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_NTP_API_H__ */
