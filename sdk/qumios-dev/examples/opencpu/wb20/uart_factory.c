#include "uart_factory.h"
#include "serial_protocol.h"

#define LOG_TAG "TEST"

typedef struct 
{
    uint8_t is_init;
    qm_sem_t serial_sem;
    uint8_t msg_readbuf[SERIAL_MSGLEN];
}gen_serial_info_t;

static gen_serial_info_t gen_serial_info = {0};

static uint8_t recv_buff[256] = {0};

static qm_uart_dev_t uart_dev = {
    .config = {
        .baud_rate = 9600,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = 14,
        .pins.rx_pin = 13,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = 0,
};

static serial_context_t *context = NULL;

static void uart_read_callback(qm_uart_dev_t *uart, uint32_t size)
{
    int recv_len = 0;
    if(uart == NULL || size == 0){
        return;
    }

    memset(recv_buff, 0, 50);
    recv_len = qm_uart_read(uart, recv_buff, size, 0);
    if(recv_len > 0){
        QM_HEX_LOGD(LOG_TAG, "READ: ", recv_buff, recv_len);
        qm_uart_write(uart, recv_buff, recv_len, 0);
    }
}


qm_at_server_t *qm_at_server = NULL;

int at_cmd_send(void *handle, char *buf, int size)
{
	return qm_uart_write(context->handle, buf, size, 0);
}

int at_cmd_recv(void *handle, char *buf, int size, int timeout)
{
    qm_msleep(1);
	return qm_uart_read(context->handle, buf, size, timeout);
}


qm_at_server_param_t g_qm_at_server_param = {
	.echo_mode = 0,
	.channel_type = 0,
	.recv_buf_len = 80,
	.handle = NULL,
	.send = at_cmd_send,
	.recv = at_cmd_recv,
};


qm_at_result_t at_get_fw_name(void *channel)
{
	char firmware_name[100] = {0};
    sprintf(firmware_name, "%s_%s_%s", XJIANG_FIRMWARE_VERSION_HEAD, \
                        XJIANG_FIRMWARE_VERSION_XJIANG, XJIANG_FIRMWARE_VERSION_MI);
	qm_at_server_printfln(channel, "+FW_NAME:\"%s\"", firmware_name);
	return QM_AT_RESULT_OK;
}

qm_at_result_t at_get_fw_version(void *channel)
{
	qm_at_server_printfln(channel, "+FW_VER:\"%s\"", XJIANG_FIRMWARE_VERSION_XJIANG);
	return QM_AT_RESULT_OK;
}

qm_at_result_t at_get_mac_ap(void *channel)
{
	uint8_t mac[6] = {0};
    arch_get_mac(mac);
	qm_at_server_printfln(channel, "+MAC_AP:\"%02X:%02X:%02X:%02X:%02X:%02X\"", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return QM_AT_RESULT_OK;
}

qm_at_result_t at_get_mac_sta(void *channel)
{
	uint8_t mac[6] = {0};
    arch_get_mac(mac);
	qm_at_server_printfln(channel, "+MAC_STA:\"%02X:%02X:%02X:%02X:%02X:%02X\"", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return QM_AT_RESULT_OK;
}


static qm_at_cmd_t at_cmd[] = {   
	{"AT+FW_NAME", NULL, NULL, at_get_fw_name, NULL, NULL},
	{"AT+FW_VER", NULL, NULL, at_get_fw_version, NULL, NULL},
	{"AT+MAC_AP", NULL, NULL, at_get_mac_ap, NULL, NULL},
    {"AT+MAC_STA", NULL, NULL, at_get_mac_sta, NULL, NULL}
};


  void qm_app_serial_init(void)
{
    serial_param_t serial_param = {0};
    serial_param.read_buf_size = SERIAL_MSGLEN;
    serial_param.pread_buf = gen_serial_info.msg_readbuf;

   qm_serial_init(&serial_param);
}

void uart_fac_init(void)
{
    int ret = QM_EOK;

    
    qm_app_serial_init();

    context = qm_serial_context_get();

    ret = qm_uart_ctrl(context->handle, QM_UART_CTRL_CMD_READ_CALLBACK, uart_read_callback);
    if(ret != QM_EOK){
        qm_printf("reg read cb FAILED!!");
        return;
    }
    g_qm_at_server_param.handle = context->handle;
    qm_at_server = qm_at_server_init(&g_qm_at_server_param);

    if(qm_at_server != NULL)
    {
        qm_printf("---------------------qm_at_server_init ok!---------------\n");
        
		qm_at_register_commands(qm_at_server, at_cmd, sizeof(at_cmd)/sizeof(at_cmd[0]));

    }
    else
    {
        qm_printf("---------------------qm_at_server_init fail!---------------\n");
    }
}




