#ifndef __QM_IOT_DYNREG_H__
#define __QM_IOT_DYNREG_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "qm_iot_common.h"
#include "qm_iot_config.h"

/**
 * @brief 动态注册 MQTT/TLS 凭据结果。
 */
typedef struct {
    uint32_t did;
    uint16_t mqtt_port;
    char *mqtt_host;
    uint32_t mqtt_host_len;
    char *server_cert;
    uint32_t server_cert_len;
    char *client_crt;
    uint32_t client_cert_len;
    char *client_private_key;
    uint32_t client_privkey_len;
    char *public_key;
    uint32_t public_key_len;
} qm_iot_dynreg_device_info_t;

/**
 * @brief dynreg模块收到从网络上来的报文时, 通知用户的报文类型
 */
typedef enum {
    /**
     * @brief dynreg模块返回的http status code
     */
    QM_IOT_DYNREGRECV_STATUS_CODE,
    /**
     * @brief dynreg模块返回的设备信息
     */
    QM_IOT_DYNREGRECV_DEVICE_INFO,
} qm_iot_dynreg_recv_type_t;

/**
 * @brief dynreg模块收到从网络上来的报文时, 通知用户的报文内容
 */
typedef struct {
    /**
     * @brief 报文内容所对应的报文类型, 更多信息请参考@ref qm_iot_dynreg_recv_type_t
     */
    qm_iot_dynreg_recv_type_t  type;
    union {
        /**
         * @brief dynreg模块返回的http status code
         */
        struct {
            uint32_t code;
        } status_code;
        /**
         * @brief dynreg模块返回的设备信息
         */
        qm_iot_dynreg_device_info_t device_info;
    } data;
} qm_iot_dynreg_recv_t;

/**
 * @brief dynreg模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle dynreg会话句柄
 * @param[in] packet dynreg消息结构体, 存放收到的dynreg报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_dynreg_recv_handler_t)(void *handle, const qm_iot_dynreg_recv_t *packet, void *userdata);

/**
 * @brief dynreg 默认服务器环境。
 *
 * @details dynreg host 的选择优先级如下:
 *          1. 应用通过 @ref QM_IOT_DYNREGOPT_HOST 显式传入的 host
 *          2. dynreg 模块内部已保存的 host
 *          3. 根据当前环境选择的内置默认 host
 */
typedef enum {
    /** @brief 开发环境。 */
    QM_IOT_DYNREG_ENV_DEV = 0,
    /** @brief 测试环境。 */
    QM_IOT_DYNREG_ENV_TEST,
    /** @brief 正式环境。 */
    QM_IOT_DYNREG_ENV_PROD,
} qm_iot_dynreg_env_t;

/**
 * @brief @ref qm_iot_dynreg_setopt 接口的option参数可选值.
 *
 * @details 下文每个选项中的数据类型, 指的是@ref qm_iot_dynreg_setopt 中, data参数的数据类型
 *
 * 1. data的数据类型是char *时, 以配置@ref QM_IOT_DYNREGOPT_HOST 为例:
 *
 *    char *host = "xxx";
 *    qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_HOST, host);
 *
 * 2. data的数据类型是其他数据类型时, 以配置@ref QM_IOT_DYNREGOPT_PORT 为例:
 *
 *    uint16_t port = 443;
 *    qm_iot_dynreg_setopt(dynreg_handle, QM_IOT_DYNREGOPT_PORT, (void *)&port);
 */
