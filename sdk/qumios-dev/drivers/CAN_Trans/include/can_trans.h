#ifndef _CAN_TRANS_H
#define _CAN_TRANS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "qm_at.h"
#include "qm_can.h"
#include "qm_event.h"

/**
 * Event base used by CAN_Trans internal dispatch.
 */
QM_EVENT_DECLARE_BASE(CAN_TRANS_EVENT);
#define CAN_TRANS_SUB_EVENT_DATA_RECV       0x0001
#define CAN_TRANS_SUB_EVENT_DATA_SEND       0x0002

#define CAN_TRANS_SELF_CHANNEL              (0)

#define CAN_TRANS_ADDR_IO_NUM               (3)

#define CAN_TRANS_DEV_BOX                   (0)
#define CAN_TRANS_DEV_UART                  (1)
#define CAN_TRANS_DEV_MULTIMETER            (2)
#define CAN_TRANS_DEV_LED                   (3)
#define CAN_TRANS_DEV_VOICE                 (4)
#define CAN_TRANS_DEV_RELAY                 (5)
#define CAN_TRANS_DEV_IO                    (6)
#define CAN_TRANS_DEV_LED_BUTTON            (7)


#ifndef CONFIG_CAN_TRANS_RECV_BUFF_LEN
#define CONFIG_CAN_TRANS_RECV_BUFF_LEN   (128)
#endif

#ifndef CONFIG_CAN_TRANS_TASK_SIZE  
#define CONFIG_CAN_TRANS_TASK_SIZE      (2048)
#endif

#ifndef CONFIG_CAN_TRANS_TASK_PRIO
#define CONFIG_CAN_TRANS_TASK_PRIO      (20)
#endif

#ifndef CONFIG_CAN_TRANS_IO_READ_TOGGLE
#define CONFIG_CAN_TRANS_IO_READ_TOGGLE (0)
#endif

#if defined(CONFIG_CAN_DEVICE_TYPE_BOX)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_BOX)
#elif defined(CONFIG_CAN_DEVICE_TYPE_UART)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_UART)
#elif defined(CONFIG_CAN_DEVICE_TYPE_MULTIMETER)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_MULTIMETER)
#elif defined(CONFIG_CAN_DEVICE_TYPE_LED)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_LED)
#elif defined(CONFIG_CAN_DEVICE_TYPE_VOICE)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_VOICE)
#elif defined(CONFIG_CAN_DEVICE_TYPE_RELAY)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_RELAY)
#elif defined(CONFIG_CAN_DEVICE_TYPE_IO)
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_IO)
#else
#error "pelease use qm menuconfig select can device type"
#endif

#ifndef CONFIG_CAN_DEVICE_SUPPORT
#define CONFIG_CAN_DEVICE_SUPPORT      (CAN_TRANS_DEV_UART)
#endif

typedef struct 
{
    uint8_t dev_type    : 4;       //设备类型
    uint8_t dev_addr    : 4;       //设备地址
}can_trans_addr_t;

typedef struct 
{
    can_trans_addr_t tran_dst_addr;
    can_trans_addr_t tran_src_addr;
    uint8_t dev_channel;        //设备通道号
}can_trans_channel_t;

typedef struct 
{
    uint8_t tx_pin;
    uint8_t rx_pin;
    qm_can_clock_t can_clock;
    can_trans_addr_t device_addr;
}can_trans_param_t;

typedef void(*can_trans_recv_fn)(can_trans_channel_t can_channel, uint8_t *data, int length);

int can_trnas_deinit(void);

int can_trans_init(can_trans_param_t *param);

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_UART
int can_trans_recv_register(can_trans_recv_fn recv_fn);
#endif

int can_trans_addr_io_register(uint8_t dev_io1, uint8_t dev_io2, uint8_t dev_io3); //需在初始化之前调用

int can_trans_at_server_register(can_trans_addr_t src_addr, qm_at_cmd_t *cmd_tbl, uint16_t cmd_num);

int can_trans_data_send(can_trans_channel_t tran_channel, uint8_t *data, int length);

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
int can_trans_data_flush(can_trans_channel_t tran_channel);
int can_trans_data_read(can_trans_channel_t tran_channel, uint8_t *data, int length, int timeout);
#endif

#ifdef __cplusplus
}
#endif

#endif
