
#include "qm_log.h"
#include "qm.h"

#include "virserial.h"
#include "miio_loop.h"
#include "miio_command.h"

#include <poll.h>
#include <unistd.h>
#include "fcntl.h"
#include "mqueue.h"
#include "qm_utils_timer.h"

#if CONFIG_QM_WORKQUEUE_NO_TASK
#include "qm_workqueue.h"
#endif

// #define LOG_TAG "miio"
typedef enum
{
	MIIO_LOOP_CMD_LOG_LEVEL 	=   0,
	MIIO_LOOP_CMD_LOG_ECHO 		=   1,
	MIIO_LOOP_CMD_VERSION  		=   2,
	MIIO_LOOP_CMD_AUTO_OTA 		=   3,
	MIIO_LOOP_CMD_MODEL_SET 	=   4,
	MIIO_LOOP_CMD_PID_SET  		=   5,
#if BLE_SCAN_ENABLE
	MIIO_LOOP_CMD_BLE_SCAN 		=   6,
	MIIO_LOOP_CMD_BLE_SCAN_RPC  =   7,
#endif

	MIIO_LOOP_CMD_MAX,
}miio_loop_init_status_t;

typedef struct 
{
	int fd;
}miio_uart_handle_t;


typedef struct 
{
	int pid;
	char model[32 + 1];

	int mq_id;
	uint8_t miio_init;
	char pbuf[CMD_STR_MAX_LEN];
	char method[CMD_METHOD_LEN_MAX];
	miio_loop_handle_t g_loop_handler;
	miio_beacon_handle_t g_beacon_handler;
	qm_utils_time_t wait_timer;
	qm_utils_time_t ble_scan_wait_timer;
}miio_loop_ctx_t;

static miio_loop_ctx_t loop_ctx = {0};

#if !defined(CONFIG_MIIO_VERSION) 
#error "please use qdf.py menuconfig"
#endif

miio_uart_handle_t miio_uart = {0};

static int miio_uart_init(void)
{
	int fd = open("/dev/ttyp0", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		return -QM_ERROR;
	}
	return fd;
}

int miio_uart_write(void *handle, const void *data, uint32_t size, uint32_t timeout)
{
    int ret = QM_EOK;
	miio_uart_handle_t *uart = (miio_uart_handle_t *)handle;
    if(handle == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    struct pollfd fdp = {
        .fd = uart->fd,
        .events = POLLOUT,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLOUT) != 0) {
            ret = write(uart->fd, data, size);
        }
    }
	return ret;
}

int miio_uart_read(void *handle, void *data, uint32_t expect_size, uint32_t timeout)
{
    int ret = QM_EOK;
   	miio_uart_handle_t *uart = (miio_uart_handle_t *)handle;
    if(handle == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    struct pollfd fdp = {
        .fd = uart->fd,
        .events = POLLIN,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLIN) != 0) {
            ret = read(uart->fd, data, expect_size);
        }
    }
    return (ret < 0 ? 0 : ret);
}


int miio_uart_send_str_wait_ack(void *handle, const char* str, uint32_t time_out)

{
	int ret = QM_EOK;
	uint8_t ack_buf[10] = {0};
	int len = strlen(str);
	if (len <= 0 || handle == NULL) {
		return -QM_EINVAL; 
	}
	miio_uart_handle_t *miio_uart = (miio_uart_handle_t *)handle;
	
	ret = miio_uart_write(miio_uart, str, len, 0);
	if(ret < 0){
		return -QM_ERROR;
	}
	ret = miio_uart_read(miio_uart, ack_buf, sizeof(ack_buf), time_out);
	if(ret <= 0){
		return -QM_ERROR;
	}

	if (0 != strncmp((const char*)ack_buf, "ok", strlen("ok"))) {
		return -QM_ERROR;
	}

	return QM_EOK;
}

static int miio_command_cmd_init(char *data, uint32_t timeout, int init_count)
{
	int count = 0;
	int result = QM_EOK;
	do{
		result = miio_uart_send_str_wait_ack((void *)&miio_uart, data, timeout);
	}while (result != QM_EOK && count++ < init_count);
	return result;
}

