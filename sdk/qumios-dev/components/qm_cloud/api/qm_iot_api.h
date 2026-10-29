#ifndef __QM_IOT_CLOUD_H__
#define __QM_IOT_CLOUD_H__

/**
 * @file qm_iot_api.h
 * @brief 物联网云端对外 API（会话、网络、物模型上报、设备 Topic 等）。
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "qm_spec_api.h"
#include "qm_iot_mqtt.h"
#include "qm_iot_config.h"
#include "qm_iot_common.h"
#include "qm_cloud_log.h"

typedef enum {
    QM_IOT_EVENT_NONE = 0,

    /**
     * @brief 正在连接云端
     */
    QM_IOT_EVENT_DEINIT = 0,

    /**
     * @brief 正在连接云端
     */
    QM_IOT_EVENT_CONNING_CLOUD,
    /**
     * @brief 云端连接成功
     */
    QM_IOT_EVENT_CLOUD_CONN,
    /**
     * @brief 云端断连
     */
    QM_IOT_EVENT_CLOUD_DISCONN,
    /**
     * @brief 外部网络已可用
     */
    QM_IOT_EVENT_NETWORK_UP,
    /**
     * @brief 外部网络已断开
     */
    QM_IOT_EVENT_NETWORK_DOWN,

    /**
     * @brief 绑定通知
     */
    QM_IOT_EVENT_BIND_NOTIFY,

    /**
     * @brief 设备重置
     */
    QM_IOT_EVENT_RESET,
    /**
     * @brief OTA 准备
     */
    QM_IOT_EVENT_OTA_READY,
    /**
     * @brief OTA 开始
     */
    QM_IOT_EVENT_OTA_START,
    /**
     * @brief OTA 下载中
     */
    QM_IOT_EVENT_OTA_INSTALLING,

    /**
     * @brief OTA 进度更新
     */
    QM_IOT_EVENT_OTA_PROGRESS_UPADTE,

    /**
     * @brief OTA 失败
     */
    QM_IOT_EVENT_OTA_FAILED,
    /**
     * @brief OTA 完成
     */
    QM_IOT_EVENT_OTA_SUCCESS,

    /**
     * @brief OTA 取消
     */
    QM_IOT_EVENT_OTA_CANCEL,
    /**
     * @brief 重启通知
     */
    QM_IOT_EVENT_REBOOT_NOTIFY,

    QM_IOT_EVENT_MAX
} qm_iot_event_type_t;

/**
 * @brief 4G/蜂窝设备信息快照，用于 @ref QM_IOT_OPT_DEVINFO 上报。
 *
 * @details JSON 组包时 `imsi` 写入历史协议字段 `iccId`；经纬度可为空字符串。
 */
typedef struct {
    /** 蜂窝身份；上报 JSON 时写入历史字段 `iccId`（协议沿用该 key）。 */
    char imsi[64];
    /** 经度字符串；可为空。 */
    char longitude[32];
    /** 纬度字符串；可为空。 */
    char latitude[32];
} qm_iot_4g_devinfo_t;

typedef struct {
    char mac[18];
    char ssid[33];
    char bssid[18];
    int rssi;
    uint8_t channel;
    /** IPv4 点分十进制字符串（如 192.168.1.1）；未获取时留空则不上报 ip 字段。 */
    char ip[16];
} qm_iot_wifi_devinfo_t;

typedef struct {
    qm_iot_network_type_t type;
    union {
        qm_iot_wifi_devinfo_t wifi;
        qm_iot_4g_devinfo_t modem_4g;
    } data;
} qm_iot_devinfo_t;

typedef struct {
    uint32_t did;
    qm_iot_cloud_info_t cloud_info;
    /** MQTT 用户名；仅当动态注册响应返回该字段时非 NULL。 */
    char *username;
    /** @ref username 的字节长度，不含结尾 '\0'。 */
    uint32_t username_len;
    char *server_cert;
    uint32_t server_cert_len;
    char *client_crt;
    uint32_t client_crt_len;
    char *client_key;
    uint32_t client_key_len;
    char *ota_public_key;
    uint32_t ota_public_key_len;
} qm_iot_cloud_auth_t;

