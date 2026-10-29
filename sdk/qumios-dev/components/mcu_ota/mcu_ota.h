#ifndef MCU_OTA_H
#define MCU_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 操作成功。 */
#define MCU_OTA_OK                    0
/** 参数为空、长度非法或配置无效。 */
#define MCU_OTA_ERR_PARAM            (-1)
/** 串口帧头、版本、长度或校验和错误。 */
#define MCU_OTA_ERR_FRAME            (-2)
/** 通过 send_cb 发送 ACK/NOTIFY 失败。 */
#define MCU_OTA_ERR_SEND             (-3)

/** 单个 OTA_DATA 分片支持的最大固件数据长度。 */
#define MCU_OTA_MAX_CHUNK_SIZE       1024U
/** CRC32 校验值长度。 */
#define MCU_OTA_CRC32_VALUE_LEN      4U
/** MD5 校验值长度。 */
#define MCU_OTA_MD5_VALUE_LEN        16U

/** 是否启用 CRC32 校验，1 表示启用，0 表示关闭。 */
#ifndef MCU_OTA_USE_CRC32
#define MCU_OTA_USE_CRC32            1
#endif

/** 是否启用 MD5 校验，1 表示启用，0 表示关闭。 */
#ifndef MCU_OTA_USE_MD5
#define MCU_OTA_USE_MD5              0
#endif

#if (MCU_OTA_USE_CRC32 != 0 && MCU_OTA_USE_CRC32 != 1) || \
    (MCU_OTA_USE_MD5 != 0 && MCU_OTA_USE_MD5 != 1)
#error "MCU_OTA_USE_CRC32 and MCU_OTA_USE_MD5 must be 0 or 1"
#endif

#if !MCU_OTA_USE_CRC32 && !MCU_OTA_USE_MD5
#error "MCU_OTA_USE_CRC32 or MCU_OTA_USE_MD5 must be 1"
#endif

/** check_value 缓冲区长度。CRC32-only 为 4 字节，启用 MD5 时为 16 字节。 */
#if MCU_OTA_USE_MD5
#define MCU_OTA_CHECK_VALUE_LEN      MCU_OTA_MD5_VALUE_LEN
#else
#define MCU_OTA_CHECK_VALUE_LEN      MCU_OTA_CRC32_VALUE_LEN
#endif

/** 固件完整性校验类型，来自 OTA_BEGIN 的 check_type 字段。 */
typedef enum {
    MCU_OTA_CHECK_CRC32 = 1, /**< 使用 CRC32，check_value 有效长度为 4 字节。 */
    MCU_OTA_CHECK_MD5   = 2, /**< 使用 MD5，check_value 有效长度为 16 字节。 */
} mcu_ota_check_t;

/** MCU 返回给模组端的 OTA_ACK 状态码。 */
typedef enum {
    MCU_OTA_ACK_OK             = 0, /**< 命令处理成功。 */
    MCU_OTA_ACK_ERROR          = 1, /**< 通用错误。 */
    MCU_OTA_ACK_BUSY           = 2, /**< MCU 正忙，暂不接收新的 OTA。 */
    MCU_OTA_ACK_PARAM_ERROR    = 3, /**< payload 参数错误。 */
    MCU_OTA_ACK_SEQUENCE_ERROR = 4, /**< 分片 offset 或 seq 不符合期望。 */
    MCU_OTA_ACK_WRITE_ERROR    = 5, /**< 写入固件存储区失败。 */
    MCU_OTA_ACK_CHECK_ERROR    = 6, /**< OTA_END 后整包校验失败。 */
    MCU_OTA_ACK_UNSUPPORTED    = 7, /**< 不支持的校验类型或能力。 */
} mcu_ota_ack_code_t;

