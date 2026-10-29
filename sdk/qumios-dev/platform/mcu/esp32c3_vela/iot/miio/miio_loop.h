#ifndef __MIIO_LOOP_H_
#define __MIIO_LOOP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/* buffer used to receive command string from wifi module, max 900byte*/
#define CMD_STR_MAX_LEN                 (512)
/* max method name register with miio_cmd_method_register() function */
#define CMD_METHOD_LEN_MAX              (20)
/* buffer used to response command execute result */
#define RESULT_BUF_SIZE                 (CMD_STR_MAX_LEN / 2)
#define USER_POLL_INTERVAL_MS           (100)
#define USER_UART_TIMEOUT_MS            (1000)
#define MIIO_BEACON_WAIT_TIMEOUT        (500)
#define MIIO_BEACON_LOOP_WAIT_TIMEOUT   (100)
#define BLE_SCAN_ENABLE                 (1)

typedef void (*miio_loop_handle_t)(void);
typedef void (*miio_beacon_handle_t)(uint8_t *data, uint32_t len);

void *miio_command_handle_init(void);
void *miio_command_channel_get(void);

int miio_pid_model_set(int pid, char *model);
int miio_beacon_handler_reg(miio_beacon_handle_t handler);
int miio_loop_handler_reg(miio_loop_handle_t handler);
int miio_command_handle_loop(void *miio_handle);

int miio_uart_write(void *handle, const void *data, uint32_t size, uint32_t timeout);
int miio_uart_read(void *handle, void *data, uint32_t expect_size, uint32_t timeout);
int miio_uart_send_str_wait_ack(void *handle, const char* str, uint32_t time_out);

#ifdef __cplusplus
}
#endif

#endif