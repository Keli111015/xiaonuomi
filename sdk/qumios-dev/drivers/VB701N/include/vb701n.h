#ifndef VB701N_H
#define VB701N_H

#include "qm.h"
#include "qm_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * VB701N/AC707 55 AA 串口协议。与 MCU 无关，不实现 qm_audio。
 * 帧：55 AA | LEN(2) | CMD(2) | DATA(N) | SUM8
 * SUM8 = (包头+长度+命令+DATA) 累加和低 8 位
 *
 * 串口由平台设备绑定 qm_uart_init() 之后传进来。本文件不配置端口和管脚。
 */

#define VB701N_FRAME_HEAD_0          (0x55)
#define VB701N_FRAME_HEAD_1          (0xAA)

#define VB701N_CMD_HOST_STOP_AUDIO   (0x0201)
#define VB701N_CMD_HOST_VOLUME       (0x0203)
#define VB701N_CMD_HOST_OTA_CTRL     (0x0205)
#define VB701N_CMD_HOST_GET_VER      (0x020A)
#define VB701N_CMD_HOST_REC_CTRL     (0x0231)

#define VB701N_CMD_DEV_BOOT_OK       (0x0100)
#define VB701N_CMD_DEV_VERSION       (0x010A)
#define VB701N_CMD_DEV_ASR           (0x0180)
#define VB701N_CMD_DEV_MIC_AUDIO     (0x2080)
#define VB701N_CMD_HOST_SPK_AUDIO    (0x2081)

/* uart 必须已经 qm_uart_init。这里只保存端口并开始拆帧，不读串口。 */
qm_err_t vb701n_proto_init(qm_uart_dev_t *uart);

/* 平台读到的原始字节。由一个任务调用，不要并发。 */
void vb701n_proto_recv_push(const uint8_t *data, uint32_t len);

/* cmd: 如 0x020A；data 可为 NULL。 */
qm_err_t vb701n_proto_send(uint16_t cmd, const uint8_t *data, uint16_t data_len);

qm_err_t vb701n_proto_query_version(void);
qm_err_t vb701n_proto_stop_audio(void);
qm_err_t vb701n_proto_set_volume(uint8_t vol);

/* 02 31 01 开麦。 */
qm_err_t vb701n_proto_start_record(void);

/* 20 80 交给这个回调。cb 为 NULL 则丢掉麦克风帧。 */
typedef void (*vb701n_mic_cb_t)(const uint8_t *data, uint16_t len, void *user);
void vb701n_proto_set_mic_cb(vb701n_mic_cb_t cb, void *user);

/* 除麦克风外的完整帧。版本、开机、ASR 由设备绑定在这里处理。 */
typedef void (*vb701n_frame_cb_t)(uint16_t cmd, const uint8_t *data, uint16_t len, void *user);
void vb701n_proto_set_frame_cb(vb701n_frame_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* VB701N_H */