#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT
/**
 * @brief 单条设备日志项；timestamp 单位为 Unix 秒。
 */
typedef struct {
    const char *type;       /**< Comm / Cmd / Data / Error，见 @ref QM_IOT_COMMON_LOG_TYPE_* 。 */
    uint64_t timestamp;      /**< 日志时间戳（秒）。 */
    const char *message;    /**< 日志正文。 */
    const char *source;     /**< 可选来源；NULL 时不写入 JSON。 */
} qm_iot_log_item_t;
#endif

typedef enum {

#if CONFIG_QM_IOT_SPEC_SUPPORT
    /**
     * @brief 接收类型为 QM IOT 下发
     *
     * @details
     *
     */
    QM_IOT_RECV_TYPE_PROPERTY_SET,


    /**
     * @brief 接收类型为 平台回复设备的get请求信息
     *
     * @details
     *
     */
    QM_IOT_RECV_TYPE_PROPERTY_GET_RSP,

    /**
     * @brief 接收类型为 平台向设备请求信息
     *
     * @details
     *
     */
    QM_IOT_RECV_TYPE_PROPERTY_GET_REQ,

#endif

    QM_IOT_RECV_TYPE_MAX
} qm_iot_recv_type_t;


typedef enum {
    QM_IOT_SERVICE_TYPE_NONE = 0,
    QM_IOT_SERVICE_TYPE_SET,
    QM_IOT_SERVICE_TYPE_GET_REQ,
    QM_IOT_SERVICE_TYPE_GET_RSP,
    QM_IOT_SERVICE_TYPE_MAX
} qm_iot_service_type_t;

typedef struct {
    uint8_t siid;
    uint16_t piid;
} qm_iot_service_iid_t;

typedef struct {
    /**
     * @brief 接收类型
     *
     * @details
     *
     */
    qm_iot_recv_type_t type;
    /**
     * @brief 设备DID
     *
     * @details
     *
     */
    uint32_t did;
    /**
     * @brief SIID
     *
     * @details
     *
     */
    uint8_t siid;
    /**
     * @brief PIID
     *
     * @details
     *
     */
    uint16_t piid;
    union {
        /**
         * @brief 小匠spec格式
         *
         * @details
         *
         */
        struct {
            qm_spec_property_t *property;
        }set;

        //平台回复设备的get请求信息
        struct {
            qm_spec_property_t *property;
        }get_rsp;

        //平台向设备请求信息
        struct {
            qm_spec_property_t *property;
        }get_req;

    } data;
} qm_iot_recv_t;

/**
 * @brief device/down 请求上下文。
 *
 * @details
 * 用于按“回复某条下行请求”的语义组织参数，避免调用方重复手写
 * method / subMethod / msg_id。配合 @ref qm_iot_down_reply 使用时，
 * @p method 填下行原始 method，SDK 回复时自动追加 `"Reply"` 后缀。
 */
typedef struct {
    /** @brief 下行 JSON 的 method 字段；回复时无需手动带 `"Reply"` 后缀。 */
    const char *method;
    /** @brief 下行 JSON 的 subMethod 字段；无则填 NULL。 */
    const char *submethod;
    /** @brief 下行 JSON 的 id 字段；回复时原样回传，0 表示由 dev 层自增。 */
    uint32_t msg_id;
} qm_iot_down_req_t;

