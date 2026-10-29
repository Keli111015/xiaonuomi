#include "mcu_ota.h"

#include <string.h>

#define MCU_OTA_EXAMPLE_MAX_IMAGE_SIZE (256U * 1024U)

/* 示例只保留平台必须适配的几个点：串口发送、擦除、写入、确认固件。 */
static int example_uart_send(const uint8_t *data, uint16_t len, void *user_data)
{
    (void)data;
    (void)len;
    (void)user_data;

    /*
     * TODO:
     * 调用 MCU 平台串口发送接口，把 data[0..len-1] 发给模组端。
     * 发送成功返回 0，失败返回非 0。
     */
    return 0;
}

static int example_enter(uint8_t flags, void *user_data)
{
    (void)flags;
    (void)user_data;

    /*
     * TODO:
     * 如果 App 收到 OTA_ENTER 后需要重启到 bootloader，可在这里设置标志并重启。
     * 如果当前固件已经是 bootloader/OTA 模式，直接返回 0 即可。
     */
    return 0;
}

static int example_begin(const mcu_ota_info_t *info, void *user_data)
{
    (void)user_data;

    /* 这里可以按产品实际 flash 分区大小调整最大固件限制。 */
    if (!info ||
        info->image_size == 0 ||
        info->image_size > MCU_OTA_EXAMPLE_MAX_IMAGE_SIZE) {
        return MCU_OTA_ACK_PARAM_ERROR;
    }

    /*
     * TODO:
     * 擦除或准备升级存储区域，大小为 info->image_size。
     * 例如：flash_erase(OTA_ADDR, info->image_size)。
     */
    return 0;
}

static int example_write(uint32_t offset,
                         const uint8_t *data,
                         uint16_t len,
                         void *user_data)
{
    (void)offset;
    (void)len;
    (void)user_data;

    if (!data) {
        return MCU_OTA_ACK_WRITE_ERROR;
    }

    /*
     * TODO:
     * 把 data[0..len-1] 写入升级区 offset 位置。
     * 例如：flash_write(OTA_ADDR + offset, data, len)。
     */
    return 0;
}

static int example_finish(const mcu_ota_info_t *info, void *user_data)
{
    (void)info;
    (void)user_data;

    /*
     * TODO:
     * 到这里时组件内部已经完成 CRC32/MD5 校验。
     * 在这里确认固件、设置启动分区或记录升级成功标志。
     * 例如：set_boot_partition(OTA_PARTITION)。
     */
    return 0;
}

static void example_abort(void *user_data)
{
    (void)user_data;

    /*
     * TODO:
     * 按产品策略清理临时升级区。
     */
}

void mcu_ota_example_init(void)
{
    mcu_ota_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));

    cfg.send_cb = example_uart_send;
    cfg.enter_cb = example_enter;
    cfg.begin_cb = example_begin;
    cfg.write_cb = example_write;
    cfg.finish_cb = example_finish;
    cfg.abort_cb = example_abort;

    mcu_ota_init(&cfg);
}

void mcu_ota_example_uart_rx_byte(uint8_t byte)
{
    (void)mcu_ota_feed(byte);
}

void mcu_ota_example_notify_ready(void)
{
    (void)mcu_ota_notify_ready();
}
