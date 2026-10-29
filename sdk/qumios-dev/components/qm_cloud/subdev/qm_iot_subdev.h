/**
 * @file qm_iot_subdev.h
 * @brief subdev模块头文件, 提供子设备管理的能力
 *
 * @copyright Copyright (C) 2024 Wells. All rights reserved.
 *
 */
#ifndef __QM_IOT_SUBDEV_API_H__
#define __QM_IOT_SUBDEV_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm_types.h"
#include "qm_iot_common.h"

typedef enum {
    QM_IOT_SUBDEV_TYPE_NONE,
    QM_IOT_SUBDEV_TYPE_BLE_MESH,
    QM_IOT_SUBDEV_TYPE_MAX
}qm_iot_subdev_type_t;

typedef struct {
    uint32_t pid;
    uint32_t did;
    uint8_t flag;
    uint8_t element_num;
    uint16_t unicast_addr;
    uint32_t iv_index;
    uint32_t net_index;
    uint8_t mac[6];
    uint8_t dev_key[16];
}qm_iot_subdev_ble_mesh_dev_t;

typedef struct {
    uint8_t                         net_key[16];
    uint8_t                         app_key[16];
    uint16_t                        unicast_addr;
    uint16_t                        dev_num;
    qm_iot_subdev_ble_mesh_dev_t    *ble_mesh_dev;
} qm_iot_subdev_ble_mesh_t;

typedef struct {
    uint32_t did;
} qm_iot_subdev_ble_mesh_del_t;

typedef struct {
    int8_t rssi;
    uint8_t ttl;
} qm_iot_subdev_ble_mesh_state_t;

typedef struct {
    qm_iot_subdev_type_t dev_type;
    uint32_t did;
    int state;
    union 
    {
        qm_iot_subdev_ble_mesh_state_t  ble_mesh;
    }data;
} qm_iot_subdev_state_t;

/**
 * @brief subdev模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    QM_IOT_SUBDEVRECV_NONE,
    QM_IOT_SUBDEVRECV_STATE_NOTIFY,
    QM_IOT_SUBDEVRECV_STATE_REQ_REPLY,
    QM_IOT_SUBDEVRECV_TOPO_GET_REPLY,
    QM_IOT_SUBDEVRECV_TOPO_ADD_NOTIFY,
    QM_IOT_SUBDEVRECV_TOPO_DEL_NOTIFY,
    QM_IOT_SUBDEVRECV_MAX
} qm_iot_subdev_recv_type_t;

typedef struct {
    qm_iot_code_t                       code;
    uint32_t                            msg_id;
    qm_iot_subdev_type_t                type;
    union 
    {
        qm_iot_subdev_ble_mesh_t        ble_mesh;
    }data;
    
} qm_iot_subdev_topo_get_reply_t;

typedef struct {
    qm_iot_code_t        code;
    uint32_t             msg_id;
} qm_iot_subdev_generic_reply_t;

typedef struct {
    uint32_t                            msg_id;
    qm_iot_subdev_type_t                type;
    uint16_t                            dev_num;
    union 
    {
        qm_iot_subdev_ble_mesh_dev_t    *ble_mesh_dev;
    }data;
} qm_iot_subdev_topo_add_notify_t;

typedef struct {
    uint32_t                                msg_id;
    qm_iot_subdev_type_t                    type;
    uint16_t                                dev_num;
    union 
    {
        qm_iot_subdev_ble_mesh_del_t        *ble_mesh_del;
    }data;
            
} qm_iot_subdev_topo_del_notify_t;

typedef struct {
    uint32_t                                msg_id;
    uint16_t                                dev_num;
    qm_iot_subdev_state_t                   *dev_state;
} qm_iot_subdev_state_notify_t;


/**
 * @brief subdev模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_subdev_recv_type_t
     */
    qm_iot_subdev_recv_type_t type;
    union {
        /**
         * @brief 返回云端设备列表
         */
        qm_iot_subdev_topo_get_reply_t topo_reply;
        /**
         * @brief 从设备发起设备状态请求消息后，收到的云端的应答
         */
        qm_iot_subdev_generic_reply_t generic_reply;
        /**
         * @brief 收到的云端的通知
         */
        qm_iot_subdev_topo_add_notify_t add_notify;
        /**
         * @brief 收到的云端子设备绑定关系的通知
         */
        qm_iot_subdev_topo_del_notify_t del_notify;
        /**
         * @brief 收到的云端子设备登入/登出的通知
         */
        qm_iot_subdev_state_notify_t state_notify;
    } data;
} qm_iot_subdev_recv_t;

/**
 * @brief subdev模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle subdev会话句柄
 * @param[in] packet subdev消息结构体, 存放收到的subdev报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_subdev_recv_handler_t)(void *handle, const qm_iot_subdev_recv_t *packet, void *user_data);

/**
 * @brief @ref qm_iot_subdev_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_subdev_setopt 中, data参数的数据类型
 *
 * 1. data的数据类型是char *时, 以配置@ref QM_IOT_SUBDEVOPT_MQTT_HANDLE 为例:
 *
 *    void *mqtt_handle = qm_iot_mqtt_init();
 *    qm_iot_subdev_setopt(subdev_handle, QM_IOT_SUBDEVOPT_MQTT_HANDLE, mqtt_handle);
 */
typedef enum {
    /**
     * @brief subdev会话 需要的MQTT句柄, 需要先建立MQTT连接，再设置MQTT句柄
     * 
     * @details
     * 
     * 数据类型: (void *)
     */
    QM_IOT_SUBDEVOPT_MQTT_HANDLE,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户, 数据类型为(qm_iot_subdev_recv_handler_t)
     * 
     * @details
     * 
     * 数据类型: (qm_iot_subdev_recv_handler_t)
     */
    QM_IOT_SUBDEVOPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文, 数据类型为(void *)
     *
     * @details 这个上下文指针会在 QM_IOT_SUBDEVOPT_RECV_HANDLER 和 QM_IOT_SUBDEVOPT_EVENT_HANDLER 设置的回调被调用时, 由SDK传给用户
     * 
     * 数据类型: (void *)
     */
    QM_IOT_SUBDEVOPT_USERDATA,
    QM_IOT_SUBDEVOPT_MAX
} qm_iot_subdev_option_t;

/**
 * @brief 创建subdev会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL subdev实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_subdev_init(void);

/**
 * @brief 配置subdev会话
 *
 * @param[in] handle subdev会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_subdev_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_subdev_option_t
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_subdev_setopt(void *handle, qm_iot_subdev_option_t option, void *data);

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
int32_t qm_iot_subdev_deinit(void **handle);

/**
 * @brief 向物联网平台发送查询子设备与网关topo关系的请求
 *
 * @param handle subdev会话句柄
 * 
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_subdev_topo_get(void *handle);

/**
 * @brief 向物联网平台上报子设备在线状态
 *
 * @param handle subdev会话句柄
 * @param subdev_state 子设备状态数组
 * @param dev_num 子设备数组中的子设备数量
 * 
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t qm_iot_subdev_state_report(void *handle, qm_iot_subdev_state_t *subdev_state, int dev_num);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_SUBDEV_API_H__ */