typedef struct {
    /**
     * @brief 事件类型
     *
     * @details
     *
     */
    qm_iot_event_type_t type;
    union {
        /**
         * @brief QM_IOT_EVENT_OTA_READY
         *
         * @note OTA开始前,会触发QM_IOT_EVENT_OTA_READY事件
         *       若开发者无需进行OTA升级则对其进行置位
         */
        struct
        {
            uint8_t result;
            uint8_t ota_type;
            int version_len;
            char *update_version;
        }ota_ready;

        /**
         * @brief QM_IOT_EVENT_OTA_START
         */
        struct
        {
            uint8_t ota_type;
            uint32_t file_size;

            int version_len;
            char *update_version;

            int extra_len;
            char *extra;

            qm_iot_code_t code;
            uint32_t rsp_block_size;
        }ota_start;

                /**
         * @brief QM_IOT_EVENT_OTA_INSTALLING
         */
        struct {
            uint32_t id;
            uint8_t ota_type;
            uint8_t *ota_data;
            uint16_t data_len;
            qm_iot_code_t code;
        } data_write;

        /**
         * @brief QM_IOT_EVENT_OTA_PROGRESS_UPADTE
         *
         * @note 升级进度 0~100%
         */
        struct
        {
            uint8_t ota_type;
            qm_iot_code_t code;
            uint16_t progress_details;
        }ota_progress_update;

        /**
         * @brief QM_IOT_EVENT_OTA_SUCCESS
         *
         * @note
         */
        struct
        {
            uint8_t ota_type;
            qm_iot_code_t code;
        }ota_success;

        /**
         * @brief QM_IOT_EVENT_OTA_FAILED
         *
         * @note
         */
        struct
        {
            uint8_t ota_type;
            qm_iot_code_t code;
        }ota_failed;

        /**
         * @brief QM_IOT_EVENT_OTA_CANCEL
         *
         * @note
         */
        struct
        {
            uint8_t ota_type;
            qm_iot_code_t code;
        }ota_cancel;

    } data;
} qm_iot_event_t;


/**
 * @brief 通知用户所调用的数据接收回调函数
 *
 * @param[in] handle 会话句柄
 * @param[in] recv 消息结构体, 存放收到的dev报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (*qm_iot_recv_handler_t)(void *handle, const qm_iot_recv_t *recv, void *userdata);

/**
 * @brief 通知用户所调用的相关事件
 *
 * @details
 *
 */
typedef void (*qm_iot_event_handler_t)(void *handle, const qm_iot_event_t *event, void *userdata);

/**
 * @brief 通知用户所调用的服务接收回调函数
 *
 * @param[in] handle 会话句柄
 * @param[in] type 接收类型
 * @param[in] service_id 服务ID
 * @param[in] property_operation 服务属性列表
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (*qm_iot_service_handler_t)(void *handle, int service_id,
                                           qm_iot_service_type_t type, qm_spec_property_operation_t *property_operation,
                                           void *userdata);


typedef enum {

    /**
     * @brief 设备的Version, 可从物联网平台控制台</a>获取
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_OPT_VERSION_STR,

    /**
     * @brief 设备的product id, 可从物联网平台控制台</a>获取
     *
     * @details
     *
     * 数据类型: (uint32_t *)
     */
    QM_IOT_OPT_PRODUCT_ID,

    /**
     * @brief 设备的productSecret, 可从物联网平台控制台</a>获取
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_OPT_PRODUCT_SECRET,

    /**
     * @brief 设备 SN。
     *
     * 数据类型: (char *)
     */
    QM_IOT_OPT_SN,

    /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details
     *
     * 这个上下文指针会在 QM_IOT_OPT_RECV_HANDLER 和 QM_IOT_OPT_EVENT_HANDLER 设置的回调被调用时, 由SDK传给用户
     *
     * 数据类型: (void *)
     */
    QM_IOT_OPT_USERDATA,

    /**
     * @brief 从CLOUD服务器收取的数据从此默认回调函数进行通知
     *
     *
     * 数据类型: ( @ref qm_iot_recv_handler_t )
     */
    QM_IOT_OPT_RECV_HANDLER,

    /**
     * @brief 事件会从此回调函数进行通知, 如网络、上线、断线、OTA 等信息
     *
     * @details
     *
     * 数据类型: ( @ref qm_iot_event_handler_t )
     */
    QM_IOT_OPT_EVENT_HANDLER,

    /**
     * @brief 设备信息快照，应用填充结构体，SDK 内部负责协议 JSON 组包。
     *
     * 数据类型: ( @ref qm_iot_devinfo_t * )
     */
    QM_IOT_OPT_DEVINFO,

    /**
     * @brief 云端认证信息。动态注册由外部调用 dynreg 模块完成后，将结果配置到会话。
     *
     * 数据类型: ( @ref qm_iot_cloud_auth_t * )
     */
    QM_IOT_OPT_CLOUD_CREDENTIAL,

    QM_IOT_OPT_MAX
} qm_iot_option_t;