/** MCU OTA 接收状态。 */
typedef enum {
    MCU_OTA_STATE_IDLE = 0,  /**< 未进入 OTA 接收流程。 */
    MCU_OTA_STATE_READY,     /**< 已准备好接收 OTA_BEGIN。 */
    MCU_OTA_STATE_RECEIVING, /**< 正在接收 OTA_DATA 分片。 */
    MCU_OTA_STATE_DONE,      /**< OTA_END 校验通过，升级包接收完成。 */
    MCU_OTA_STATE_ABORTED,   /**< 模组端主动取消 OTA。 */
    MCU_OTA_STATE_FAILED,    /**< OTA 流程失败。 */
} mcu_ota_state_t;

/** OTA 任务信息和当前接收进度。 */
typedef struct {
    uint32_t image_size; /**< 固件总长度，来自 OTA_BEGIN total_size。 */
    uint32_t offset; /**< 下一个期望写入的固件偏移，也是 ACK/NOTIFY 的 next_offset。 */
    uint16_t seq; /**< 下一个期望接收的分片序号，也是 ACK/NOTIFY 的 next_seq。 */
    uint16_t chunk_size; /**< 模组端声明的 OTA_DATA 最大分片长度。 */
    uint8_t check_type; /**< 校验类型，见 mcu_ota_check_t。 */
    uint8_t flags; /**< 协议扩展标志，当前通常为 0。 */
    uint8_t check_value[MCU_OTA_CHECK_VALUE_LEN]; /**< 固件整包校验值，长度由启用的校验算法决定。 */
} mcu_ota_info_t;

/** MCU OTA 平台适配回调。除 send_cb 和 write_cb 外，其余回调可按产品需要留空。 */
typedef struct {
    /** 发送完整串口帧给模组端。用于发送 OTA_ACK 和 OTA_NOTIFY，必须提供。 */
    int (*send_cb)(const uint8_t *data, uint16_t len, void *user_data);
    /** 收到 OTA_ENTER 后调用。返回 0 表示允许进入 OTA，返回正数 ACK 状态码可直接回传。 */
    int (*enter_cb)(uint8_t flags, void *user_data);
    /** 收到 OTA_BEGIN 且参数合法后调用。通常在这里擦除或准备升级区。 */
    int (*begin_cb)(const mcu_ota_info_t *info, void *user_data);
    /** 写入一个 OTA_DATA 分片。必须提供。返回非 0 会回复 WRITE_ERROR 或指定 ACK 状态码。 */
    int (*write_cb)(uint32_t offset,
                    const uint8_t *data,
                    uint16_t len,
                    void *user_data);
    /** 内部 CRC32/MD5 校验通过后调用。通常在这里确认固件或切换启动分区。 */
    int (*finish_cb)(const mcu_ota_info_t *info, void *user_data);
    /** 收到 OTA_ABORT 后调用。可用于清理临时状态或擦除临时固件。 */
    void (*abort_cb)(void *user_data);
    /** 用户自定义指针，会原样传给所有回调。 */
    void *user_data;
} mcu_ota_config_t;

/**
 * 初始化 MCU OTA 组件。
 *
 * @param cfg 平台回调配置，可为空；为空时只初始化状态，后续无法发送 ACK。
 */
void mcu_ota_init(const mcu_ota_config_t *cfg);

/**
 * 喂入 1 个串口字节。
 *
 * 函数内部会自动同步帧头、缓存完整帧、校验帧并处理 OTA 命令。
 *
 * @param byte 串口收到的 1 字节数据。
 * @return MCU_OTA_OK 或 MCU_OTA_ERR_*。
 */
int mcu_ota_feed(uint8_t byte);

/**
 * 主动通知模组端 MCU 已准备好接收 OTA_BEGIN。
 *
 * 常用于 bootloader 启动并初始化串口后发送 OTA_NOTIFY READY。
 */
int mcu_ota_notify_ready(void);

/**
 * 主动通知模组端 MCU OTA 发生异常。
 *
 * @param error_code MCU 自定义错误码，会放入 OTA_NOTIFY error_code 字段。
 */
int mcu_ota_notify_error(uint32_t error_code);

/** 获取当前 OTA 状态。 */
mcu_ota_state_t mcu_ota_get_state(void);

/** 拷贝当前 OTA 信息和进度。info 为空时不做处理。 */
void mcu_ota_get_info(mcu_ota_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
