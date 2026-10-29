#include "ymodem.h"

#include "qm_ota.h"
#include "qm_uart.h"

static int32_t example_write(void *user, const uint8_t *data,
                             uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_write((qm_uart_dev_t *)user, data, length, timeout_ms);
}

static int32_t example_read(void *user, uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_read((qm_uart_dev_t *)user, data, length, timeout_ms);
}

static qm_err_t example_storage_begin(const ymodem_file_info_t *file,
                                      void *user)
{
    (void)file;
    (void)user;
    return (qm_err_t)qm_ota_start(NULL);
}

static qm_err_t example_storage_write(uint32_t offset, const uint8_t *data,
                                      uint32_t length, void *user)
{
    uint32_t ota_offset = offset;

    (void)user;
    return (qm_err_t)qm_ota_write(&ota_offset, (uint8_t *)data, length);
}

static qm_err_t example_storage_finish(const ymodem_file_info_t *file,
                                       void *user)
{
    (void)file;
    (void)user;
    return (qm_err_t)qm_ota_end(NULL);
}

static void example_storage_abort(void *user)
{
    (void)user;
    (void)qm_ota_abort(NULL);
}

/**
 * @brief Receive a PC-sent image and commit it to the QUMI OTA partition.
 * @brief 接收 PC 发送的镜像并提交到 QUMI OTA 分区。
 *
 * The caller must configure an exclusive UART, stop application traffic and
 * logs on that UART, and enter the upgrade mode before calling this function.
 * The function clears stale RX bytes. It does not reboot after a successful
 * OTA; the caller decides when to call qm_reboot().
 *
 * 调用方必须先配置独占 UART，停止该 UART 上的业务流量和日志，并进入
 * 升级模式。本函数会清理残留 RX，不会在 OTA 成功后自动重启；重启时机
 * 由调用方决定。
 */
qm_err_t ymodem_example_receive_to_qm_ota(qm_uart_dev_t *uart)
{
    ymodem_config_t config = {0};
    ymodem_handle_t receiver = NULL;
    qm_err_t ret = QM_EOK;

    if (uart == NULL) {
        return -QM_EINVAL;
    }
    ret = (qm_err_t)qm_uart_ctrl(uart, QM_UART_CTRL_CMD_RX_CLEAR, NULL);
    if (ret != QM_EOK) {
        return ret;
    }
    config.write = example_write;
    config.read = example_read;
    config.transport_user = uart;
    config.storage.begin = example_storage_begin;
    config.storage.write = example_storage_write;
    config.storage.finish = example_storage_finish;
    config.storage.abort = example_storage_abort;
    receiver = ymodem_create(&config);
    if (receiver == NULL) {
        return -QM_ENOMEM;
    }
    ret = ymodem_receive(receiver);
    ymodem_destroy(receiver);
    return ret;
}