/**
 * @brief 创建 IOT 会话实例（初始化互斥、事件注册及云端子模块）。
 *
 * @return void *
 * @retval 非NULL 会话句柄，供 @ref qm_iot_setopt / @ref qm_iot_start 等使用。
 * @retval NULL   通常因内存分配失败；部分子模块初始化失败时也会返回 NULL。
 */
void *qm_iot_init(void);

/**
 * @brief 反初始化 IOT 会话并释放资源。
 *
 * @param[in,out] handle 指向会话句柄的指针。
 *
 * @return int32_t
 * @retval QM_EOK     成功（若后续实现完成）。
 * @retval -QM_EINVAL 失败；当前源码中多为占位实现固定返回无效，以工程实际行为为准。
 */
int32_t qm_iot_deinit(void **handle);

/**
 * @brief 配置 IOT 会话参数（产品信息、数据/事件回调、用户上下文等）。
 *
 * @param[in] handle IOT 会话句柄（@ref qm_iot_init 返回值）。
 * @param[in] option 选项，见 @ref qm_iot_option_t；对应 @p data 类型以各选项注释为准。
 * @param[in] data   选项数据指针；不可为 NULL。
 *
 * @return int32_t
 * @retval QM_EOK     设置成功。
 * @retval -QM_EINVAL 参数非法或不支持的 option。
 */
int32_t qm_iot_setopt(void *handle, qm_iot_option_t option, void *data);

/**
 * @brief 启动 IOT 云会话；外部网络可用后需调用 @ref qm_iot_network_update 触发连云。
 *
 * @param[in] handle IOT 会话句柄。
 *
 * @return int32_t
 * @retval QM_EOK     启动流程已执行成功。
 * @retval <QM_EOK    会话状态异常等情况失败。
 */
int32_t qm_iot_start(void *handle);

/**
 * @brief 停止 IOT：断开云端连接（若支持）。
 *
 * @param[in] handle IOT 会话句柄。
 *
 * @return int32_t
 * @retval QM_EOK     停止成功。
 * @retval <QM_EOK    参数非法。
 */
int32_t qm_iot_stop(void *handle);

/**
 * @brief 更新网络状态。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] state 网络状态。
 *
 * @return int32_t
 * @retval QM_EOK     网络状态更新成功。
 * @retval <QM_EOK    参数非法。
 */
int32_t qm_iot_network_update(void *handle, qm_iot_network_state_t state);

/**
 * @brief 上报属性（AWS Shadow 风格通路，后续可能废弃；需开启 PUB 支持）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] property_operation 待上报属性操作结构；调用后由接口内部负责 @c qm_free 释放相关资源。
 *
 * @return int32_t
 * @retval QM_EOK     发送成功。
 * @retval -QM_EINVAL 参数非法、未建连或未编译 PUB 能力等。
 */
int32_t qm_iot_report(void *handle, qm_spec_property_operation_t *property_operation);

#if CONFIG_QM_IOT_SPEC_SUPPORT
/**
 * @brief 按 QM 标准协议上报 spec 属性（需开启 PUB 支持）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] property_operation 属性操作；调用后由接口内部 @c qm_free 。
 *
 * @return int32_t
 * @retval QM_EOK     发送成功。
 * @retval -QM_EINVAL 参数非法或 pub 未就绪。
 */
int32_t qm_iot_property_report(void *handle, qm_spec_property_operation_t *property_operation);


/**
 * @brief 设备向平台发起 GET 请求（QM 标准 spec）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] property_operation 请求内容；调用后由接口内部 @c qm_free 。
 *
 * @return int32_t
 * @retval QM_EOK     发送成功。
 * @retval -QM_EINVAL 参数非法或 pub 未就绪。
 */
int32_t qm_iot_property_get_request(void *handle, qm_spec_property_operation_t *property_operation);


/**
 * @brief 设备回复平台的 GET 请求（QM 标准 spec）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] property_operation 应答内容；调用后由接口内部 @c qm_free 。
 *
 * @return int32_t
 * @retval QM_EOK     发送成功。
 * @retval -QM_EINVAL 参数非法或 pub 未就绪。
 */