void *miio_command_handle_init(void)
{
	void *miio_handle = NULL;

	if(!loop_ctx.miio_init){
		loop_ctx.miio_init = 1;
    	miio_uart.fd = miio_uart_init();
	}

	memset(loop_ctx.pbuf, 0, CMD_STR_MAX_LEN);
	miio_handle = miio_cmd_init((void *)&miio_uart);
	if(miio_handle == NULL){
		return NULL;
	}


	return miio_handle;
}

void *miio_command_channel_get(void)
{
	if(!loop_ctx.miio_init){
		loop_ctx.miio_init = 1;
    	miio_uart.fd = miio_uart_init();
	}

	return (void *)&miio_uart;
}

int miio_pid_model_set(int pid, char *model)
{

	loop_ctx.pid = pid;

	memset(loop_ctx.model, 0, 32 + 1);
	memcpy(loop_ctx.model, model, strlen(model));

	return QM_EOK;
}

static int miio_beacon_loop(qm_utils_time_t *time_out)
{
	int recv_len = 0;
	int count_max = 10;
	if(time_out == NULL){
		return -QM_EINVAL;
	}

	if(loop_ctx.g_beacon_handler == NULL){
		return -QM_EINVAL;
	}

	memset(loop_ctx.pbuf, 0, CMD_STR_MAX_LEN);
	
	if(loop_ctx.mq_id < 0){
		if (!qm_utils_time_is_expired(time_out)) {
			return -QM_EINVAL;
		}
		qm_utils_time_countdown_ms(time_out, MIIO_BEACON_LOOP_WAIT_TIMEOUT);
		loop_ctx.mq_id = mq_open("/beacon_mq", O_RDONLY | O_NONBLOCK);
		if(loop_ctx.mq_id < 0){
			return -QM_EINVAL;
		}
	}

	do{
		recv_len = mq_receive(loop_ctx.mq_id, loop_ctx.pbuf, 100, NULL);
		if (recv_len <= 0){
			continue;
		 }
		 loop_ctx.g_beacon_handler((uint8_t*)loop_ctx.pbuf, recv_len);
		 memset(loop_ctx.pbuf, 0, recv_len);
	}while(count_max-- > 0);	
	return QM_EOK;
}

//初始化相关命令，若失败则下次循环进行重试，防止长时间阻塞
static void miio_cmd_init_retry(void)
{
	int ret = -QM_EINVAL;
	char temp[50] = {0};
	static int complete_cnt = 0;

	if(complete_cnt == MIIO_LOOP_CMD_MAX){
		return ;
	}
	
	do{
		ret = -QM_EINVAL;
		switch (complete_cnt)
		{
			//log等级
			case MIIO_LOOP_CMD_LOG_LEVEL:
				ret = miio_command_cmd_init("set_log_level 0\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_LOG_ECHO;
				}
			break;
			
			//
			case MIIO_LOOP_CMD_LOG_ECHO:
				ret = miio_command_cmd_init("echo off\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_VERSION;
				}
			break;

			//固件版本
			case MIIO_LOOP_CMD_VERSION:
				ret = miio_command_cmd_init("mcu_version "CONFIG_MIIO_VERSION"\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_AUTO_OTA;
				}
			break;

			//静默OTA支持
			case MIIO_LOOP_CMD_AUTO_OTA:
				ret = miio_command_cmd_init("set_mcu_auto_ota on\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_MODEL_SET;
				}
			break;

			//米家model
			case MIIO_LOOP_CMD_MODEL_SET:
				memset(temp, 0, sizeof(temp));
				snprintf(temp, 50, "model %s\r", loop_ctx.model);
				ret =miio_command_cmd_init(temp, 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_PID_SET;
				}
			break;

			//米家pid
			case MIIO_LOOP_CMD_PID_SET:
				memset(temp, 0, sizeof(temp));
				snprintf(temp, 50, "ble_config set %d %s\r", loop_ctx.pid, CONFIG_MIIO_VERSION);
				ret = miio_command_cmd_init(temp, 1000, 3);
				if(ret == QM_EOK){
				#if BLE_SCAN_ENABLE
					complete_cnt = MIIO_LOOP_CMD_BLE_SCAN;
				#else
					complete_cnt = MIIO_LOOP_CMD_MAX;
				#endif
				}
			break;
			
			//BLE扫描
		#if BLE_SCAN_ENABLE
			case MIIO_LOOP_CMD_BLE_SCAN:
				ret = miio_command_cmd_init("ble_scan on\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_BLE_SCAN_RPC;
				}
			break;
			
			case MIIO_LOOP_CMD_BLE_SCAN_RPC:
				ret = miio_command_cmd_init("ble_rctype set 1\r", 1000, 3);
				if(ret == QM_EOK){
					complete_cnt = MIIO_LOOP_CMD_MAX;
				}
			break;
		#endif
			default:
				break;
		}
	
	}while (ret == QM_EOK && complete_cnt < MIIO_LOOP_CMD_MAX);
}

