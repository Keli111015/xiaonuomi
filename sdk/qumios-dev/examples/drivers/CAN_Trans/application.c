#include "qm.h"
#include "qm_log.h"
#include "qm_can.h"
#include "qm_platform.h"
#include "can_trans.h"

#include "main.h"

#define LOG_TAG "app"

#define CAN_TX_PIN  (QM_GPIO_PIN_34)
#define CAN_RX_PIN  (QM_GPIO_PIN_33)

#define CAN_ADDR_IO_PIN1  (QM_GPIO_PIN_16)
#define CAN_ADDR_IO_PIN2  (QM_GPIO_PIN_17)
#define CAN_ADDR_IO_PIN3  (QM_GPIO_PIN_18)


// #define CAN_TRANS_TEST_BOX 1

static can_trans_param_t param = {
    .can_clock = QM_CAN_CLOCK_1MHZ,
    .device_addr = {
        .dev_type = CAN_TRANS_DEV_UART,
        .dev_addr = 1,
    },
    .tx_pin = CAN_TX_PIN,
    .rx_pin = CAN_RX_PIN,
};


static qm_at_result_t at_uart_cfg_setup(void *channel, const char *args)
{
    QM_LOGD(LOG_TAG, "UART CFG :%s", args);
    int count = 30;
    qm_at_server_printf(channel,"AT_start//");
    for (int i = 0; i < count; i++)
    {
        qm_at_server_printf(channel, "hello world[%d]",i);
    }
    qm_at_server_printfln(channel,"//AT_complete");
    return QM_AT_RESULT_OK;
}

static qm_at_cmd_t at_cmd[] = {      
    {"AT+UART", NULL, NULL, NULL, at_uart_cfg_setup, NULL},
};

static void can_trans_recv_callback(can_trans_channel_t can_channel, uint8_t *data, int length)
{
    can_trans_channel_t trans_channel = {0};

    QM_LOGD(LOG_TAG, "RECV SRC_ADDR[%d/%d] DST_ADDR[%d/%d] channel %d", 
            can_channel.tran_src_addr.dev_type, can_channel.tran_src_addr.dev_addr,
            can_channel.tran_dst_addr.dev_type, can_channel.tran_dst_addr.dev_addr, can_channel.dev_channel);

    QM_HEX_LOGD(LOG_TAG, "RECV DATA ", data, length);

    memcpy(&trans_channel.tran_dst_addr, &can_channel.tran_src_addr, sizeof(can_trans_addr_t));
    memcpy(&trans_channel.tran_src_addr, &can_channel.tran_dst_addr, sizeof(can_trans_addr_t));
#if CAN_TRANS_TEST_BOX
    memcpy(&trans_channel.tran_dst_addr, &can_channel.tran_dst_addr, sizeof(can_trans_addr_t));

    can_trans_data_send(trans_channel, data, length);
    can_trans_data_send(trans_channel, "\r\nOK\r\n", strlen("\r\nOK\r\n"));
#else
    can_trans_data_send(trans_channel, data, length);
    can_trans_data_send(trans_channel, "\r\nOK\r\n", strlen("\r\nOK\r\n"));
#endif
}

void qm_application_start(void)
{
    QM_LOGD(LOG_TAG, "hello world\n");

    can_trans_addr_t src_addr = {
        .dev_type = CAN_TRANS_DEV_BOX,
        .dev_addr = 0,
    };

    can_trans_addr_io_register(CAN_ADDR_IO_PIN2, CAN_ADDR_IO_PIN2, CAN_ADDR_IO_PIN3);

	can_trans_init(&param);

    can_trans_recv_register(can_trans_recv_callback);
    can_trans_at_server_register(src_addr, at_cmd, QM_ARRAY_SIZE(at_cmd));

    QM_LOGD(LOG_TAG, "CAN_TRAN DEVTYPE %d DEV_ADDR %d", param.device_addr.dev_type, param.device_addr.dev_addr);
}

