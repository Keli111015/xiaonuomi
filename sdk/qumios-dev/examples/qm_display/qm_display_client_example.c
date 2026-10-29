#include "qm_display_client.h"
#include "qm_uart.h"

static int32_t uart_write(void *user, const uint8_t *data,
                          uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_write((qm_uart_dev_t *)user, data, length, timeout_ms);
}

static int32_t uart_read(void *user, uint8_t *data,
                         uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_read((qm_uart_dev_t *)user, data, length, timeout_ms);
}

qm_err_t qm_display_client_example(qm_uart_dev_t *uart)
{
    qm_display_client_config_t config = {0};
    qm_display_client_handle_t client = NULL;
    qm_display_fw_info_t firmware = {0};
    qm_display_result_t result = {0};
    qm_err_t ret;

    if (uart == NULL) {
        return -QM_EINVAL;
    }

    config.write = uart_write;
    config.read = uart_read;
    config.user = uart;
    client = qm_display_client_create(&config);
    if (client == NULL) {
        return -QM_ENOMEM;
    }

    ret = qm_display_client_get_fw_version(client, &firmware, &result);
    if (ret == QM_EOK && result.status != QM_DISPLAY_STATUS_OK) {
        ret = -QM_EIO;
    }

    qm_display_client_destroy(client);
    return ret;
}
