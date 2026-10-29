#include "qm.h"
#include "qm_log.h"

#include "qm_uart.h"
#include "util.h"
#include "miio_loop.h"
#include "virserial.h"
#include "func_api.h"

#include "qm_utils_string.h"

#define TIMESTAMP_2000  943891200  /* 1970~2000 in seconds */
typedef int (*virserial_callback)(virserial_recv_t app_recv_cb, void *data, int err_code);

typedef struct{
    uint16_t                len;
    uint16_t                cmd;
    char                  *data;
    virserial_recv_t       app_recv_cb;
}virserial_info_t;

typedef struct{
    virserial_cmd_t         cmd;
    virserial_callback      recv_cb;
}virserial_get_cb_t;


static uint32_t fast_timestamp = 0;
static qm_timer_t time_index = {0};
static qm_queue_t virserial_queue = {0};

typedef struct {
    uint8_t is_init;
    uint8_t timestamp_is_update;
    int8_t fast_rssi;
    uint8_t  fast_mac[6];
}virserial_ctx_t;

static virserial_ctx_t virserial_ctx = {0};

static int32_t get_timestamp(void *data, int err_code);
static int virserial_get_timestamp(virserial_recv_t app_recv_cb, void *data, int err_code);
static int virserial_get_rssi(virserial_recv_t app_recv_cb, void *data, int err_code);
static int32_t virserial_get_mac(virserial_recv_t app_recv_cb, void *data, int err_code);
static int32_t virserial_get_wifi(virserial_recv_t app_recv_cb, void *data, int err_code);

static virserial_get_cb_t get_cb_info[] = 
{
    {VIRSERIAL_SEND         ,   NULL},
    {VIRSERIAL_REPORT       ,   NULL},
    {VIRSERIAL_GET_MAC      ,   virserial_get_mac},
    {VIRSERIAL_GET_WIFI     ,   virserial_get_wifi},
    {VIRSERIAL_GET_RSSI     ,   virserial_get_rssi},
    {VIRSERIAL_GET_TIMESTAMP,   virserial_get_timestamp},
};


static void time_update_cb( qm_timer_t *timer, void *arg)
{
    fast_timestamp++;
}

int virserial_cmd_init(void)
{
    int ret = QM_EOK;

    virserial_ctx.fast_rssi = -127;
    virserial_ctx.is_init = QM_TRUE;
    
    ret = qm_timer_new(&time_index, time_update_cb, NULL, 1000, 1);
    if(ret != QM_EOK){
       return ret;
    }
    ret = qm_queue_new(&virserial_queue, APP_QUEUE_NUM_MAX, APP_QUEUE_SIZE_MAX);
    if(ret != QM_EOK){
        qm_timer_free(&time_index);
        return ret;
    }    
    return ret;
}

int virserial_cmd_send(virserial_cmd_t cmd, char *buff, uint32_t len, virserial_recv_t app_recv_cb)
{
   if(buff == NULL || len == 0){
        return -QM_EINVAL; 
    }

    virserial_info_t queue = {0};
    char *data = (char *)qm_malloc(len + 1);
    if(data == NULL){
        return -QM_ENOMEM;
    }
    memset(data, 0, len + 1);
    memcpy(data, buff, len);

    queue.len  = len;
    queue.cmd =  cmd;
    queue.data = data;
    queue.app_recv_cb = app_recv_cb;

    int ret = qm_queue_send(&virserial_queue, (void *)&queue, sizeof(virserial_info_t));
    if(ret != QM_EOK){
        qm_free(data);
        data = NULL;
    }
    return ret;
}

int  virserial_cmd_loop(void *handle, uint32_t timeout)
{
    if(handle == NULL){
        return -QM_EINVAL;
    }
    
    int count = 1;
    int ret = QM_EOK;
    uint32_t len = 0;
    virserial_info_t serial_msg = {0};

    ret = qm_queue_recv(&virserial_queue, &serial_msg, &len, timeout);
    if(len == 0 || ret != QM_EOK){
        goto _exit;
    }

    if(serial_msg.cmd == VIRSERIAL_REPORT){
        
    } 

    do{
        char ack_buff[64] = {0};
        miio_uart_write(handle, serial_msg.data, serial_msg.len, 0);
        virserial_callback recv_cb = get_cb_info[serial_msg.cmd].recv_cb;

        ret = miio_uart_read(handle, ack_buff, 64, 1000);
        if(ret <= 0 || !strncmp(ack_buff, "error", strlen("error"))){
            count++;
            continue;
        }

        if(recv_cb){
            ret = recv_cb(serial_msg.app_recv_cb, (void *)ack_buff, QM_EOK);
        }else{
            ret = QM_EOK;
        }
    }while(ret != QM_EOK && count++ < 3);

_exit:
    if(serial_msg.data){
        qm_free(serial_msg.data);
        serial_msg.data = NULL;
    }
    
    if(!virserial_ctx.is_init && (fast_timestamp % (5 * 60) == 0)){
        if(!virserial_ctx.timestamp_is_update){
            qm_timer_stop(&time_index);
            virserial_get_timestamp_sync();
            qm_timer_start(&time_index);
        }
    }else{
        virserial_ctx.timestamp_is_update = QM_TRUE;
    }

    return ret;
}

int virserial_properties_changed(char *buff, uint32_t len)
{
    str_n_cat(buff, 1, "\r");
    return virserial_cmd_send(VIRSERIAL_SEND, buff, strlen(buff), NULL);
}

uint32_t virserial_fast_get_timestamp(void)
{
    return fast_timestamp;
}

