#include "serial_mcu_ota.h"
#include "serial_protocol.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *firmware; /**< 待发送固件数据指针。 */
    uint32_t firmware_size; /**< 待发送固件总长度。 */
    int finished; /**< 已完成标志。 */
    int failed; /**< 失败标志。 */
} serial_mcu_ota_example_ctx_t;

static serial_mcu_ota_example_ctx_t s_qumi_ota_example_ctx;

static uint16_t qumi_ota_example_check_value_len(serial_mcu_ota_check_t check_type)
{
    if (check_type == SERIAL_MCU_OTA_CHECK_DEFAULT ||
        check_type == SERIAL_MCU_OTA_CHECK_CRC32) {
        return SERIAL_MCU_OTA_CRC32_VALUE_LEN;
    }

    if (check_type == SERIAL_MCU_OTA_CHECK_MD5) {
        return SERIAL_MCU_OTA_MD5_VALUE_LEN;
    }

    return 0;
}

static int32_t qumi_ota_example_read_chunk(uint32_t offset, uint8_t *data, uint16_t len, void *user)
{
    serial_mcu_ota_example_ctx_t *ctx =
        (serial_mcu_ota_example_ctx_t *)user;

    if (!ctx || !ctx->firmware || !data ||
        offset > ctx->firmware_size ||
        (uint32_t)len > ctx->firmware_size - offset) {
        return -1;
    }

    memcpy(data, &ctx->firmware[offset], len);
    return 0;
}

static void qumi_ota_example_notify(serial_mcu_ota_event_t event, const serial_mcu_ota_info_t *info, void *user)
{
    serial_mcu_ota_example_ctx_t *ctx =
        (serial_mcu_ota_example_ctx_t *)user;

    if (!ctx || !info) {
        return;
    }

    switch (event) {
        case SERIAL_MCU_OTA_EVENT_ENTER:
            printf("QUMI OTA enter request\n");
            break;
        case SERIAL_MCU_OTA_EVENT_START:
            printf("QUMI OTA start total=%u\n", (unsigned int)info->total_size);
            break;
        case SERIAL_MCU_OTA_EVENT_PROGRESS:
            printf("QUMI OTA progress offset=%u/%u seq=%u\n",
                   (unsigned int)info->offset,
                   (unsigned int)info->total_size,
                   (unsigned int)info->seq);
            break;
        case SERIAL_MCU_OTA_EVENT_RETRY:
            printf("QUMI OTA retry cmd=0x%02X retry=%u\n",
                   info->last_cmd,
                   info->retry_count);
            break;
        case SERIAL_MCU_OTA_EVENT_FINISH:
            ctx->finished = 1;
            printf("QUMI OTA finish\n");
            break;
        case SERIAL_MCU_OTA_EVENT_ABORT:
            ctx->failed = 1;
            printf("QUMI OTA abort reason=%d\n", (int)info->error_code);
            break;
        case SERIAL_MCU_OTA_EVENT_ERROR:
            ctx->failed = 1;
            printf("QUMI OTA error code=%d ack=%u\n",
                   (int)info->error_code,
                   info->last_ack_code);
            break;
        case SERIAL_MCU_OTA_EVENT_MCU_NOTIFY:
            printf("QUMI OTA mcu notify reason=%u next_seq=%u next_offset=%u error=%u\n",
                   (unsigned int)info->notify_reason,
                   (unsigned int)info->notify_next_seq,
                   (unsigned int)info->notify_next_offset,
                   (unsigned int)info->notify_error_code);
            if (info->notify_reason == SERIAL_MCU_OTA_NOTIFY_REASON_ERROR) {
                ctx->failed = 1;
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 启动 Qumi 主控侧 MCU OTA 示例。
 *
 * 调用前需要确保 Qumi 串口组件已经完成初始化。该函数会通过 Qumi 串口命令表发送 OTA_ENTER。
 *
 * @param firmware 待发送固件数据。
 * @param firmware_size 待发送固件总长度。
 * @param check_type 固件整包校验类型。
 * @param check_value 固件整包校验值，CRC32 传 4 字节，MD5 传 16 字节。
 * @return SERIAL_MCU_OTA_OK 表示启动成功，否则返回错误码。
 */
static int serial_mcu_ota_example_start_internal(const uint8_t *firmware,
                                                 uint32_t firmware_size,
                                                 serial_mcu_ota_check_t check_type,
                                                 const uint8_t *check_value)
{
    serial_mcu_ota_example_ctx_t *ctx = &s_qumi_ota_example_ctx;
    serial_mcu_ota_config_t config;
    uint16_t check_value_len;

    if (!firmware || !check_value || firmware_size == 0) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    check_value_len = qumi_ota_example_check_value_len(check_type);
    if (check_value_len == 0) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->firmware = firmware;
    ctx->firmware_size = firmware_size;

    memset(&config, 0, sizeof(config));
    config.total_size = firmware_size;
    config.check_type = check_type;
    memcpy(config.check_value, check_value, check_value_len);
    config.read_chunk = qumi_ota_example_read_chunk;
    config.notify = qumi_ota_example_notify;
    config.user = ctx;

    return qm_serial_mcu_ota_start(&config);
}

int serial_mcu_ota_example_start(const uint8_t *firmware,
                                 uint32_t firmware_size,
                                 serial_mcu_ota_check_t check_type,
                                 const uint8_t *check_value)
{
    return serial_mcu_ota_example_start_internal(firmware,
                                                 firmware_size,
                                                 check_type,
                                                 check_value);
}

/**
 * @brief 输入 MCU 返回的 OTA_ACK 或 OTA_NOTIFY payload。
 *
 * 正常接入 `xjiang_serial` 时不需要业务侧主动调用本函数，`g_qm_serial_cmd_table`
 * 会自动分发 `0x3A` / `0x3B` 到组件。
 *
 * @param cmd OTA 命令。
 * @param data OTA payload。
 * @param len OTA payload 长度。
 * @return SERIAL_MCU_OTA_OK 表示处理成功，否则返回错误码。
 */
int serial_mcu_ota_example_on_ack(uint8_t cmd, uint8_t *data, uint16_t len)
{
    return serial_mcu_ota_parse(cmd, data, len);
}

/**
 * @brief 周期处理 ACK 超时重发。
 *
 * 建议在主循环或 Qumi 任务中周期调用。
 *
 * @return SERIAL_MCU_OTA_OK 表示处理成功，否则返回错误码。
 */
int serial_mcu_ota_example_process(void)
{
    return serial_mcu_ota_process();
}

/**
 * @brief 主动取消 Qumi 主控侧 MCU OTA 示例。
 *
 * @param reason 取消原因。
 * @return SERIAL_MCU_OTA_OK 表示取消命令发送成功，否则返回错误码。
 */
int serial_mcu_ota_example_abort(int32_t reason)
{
    return qm_serial_mcu_ota_abort(reason);
}

/**
 * @brief 查询示例是否已经完成。
 *
 * @return 1 表示完成，0 表示未完成或失败。
 */
int serial_mcu_ota_example_is_done(void)
{
    return s_qumi_ota_example_ctx.finished && !s_qumi_ota_example_ctx.failed;
}
