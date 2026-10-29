#ifndef GX_TEST_UART_H
#define GX_TEST_UART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_PIN_NO_CHANGE (-1)
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl;
} uart_config_t;
int uart_param_config(int port, const uart_config_t *config);
int uart_set_pin(int port, int tx, int rx, int rts, int cts);
int uart_driver_install(int port, int rx, int tx, int queue, void *handle, int flags);
bool uart_is_driver_installed(int port);
int uart_tx_chars(int port, const char *data, uint32_t size);
#endif
