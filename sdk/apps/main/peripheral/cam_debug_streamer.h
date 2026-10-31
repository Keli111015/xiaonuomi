#ifndef CAM_DEBUG_STREAMER_H
#define CAM_DEBUG_STREAMER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化调试抓拍与上位机推流服务
 */
esp_err_t cam_debug_streamer_init(void);

/**
 * @brief 触发一次单帧抓拍并通过串口以 JPG2 格式输出
 */
esp_err_t cam_debug_streamer_request_snap(void);

/**
 * @brief 注册相机调试相关 CLI 指令（cam start/stop/status/snap/pose/calibrate）
 */
int cam_debug_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* CAM_DEBUG_STREAMER_H */