int32_t qm_iot_property_get_rsponse(void *handle, qm_spec_property_operation_t *property_operation);
#endif
/**
 * @brief 触发模块重置/解绑相关流程（如向云端发送解绑通知，具体见实现）。
 *
 * @param[in] handle IOT 会话句柄。
 *
 * @return int32_t
 * @retval QM_EOK     流程已触发。
 * @retval -QM_EINVAL 句柄无效。
 */
int32_t qm_iot_reset(void *handle);

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
/**
 * @brief 向云端上报 MCU/模组版本等信息（OTA 子模块）。
 *
 * @param[in] handle      IOT 会话句柄。
 * @param[in] moudle_ver  模块版本字符串（命名与现有 API 保持一致）。
 * @param[in] ver_info   版本详情结构体，语义见 @ref qm_iot_mcu_ver_info_t 。
 *
 * @return int
 * @retval QM_EOK     上报成功。
 * @retval -QM_EINVAL 句柄或 OTA 子模块无效。
 */
int qm_iot_version_update(void *handle, char *moudle_ver, qm_iot_mcu_ver_info_t *ver_info);

#endif

/**
 * @brief 按 service_id 与一组 siid/piid 注册平台下发的服务回调。
 *
 * @param[in] handle      IOT 会话句柄。
 * @param[in] service_id  服务编号。
 * @param[in] iid         属性索引数组，元素类型 @ref qm_iot_service_iid_t 。
 * @param[in] iid_num     @p iid 元素个数。
 * @param[in] service_fn  匹配到下发时的回调，见 @ref qm_iot_service_handler_t 。
 * @param[in] userdata    用户上下文，回调时回传。
 *
 * @details 多组 siid/piid 时，平台一次下发若带齐注册项才会触发 callback；需分次触发可多次注册。
 *
 * @return int
 * @retval QM_EOK     注册成功。
 * @retval <0         参数非法或内部错误。
 */
int qm_iot_service_register(void *handle, int service_id, qm_iot_service_iid_t *iid, int iid_num,
                                qm_iot_service_handler_t service_fn, void *userdata);

/**
 * @brief 经设备 Topic 上行 JSON 报文（转调 @ref qm_iot_dev_up_report ）。
 *
 * @param[in] handle      IOT 会话句柄（内部需已创建并挂载 dev 子句柄）。
 * @param[in] param      method 字符串。
 * @param[in] msg_id     报文 id；0 表示由 dev 层自增。
 * @param[in] payload    可选 JSON params 片段。
 * @param[in] payload_len @p payload 长度。
 *
 * @return int32_t
 * @retval 与 @ref qm_iot_dev_up_report 一致；若未挂载 dev_handle 则返回 -QM_EINVAL 。
 *
 * @note 建议在云端连接就绪、dev/MQTT 已配置后再调用。
 */
int32_t qm_iot_up_report(void *handle, char *param, uint32_t msg_id, char *payload, int payload_len);

/**
 * @brief 经设备 Topic 上行 JSON 报文，支持可选顶层 subMethod。
 *
 * @param[in] handle IOT 会话句柄（内部需已创建并挂载 dev 子句柄）。
 * @param[in] param method 字符串。
 * @param[in] submethod 可选 subMethod 字符串；NULL 或空字符串时不写入。
 * @param[in] msg_id 报文 id；0 表示由 dev 层自增。
 * @param[in] payload 可选 JSON params 片段。
 * @param[in] payload_len @p payload 长度。
 *
 * @return int32_t
 * @retval 与 @ref qm_iot_dev_up_report_ex 一致；若未挂载 dev_handle 则返回 -QM_EINVAL。
 */
int32_t qm_iot_up_report_ex(void *handle,
                            char *param,
                            char *submethod,
                            uint32_t msg_id,
                            char *payload,
                            int payload_len);

