#ifndef __QM_IOT_OTA_H__
#define __QM_IOT_OTA_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "qm_iot_config.h"
#include "qm_iot_common.h"

#ifndef CONFIG_QM_IOT_OTA_PROGRESS_GRADE 
#define CONFIG_QM_IOT_OTA_PROGRESS_GRADE        5
#endif


typedef enum {
    QM_IOT_OTA_EVENT_NONE,
    QM_IOT_OTA_EVENT_READY,
    QM_IOT_OTA_EVENT_START,
    QM_IOT_OTA_EVENT_DATA_INSTALLING,
    QM_IOT_OTA_EVENT_PROGRESS_UPADTE,
    QM_IOT_OTA_EVENT_SUCCESS,
    QM_IOT_OTA_EVENT_FAILED,
    QM_IOT_OTA_EVENT_CANCEL,
    QM_IOT_OTA_EVENT_MAX
} qm_iot_ota_event_type_t;

typedef enum
{
    QM_IOT_OTA_TYPE_MOUDLE = 0,
    QM_IOT_OTA_TYPE_MCU    = 1,
    QM_IOT_OTA_TYPE_MAX ,
}qm_iot_ota_type_t;

typedef enum
{
    QM_IOT_OTA_REQ_RESULT_OK = 0,
    QM_IOT_OTA_REQ_RESULT_BUSY = 1,             //设备繁忙
    QM_IOT_OTA_REQ_RESULT_VERSION_ERROR = 2,    //该版本不允许升级
}qm_iot_ota_req_result_t;

typedef struct {
    qm_iot_ota_event_type_t type;
    qm_iot_ota_type_t ota_type;
    union 
    {

        /**
         * @brief QM_IOT_OTA_EVENT_READY
         * 
         * @note OTA开始前,会触发QM_IOT_OTA_EVENT_READY事件
         *       若开发者无需进行OTA升级则对其result进行相应错误码赋值
         */

        struct 
        {
            qm_iot_ota_req_result_t result;
            int version_len;
            char *update_version;
        }ready;
        
        /**
         * @brief QM_IOT_OTA_EVENT_START
         */
        struct 
        {
            qm_iot_code_t code;
            uint32_t file_size;
            uint32_t block_size;

            int version_len;
            char *update_version;

            int extra_len;
            char *extra;
        }start;
        
        /**
         * @brief QM_IOT_OTA_EVENT_DATA_WRITE
         */
        struct {
            uint8_t *ota_data;
            uint16_t data_len;
            qm_iot_code_t code;
            uint32_t id;
        } data_write;

        /**
         * @brief QM_IOT_OTA_EVENT_PROGRESS_UPADTE
         */
        struct {
            qm_iot_code_t code;
            uint16_t progress_details;
        } progress_update;

    }data;

} qm_iot_ota_event_t;

/**
 * @brief dynreg模块收到从网络上来的报文时, 通知用户所调用的数据回调函数
 *
 * @param[in] handle dynreg会话句柄
 * @param[in] packet dynreg消息结构体, 存放收到的dynreg报文内容
 * @param[in] userdata 用户上下文
 *
 * @return void
 */
typedef void (* qm_iot_ota_event_handler_t)(void *handle, const qm_iot_ota_event_t *event, void *userdata);

typedef enum {
    /**
     * @brief 模块依赖的MQTT句柄
     *
     * @details
     *
     * shadow模块依赖底层的MQTT模块, 用户必需配置正确的MQTT句柄, 否则无法正常工作, 数据类型为(void *)
     */
    QM_IOT_OTAOPT_MQTT_HANDLE,

    /**
     * @brief 设备的版本号
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_OTAOPT_VER,

    /**
     * @brief 设备的MCU版本号
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_OTAOPT_MCU_VER,

    /**
     * @brief 设备的Base64 和 DER 编码的 X.509 证书, 可从云物联网平台控制台或者动态注册</a>获取
     *
     * @details
     *
     * 数据类型: (char *)
     */
    QM_IOT_OTAOPT_SINGATURE,


    /**
     * @brief 事件会从此回调函数进行通知, 如上线/断线/重新上线/OTA 状态等信息
     *
     * @details
     *
     * 数据类型: ( @ref qm_iot_ota_event_handler_t )
     */
    QM_IOT_OTAOPT_EVENT_HANDLER,

        /**
     * @brief 用户需要SDK暂存的上下文
     *
     * @details
     *
     * 这个上下文指针会在 QM_IOT_OTAOPT_EVENT_HANDLER设置的回调被调用时, 由SDK传给用户
     *
     * 数据类型: (void *)
     */
    QM_IOT_OTAOPT_USERDATA,

    /**
     * @brief 重置 MQTT 预订阅门闩, 允许再次按当前 pid/did 注册下行 topic
     *
     * @details
     * 清除 mqtt_pre_sub_done，并把内部 mqtt_handle 置空；不立刻订阅。
     * 典型用法：MQTT 会话销毁重建后，先 RESET_SUB，再重新 setopt MQTT_HANDLE / USERDATA。
     * data 可传 NULL。
     */
    QM_IOT_OTAOPT_RESET_SUB,

    QM_IOT_OTA_MAX,
} qm_iot_ota_option_t;

/**
 * @brief 创建ota会话实例, 并以默认值配置会话参数
 *
 * @return void *
 * @retval 非NULL ota实例的句柄
 * @retval NULL   初始化失败, 一般是内存分配失败导致
 * 
 */
void *qm_iot_ota_init(void);

/**
 * @brief 配置ota会话
 *
 * @param[in] handle ota会话句柄
 * @param[in] option 配置选项, 更多信息请参考@ref qm_iot_ota_option_t
 * @param[in] data   配置选项数据, 更多信息请参考@ref qm_iot_ota_option_t
 *
 * @return int32_t
 * @retval =QM_EOK 执行成功, <QM_EOK 执行失败
 *
 */
int32_t qm_iot_ota_setopt(void *handle, qm_iot_ota_option_t option, void *data);


/**
 * @brief 启动ota流程
 *
 * @param handle dynreg会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK  数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_ota_start(void *handle);


/**
 * @brief 暂停ota流程
 *
 * @param handle dynreg会话句柄
 *
 * @return int32_t
 * @retval <QM_EOK  数据接收失败
 * @retval >=QM_EOK 数据接收成功
 */
int32_t qm_iot_ota_stop(void *handle);

/**
 * @brief 结束ota会话, 销毁实例并回收资源
 *
 * @param[in] handle 指向ota会话句柄的指针
 *
 * @return int32_t
 * @retval <QM_EOK  执行失败
 * @retval >=QM_EOK 执行成功
 * 
 */
int32_t qm_iot_ota_deinit(void **handle);

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
/**
 * @brief 版本信息上报
 * 
 * @param[in] handle 会话句柄
 * @param[in] version 服务ID
 * @param[in] ver_info 用户上下文
 * 
 */
int qm_iot_ota_version_report(void *handle, char *version, qm_iot_mcu_ver_info_t *ver_info);
#endif

#if defined(__cplusplus)
}
#endif

#endif  /* __QM_IOT_OTA_H__ */