static int virserial_get_timestamp(virserial_recv_t app_recv_cb, void *data, int err_code)
{
    if(app_recv_cb == NULL || data == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    uint32_t value = atou64n(data, strlen(data));
    if(app_recv_cb){
        ret = app_recv_cb((void *)&value, err_code);
    }
    return ret;
}

static int32_t get_timestamp(void *data, int err_code)
{
    if(data == NULL){
        return -QM_EINVAL;
    }
    uint32_t *timestamp = (uint32_t *)data;
    fast_timestamp = *timestamp;
    if(fast_timestamp < TIMESTAMP_2000){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int virserial_timestamp_timer_start(void)
{
    if(virserial_ctx.is_init){
        virserial_ctx.is_init = 0;
        qm_timer_start(&time_index);
    }

    return virserial_cmd_send(VIRSERIAL_GET_TIMESTAMP, "time posix\r", strlen("time posix\r"), get_timestamp);
}

int virserial_fast_get_mac(uint8_t mac[6])
{
    memcpy(mac, virserial_ctx.fast_mac, 6);
    return QM_EOK;
}

int virserial_fast_get_rssi(void)
{
    return virserial_ctx.fast_rssi;
}

int virserial_fast_default_rssi(void)
{
    virserial_ctx.fast_rssi = -127;
    return QM_EOK;
}

int virserial_send_reboot(void)
{
    return virserial_cmd_send(VIRSERIAL_SEND, "reboot\r", strlen("reboot\r"), NULL);
}

int virserial_send_net_config(void)
{
    return virserial_cmd_send(VIRSERIAL_SEND, "restore\r", strlen("restore\r"), NULL);
}

int virserial_send_miio_factory_mode(void)
{
    return virserial_cmd_send(VIRSERIAL_SEND, "factory\r", strlen("factory\r"), NULL);
}

int virserial_send_set_wifi(char *ssid, char *passwd)
{
    char buff[64] = {0};
    snprintf(buff, 64, "setwifi %s %s\r", ssid, passwd);
    return virserial_cmd_send(VIRSERIAL_SEND, buff, strlen(buff), NULL);
}

int virserial_send_get_mac(virserial_recv_t get_cb)
{
    return virserial_cmd_send(VIRSERIAL_GET_MAC, "mac\r", strlen("mac\r"), get_cb);

}

static int32_t virserial_get_mac(virserial_recv_t app_recv_cb, void *data, int err_code)
{
    if(data == NULL){
        return -QM_EINVAL;
    }


    hex_str_to_nums(data, strlen(data), virserial_ctx.fast_mac);

    int ret = QM_EOK;
    if(app_recv_cb){
        ret = app_recv_cb((void *)&virserial_ctx.fast_mac, err_code);
    }
    return ret;    
}

int virserial_send_get_wifi(virserial_recv_t get_cb)
{
    return virserial_cmd_send(VIRSERIAL_GET_WIFI, "getwifi\r", strlen("getwifi\r"), get_cb);

}

static int32_t virserial_get_wifi(virserial_recv_t app_recv_cb, void *data, int err_code)
{
    if(data == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
    if(app_recv_cb){
        ret = app_recv_cb(data , err_code);
    }
    return ret;    
}

int virserial_send_get_rssi(virserial_recv_t get_cb)
{
    return virserial_cmd_send(VIRSERIAL_GET_RSSI, "getwifi\r", strlen("getwifi\r"), get_cb);
}

static int virserial_get_rssi(virserial_recv_t app_recv_cb, void *data, int err_code)
{
    if(data == NULL){
        return -QM_EINVAL;
    }
    char temp[32] = {0};
    memset(temp, 0, sizeof(temp)); 
    get_last_str(data, temp, 0x20);

    uint16_t temp_len = strlen(temp);
    memset(temp, 0, sizeof(temp));
    get_last_str(data + temp_len + 1,temp, 0x20);
    virserial_ctx.fast_rssi = atoi(temp);
    int ret = QM_EOK;

    if(app_recv_cb){
        ret = app_recv_cb((void *)&virserial_ctx.fast_rssi, err_code);
    }
    
    return ret;
}

int virserial_get_timestamp_sync(void)
{
    int count = 0;
    int ret_len = 0;
    uint32_t value = 0;
    uint8_t ack_buf[64] = {0};

    void *channel = miio_command_channel_get();
    if(channel == NULL){
        return -QM_EINVAL;
    }

	ret_len = miio_uart_write(channel, "time posix\r", strlen("time posix\r"), 0);
	if(ret_len < 0){
		return -QM_ERROR;
	}

    do{
        if(count++ > 2){
            break;
        }
        ret_len =  miio_uart_read(channel, ack_buf, sizeof(ack_buf), 1000);
    }while (ret_len <= 0);
    
    value = atou64n((const char *)ack_buf, strlen((const char *)ack_buf));
    if(value > TIMESTAMP_2000){
        fast_timestamp = value;
    }

    return QM_EOK;
}

int virserial_get_mac_sync(uint8_t mac[6])
{
    int count = 0;
    int ret_len = 0;
    uint8_t ack_buf[64] = {0};

    void *channel = miio_command_channel_get();
    if(channel == NULL){
        return -QM_EINVAL;
    }

	ret_len = miio_uart_write(channel, "mac\r", strlen("mac\r"), 0);
	if(ret_len < 0){
		return -QM_ERROR;
	}

    do{
        if(count++ > 2){
            break;
        }
        ret_len = miio_uart_read(channel, ack_buf, sizeof(ack_buf), 1000);
    }while (ret_len <= 0 || !strncmp((const char *)ack_buf, "error", strlen("error")));
    
    hex_str_to_nums((char *)ack_buf, strlen((char *)ack_buf), mac);
 
    return QM_EOK;
}