/**
 * @brief 回复 device/down 下行报文，支持可选顶层 subMethod。
 *
 * @details
 * 经 device/up 发布回复 JSON，转调 @ref qm_iot_dev_reply_publish 。
 * @p param 传入下行原始 method（如 `"notifyOta"`），SDK 写入 JSON 前自动追加
 * `"Reply"` 后缀；若 @p param 已以 `"Reply"` 结尾则保持不变。
 *
 * @param[in] handle      IOT 会话句柄（内部需已挂载 dev 子句柄）。
 * @param[in] param       下行原始 method 字符串；非完整 Reply method 名。
 * @param[in] submethod   可选顶层 subMethod；NULL 或空字符串时不写入。
 * @param[in] msg_id      报文 id，通常与下行请求 id 一致；0 表示由 dev 层自增。
 * @param[in] code        业务结果码，写入 JSON 的 code 字段。
 * @param[in] payload     可选 params JSON 片段；可为 NULL。
 * @param[in] payload_len @p payload 字节长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK     发布请求已发起。
 * @retval -QM_EINVAL 参数非法、method 过长或未挂载 dev_handle。
 * @retval -QM_ENOMEM JSON 组装内存分配失败。
 */
int32_t qm_iot_down_reply_ex(void *handle,
                             char *param,
                             char *submethod,
                             uint32_t msg_id,
                             int code,
                             char *payload,
                             int payload_len);

/**
 * @brief 按下行请求上下文回复 device/down 报文。
 *
 * @details
 * 使用 @p req 中的 method、subMethod、msg_id 组装回复，经 device/up 发布。
 * @p req->method 为下行原始 method，SDK 写入 JSON 前自动追加 `"Reply"` 后缀。
 *
 * @param[in] handle      IOT 会话句柄（内部需已挂载 dev 子句柄）。
 * @param[in] req         下行请求上下文；至少需提供 method；msg_id 为 0 时由 dev 层自增。
 * @param[in] code        业务结果码，写入 JSON 的 code 字段。
 * @param[in] payload     可选 params JSON 片段；可为 NULL。
 * @param[in] payload_len @p payload 字节长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK     发布请求已发起。
 * @retval -QM_EINVAL 参数非法、method 过长或未挂载 dev_handle。
 * @retval -QM_ENOMEM JSON 组装内存分配失败。
 */
int32_t qm_iot_down_reply(void *handle,
                          const qm_iot_down_req_t *req,
                          int code,
                          char *payload,
                          int payload_len);

/**
 * @brief 为下行设备报文中的 JSON method 注册处理函数（转调 @ref qm_iot_dev_down_register ）。
 *
 * @param[in] handle        IOT 会话句柄。
 * @param[in] param         与 method 匹配的字符串。
 * @param[in] recv_handler MQTT 侧收包回调类型，语义同 @ref qm_iot_mqtt_recv_handler_t ；首参为 dev 子句柄。
 * @param[in] user_data     随回调传入的上下文。
 *
 * @return int32_t
 * @retval 与 @ref qm_iot_dev_down_register 一致；未挂载 dev_handle 则 -QM_EINVAL 。
 *
 * @note 建议在连云就绪、下行订阅已建立后再依赖该回调。
 */
int32_t qm_iot_down_register(void *handle, char *param, qm_iot_mqtt_recv_handler_t recv_handler, void *user_data);

/**
 * @brief 取消下行设备报文中的 JSON method 注册（转调 @ref qm_iot_dev_down_unregister ）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] param  已注册的 method 字符串。
 *
 * @return int32_t
 * @retval 与 @ref qm_iot_dev_down_unregister 一致；未挂载 dev_handle 则 -QM_EINVAL 。
 */
int32_t qm_iot_down_unregister(void *handle, char *param);

/**
 * @brief 获取当前 MQTT 句柄，供需要 MQTT 通道的独立子模块绑定使用。
 *
 * @param[in] handle IOT 会话句柄。
 *
 * @return MQTT 句柄；句柄无效或未启用 MQTT 时返回 NULL。建议在 @ref QM_IOT_EVENT_CLOUD_CONN 后使用。
 */
void *qm_iot_get_mqtt_handle(void *handle);

#if CONFIG_QM_IOT_LOG_PUSH_SUPPORT

