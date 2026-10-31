#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef enum {
    BSP_CAM_CLIENT_NONE = 0,
    BSP_CAM_CLIENT_POSE,
    BSP_CAM_CLIENT_SNAP,
} bsp_cam_client_t;

/**
 * @brief 早期硬件初始化：UART VFS 接管、PWDN 脉冲、I2C/SPI 配置与 V4L2 缓冲分配
 */
esp_err_t bsp_camera_early_init(void);

/**
 * @brief 启动持续视频流
 */
esp_err_t bsp_camera_stream_start(void);

/**
 * @brief 停止视频流并释放 DMA
 */
esp_err_t bsp_camera_stream_stop(void);

/**
 * @brief 确保 early_init 并开启流
 */
esp_err_t bsp_camera_start(void);

/**
 * @brief 查询相机硬件是否可用
 */
bool bsp_camera_is_available(void);

/**
 * @brief 查询当前视频流是否处于开启状态
 */
bool bsp_camera_is_streaming(void);

/**
 * @brief 获取相机分辨率
 */
void bsp_camera_get_resolution(uint32_t *width, uint32_t *height);

/**
 * @brief 向相机引擎申请一帧单摄抓拍（完成后自动停流以释放 DMA）
 * @param client 请求客户端类型（姿态或快照）
 * @param dst 目的缓冲区指针
 * @param dst_size 目的缓冲区大小
 * @param notify_task 帧就绪后接收 TaskNotify 的目标任务句柄
 */
esp_err_t bsp_camera_request_frame(bsp_cam_client_t client, uint8_t *dst, size_t dst_size, TaskHandle_t notify_task);

/**
 * @brief 客户端处理完毕后释放抓拍锁，通知相机驱动回到就绪状态
 */
void bsp_camera_release_frame(bsp_cam_client_t client);

/* 向后兼容封装函数 */
esp_err_t bsp_camera_pose_request(int rotation, bool calibrate);
int bsp_camera_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAMERA_H */
