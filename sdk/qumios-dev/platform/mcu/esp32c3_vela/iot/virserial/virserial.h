#ifndef _VIRTUAL_SERIAL_H_
#define _VIRTUAL_SERIAL_H_

#include "qm_types.h"
#include "qm_kernel.h"

#define RECV_BUFF_LEN   64

#define APP_QUEUE_NUM_MAX   100
#define APP_QUEUE_SIZE_MAX  512

typedef enum{
    VIRSERIAL_SEND,
    VIRSERIAL_REPORT,
    VIRSERIAL_GET_MAC,
    VIRSERIAL_GET_WIFI,
    VIRSERIAL_GET_RSSI,
    VIRSERIAL_GET_TIMESTAMP,
}virserial_cmd_t;   

typedef int (*virserial_recv_t)(void *data, int err_code);

int virserial_properties_changed(char *buff, uint32_t len);

int virserial_timestamp_timer_start(void);
uint32_t virserial_fast_get_timestamp(void); // use it after virserial_timestamp_timer_start()

int virserial_fast_get_mac(uint8_t mac[6]);
int virserial_fast_default_rssi(void);
int virserial_fast_get_rssi(void);

int virserial_send_reboot(void);
int virserial_send_net_config(void);
int virserial_send_miio_factory_mode(void);
int virserial_send_set_wifi(char *ssid, char *passwd);

int virserial_get_timestamp_sync(void);
int virserial_get_mac_sync(uint8_t mac[6]);
int virserial_send_get_mac(virserial_recv_t get_cb);
int virserial_send_get_wifi(virserial_recv_t get_cb);
int virserial_send_get_rssi(virserial_recv_t get_cb);

int virserial_cmd_init(void);
int virserial_cmd_loop(void *handle, uint32_t timeout);

int virserial_cmd_send(virserial_cmd_t cmd, char *buff, uint32_t len, virserial_recv_t app_recv_cb);

#endif