/**
 * @brief 经 /{pid}/{did}/device/log 上报 logInfo（转调 @ref qm_iot_dev_log_push ）。
 *
 * @param[in] handle    IOT 会话句柄。
 * @param[in] log_level logLevel 字符串。
 * @param[in] items     日志项数组。
 * @param[in] item_num  元素个数。
 * @param[in] msg_id    报文 id；0 表示自增。
 *
 * @return int32_t 与 @ref qm_iot_dev_log_push 一致；未挂载 dev_handle 则 -QM_EINVAL 。
 */
int32_t qm_iot_log_push(void *handle, const char *log_level,
                        const qm_iot_log_item_t *items, uint8_t item_num,
                        uint32_t msg_id);

/**
 * @brief 经 /{pid}/{did}/device/log 上报 logInfo（外部 JSON 数组，转调 @ref qm_iot_dev_log_push_raw ）。
 *
 * @param[in] handle              IOT 会话句柄。
 * @param[in] log_level           logLevel 字符串。
 * @param[in] payload_array_json  JSON 数组片段 "[{...}]"。
 * @param[in] msg_id              报文 id；0 表示自增。
 *
 * @return int32_t 与 @ref qm_iot_dev_log_push_raw 一致；未挂载 dev_handle 则 -QM_EINVAL 。
 */
int32_t qm_iot_log_push_raw(void *handle, const char *log_level,
                            const char *payload_array_json, uint32_t msg_id);

#endif /* CONFIG_QM_IOT_LOG_PUSH_SUPPORT */

/**
 * @brief 投递一次状态/事件（多用于 SDK 内部；应用层非必要勿用）。
 *
 * @param[in] handle IOT 会话句柄。
 * @param[in] event  事件类型，见 @ref qm_iot_event_type_t 。
 *
 * @return int32_t
 * @retval QM_EOK     已投递。
 * @retval -QM_EINVAL 句柄无效。
 */
int32_t qm_iot_event_notify(void *handle, qm_iot_event_type_t event);

/**
 * @brief 追加注册网络、连云、OTA 等业务事件回调（与 @ref QM_IOT_OPT_EVENT_HANDLER 单槽并存）。
 *
 * @param[in] handle   IOT 会话句柄。
 * @param[in] handler  事件回调，不可为 NULL。
 * @param[in] userdata 回调用户上下文，可为 NULL。
 *
 * @return int32_t
 * @retval QM_EOK     注册成功。
 * @retval -QM_EINVAL 句柄或 handler 无效，或相同 handler+userdata 已注册。
 * @retval -QM_EFULL  已达 @ref CONFIG_QM_IOT_EVENT_LISTENER_NUM 上限。
 *
 * @note 可多次调用；事件发生时按槽位下标顺序依次通知各回调。
 * @note 槽位数量由 @ref CONFIG_QM_IOT_EVENT_LISTENER_NUM 配置（默认 4，静态数组无堆分配）。
 * @note 经 @ref QM_IOT_OPT_EVENT_HANDLER 设置的回调独立保留，与本接口追加的回调一并触发。
 */
int32_t qm_iot_event_register(void *handle,
                              qm_iot_event_handler_t handler,
                              void *userdata);

/**
 * @brief 移除一条由 @ref qm_iot_event_register 注册的回调。
 *
 * @param[in] handle   IOT 会话句柄。
 * @param[in] handler  待移除的回调，不可为 NULL。
 * @param[in] userdata 注册时传入的 userdata（含 NULL）。
 *
 * @return int32_t
 * @retval QM_EOK     移除成功。
 * @retval -QM_EINVAL 句柄或 handler 无效，或未找到匹配项。
 */
int32_t qm_iot_event_unregister_one(void *handle,
                                    qm_iot_event_handler_t handler,
                                    void *userdata);

/**
 * @brief 清除全部事件回调（含 @ref QM_IOT_OPT_EVENT_HANDLER 单槽与 register 列表）。
 *
 * @param[in] handle IOT 会话句柄。
 *
 * @return int32_t
 * @retval QM_EOK     清除成功。
 * @retval -QM_EINVAL 句柄无效。
 *
 * @note 不会清除 @ref QM_IOT_OPT_USERDATA ；下行 recv 仍可使用原上下文。
 */
int32_t qm_iot_event_unregister(void *handle);

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_CLOUD_H__ */
