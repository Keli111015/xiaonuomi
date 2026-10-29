#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

/*
 * BF3901 模组丝印是 DVP 名，驱动按 1-bit SPI 收：
 *   VSYNC = CS（帧有效）
 *   PCLK  = SCLK
 *   D0    = DATA
 * HREF、D1 1-bit 模式不接。
 *
 * 丝印        ESP32-S3     说明
 * VCC         3.3V         不要接 5V
 * GND         GND          必须共地
 * SDA         GPIO21       I2C 数据
 * SCL         GPIO47       I2C 时钟
 * VSYNC       GPIO4        接到驱动的 CS（原 GPIO6）
 * PCLK        GPIO13       接到驱动的 SCLK
 * D0          GPIO8        接到驱动的 DATA0（原 GPIO16）
 * PWDN        GPIO41       低有效，软件拉低
 * HREF        悬空         不要接到 CS
 * D1          悬空
 * XCLK        没有丝印就不用接（模组自带 24 MHz）
 *
 * 开机只 early_init（申请 PSRAM 帧缓冲，不开流）。
 * 云连上后再 stream_start。5fps/20M 表和板上 24M 晶振不匹配，仍用 240x320 15fps。
 */
#define BSP_CAM_I2C_PORT        0
#define BSP_CAM_SDA_GPIO        21
#define BSP_CAM_SCL_GPIO        47
#define BSP_CAM_CS_GPIO         4
#define BSP_CAM_SCLK_GPIO       13
#define BSP_CAM_DATA0_GPIO      8
#define BSP_CAM_PWDN_GPIO       41
#define BSP_CAM_RESET_GPIO      (-1)
#define BSP_CAM_XCLK_GPIO       (-1)

esp_err_t bsp_camera_early_init(void);
esp_err_t bsp_camera_stream_start(void);
esp_err_t bsp_camera_start(void);
bool bsp_camera_is_available(void);
int bsp_camera_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif
