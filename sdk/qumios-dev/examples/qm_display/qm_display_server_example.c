#include "qm_display_server.h"
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

qm_display_server_handle_t qm_display_server_example_create(
    qm_uart_dev_t *uart, uint8_t firmware_major, uint8_t firmware_minor,
    uint8_t firmware_patch)
{
    qm_display_server_config_t config = {0};

    if (uart == NULL) {
        return NULL;
    }
    config.write = uart_write;
    config.read = uart_read;
    config.transport_user = uart;
    config.firmware_version.major = firmware_major;
    config.firmware_version.minor = firmware_minor;
    config.firmware_version.patch = firmware_patch;
    config.firmware_state = QM_DISPLAY_SERVER_FW_APPLICATION;

    /* Add product asset and display callbacks before enabling those commands. */
    return qm_display_server_create(&config);
}

qm_err_t qm_display_server_example_process(qm_display_server_handle_t server)
{
    return qm_display_server_process(server, 1000U);
}