typedef enum {
    /**
     * @brief http动态注册服务器的域名地址或者 IP 地址
     *
     * @details
     *
     * host 选择优先级最高。若传入非空 host，当前 dynreg 流程优先使用该 host，
     * 并同步更新 dynreg 模块内部保存的 host；若未设置或传入空字符串，
     * dynreg 启动时会先尝试使用内部已保存的 host，再回退到当前环境对应的内置默认 host。
     *
     * 物联网平台 http动态注册 服务器域名地址列表:
     *
     * | 域名地址                                        | 区域    | 端口号
     * |-------------------------------------------------|---------|---------
     * | XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX              | 国内    | 443
     * | XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX              | 海外    | 443
     *
     * 数据类型: (char *)
     */
    QM_IOT_DYNREGOPT_HOST,

    /**
     * @brief 动态注册产品 ID。
     *
     * 数据类型: (uint32_t *)
     */
    QM_IOT_DYNREGOPT_PRODUCT_ID,

    /**
     * @brief 动态注册产品密钥。
     *
     * 数据类型: (char *)
     */
    QM_IOT_DYNREGOPT_PRODUCT_SECRET,

    /**
     * @brief 设备 SN。
     *
     * 数据类型: (char *)
     */
    QM_IOT_DYNREGOPT_SN,

    /**
     * @brief http动态注册 服务器的端口号
     *
     * @details
     *
     * 连接阿里云物联网平台 http动态注册 服务器时:
     *
     * 必须使用tls方式建联, 端口号设置为443
     *
     * 数据类型: (uint16_t *)
     */
    QM_IOT_DYNREGOPT_PORT,

    /**
     * @brief dynreg会话接收消息时可消费的最长时间间隔
     *
     * @details
     *
     * 数据类型: (uint32_t) 默认值: (5 * 1000) ms
     */
    QM_IOT_DYNREGOPT_RECV_TIMEOUT_MS,

    /**
     * @brief 设置回调, 它在SDK收到网络报文的时候被调用, 告知用户
     *
     * @details
     *
     * 数据类型: (qm_iot_dynreg_http_recv_handler_t)
     */
    QM_IOT_DYNREGOPT_RECV_HANDLER,

    /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details
     *
     * 这个上下文指针会在 QM_IOT_DYNREGOPT_RECV_HANDLER 和 QM_IOT_DYNREGOPT_EVENT_HANDLER 设置的回调被调用时, 由SDK传给用户
     *
     * 数据类型: (void *)
     */
    QM_IOT_DYNREGOPT_USERDATA,


    /**
     * @brief dynreg 模块接收重试次数
     *
     * @details
     *
     * 数据类型: (uint32_t) 默认值: 3
     */
    QM_IOT_DYNREGOPT_MAX_RETRY_NUM,

    QM_IOT_IOTDYNREGOPT_MAX
} qm_iot_dynreg_option_t;


/**
 * @brief 创建dynreg会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL dynreg实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 *
 */
void *qm_iot_dynreg_init(void);

/**
 * @brief 设置 dynreg 默认服务器环境。
 *
 * @details 仅在 dynreg 启动时未通过 @ref QM_IOT_DYNREGOPT_HOST 传入非空 host，
 *          且 dynreg 模块内部也没有已保存 host 时，才会使用该环境对应的内置默认 host。
 *
 * @param[in] env 目标环境。
 *
 * @return int32_t
 * @retval <QM_EOK  参数非法
 * @retval >=QM_EOK 设置成功
 */
int32_t qm_iot_dynreg_env_set(qm_iot_dynreg_env_t env);

/**
 * @brief 获取当前 dynreg 默认服务器环境。
 *
 * @return 当前环境。
 */
qm_iot_dynreg_env_t qm_iot_dynreg_env_get(void);

/**
 * @brief 配置dynreg会话
 *
 * @param[in] handle dynreg会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_dynreg_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_dynreg_option_t
 *
 * @return int32_t
 * @retval <QM_EOK  参数配置失败
 * @retval >=QM_EOK 参数配置成功
 *
 */
int32_t qm_iot_dynreg_setopt(void *handle, qm_iot_dynreg_option_t option, void *data);

/**
 * @brief 启动 dynreg 流程：内部完成请求体组包、签名并发送 HTTP 请求。
 *
 * @param[in] handle dynreg 会话句柄。
 * @param[in] type   当前网络类型，须大于 @ref QM_IOT_NETWORK_TYPE_NONE。
 *
 * @return int32_t
 * @retval <QM_EOK  参数非法、组包失败或数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_dynreg_start(void *handle, qm_iot_network_type_t type);

/**
 * @brief 结束dynreg会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向dynreg会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK  执行失败
 * @retval >=QM_EOK 执行成功
 *
 */
int32_t qm_iot_dynreg_deinit(void **handle);



#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_DYNREG_H__ */