int miio_command_handle_loop(void *miio_handle)
{
	if(miio_handle == NULL){
		return -QM_EINVAL;
	}

	int recv_bytes = 0;
    int result = QM_EOK;
	uint32_t method_len = 0;

	memset(loop_ctx.pbuf, 0, CMD_STR_MAX_LEN);

	virserial_send_get_mac(NULL);
	qm_utils_time_init(&loop_ctx.wait_timer);
	qm_utils_time_init(&loop_ctx.ble_scan_wait_timer);
	qm_utils_time_countdown_ms(&loop_ctx.wait_timer, USER_POLL_INTERVAL_MS);
	qm_utils_time_countdown_ms(&loop_ctx.ble_scan_wait_timer, MIIO_BEACON_LOOP_WAIT_TIMEOUT);
	while(1){

		miio_cmd_init_retry();
		
#if CONFIG_QM_WORKQUEUE_NO_TASK
		qm_workqueue_process();
#endif
		virserial_cmd_loop((void *)&miio_uart, 10);
		
		miio_beacon_loop(&loop_ctx.ble_scan_wait_timer);
	    if (!qm_utils_time_is_expired(&loop_ctx.wait_timer)) {
			continue;
		}
		
		if(loop_ctx.g_loop_handler){
			loop_ctx.g_loop_handler();
		}

		method_len = 0;
		memset(loop_ctx.pbuf, 0, CMD_STR_MAX_LEN);
		memset(loop_ctx.method, 0, CMD_METHOD_LEN_MAX);
		miio_uart_write(&miio_uart, "get_down\r", strlen("get_down\r"), 0);
		recv_bytes = miio_uart_read(&miio_uart, loop_ctx.pbuf, CMD_STR_MAX_LEN, USER_UART_TIMEOUT_MS);
		if (recv_bytes <= 0 || loop_ctx.pbuf[0] == '\r' || recv_bytes >= 512) {
			miio_uart_write(&miio_uart, "error\r", strlen("error\r"), 0);
			goto _next;
		}

		result = uart_comamnd_decoder(loop_ctx.pbuf, recv_bytes, loop_ctx.method, &method_len);
		if (result != QM_EOK || method_len == 0) {
			goto _next;
		}		

		result = miio_command_find_by_method(miio_handle, loop_ctx.method, loop_ctx.pbuf, recv_bytes);
		if(result != QM_EOK && result != -QM_ERROR){
			miio_uart_send_str_wait_ack((void *)&miio_uart, "error \"undefined command\" -9999\r", USER_POLL_INTERVAL_MS);
		}
_next:
		qm_utils_time_countdown_ms(&loop_ctx.wait_timer, USER_POLL_INTERVAL_MS);
		qm_msleep(10);
	}

	return QM_EOK;
}

int miio_loop_handler_reg(miio_loop_handle_t handler)
{
	loop_ctx.g_loop_handler = handler;
	return QM_EOK;
}


int miio_beacon_handler_reg(miio_beacon_handle_t handler)
{
	static uint8_t is_init = 1;
	qm_utils_time_t wait_timer = {0};

	if(!is_init){
		return -QM_EINVAL;
	}
	
	is_init = 0;
	qm_utils_time_init(&wait_timer);
	qm_utils_time_countdown_ms(&wait_timer, MIIO_BEACON_WAIT_TIMEOUT);
	do{ 
		qm_msleep(20);
        loop_ctx.mq_id = mq_open("/beacon_mq", O_RDONLY | O_NONBLOCK) ;
	    if (qm_utils_time_is_expired(&wait_timer)) {
			break;
		}
    } while (loop_ctx.mq_id < 0);

	loop_ctx.g_beacon_handler = handler;

	if(loop_ctx.mq_id < 0){
		return -QM_EINVAL;
	}

	return QM_EOK;
}
