#ifndef VISION_SERVICE_H
#define VISION_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化视觉姿态监测服务
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t vision_service_init(void);

/**
 * @brief 请求执行一次单帧坐姿检测（可在任何上下文调用，包括低栈的软件定时器任务）
 * @param rotation 旋转角度（0, 90, 180, 270）
 * @param calibrate 是否作为标定样本写入基准
 * @return esp_err_t ESP_OK 请求成功加入队列
 */
esp_err_t vision_service_pose_request(int rotation, bool calibrate);

/**
 * @brief 查询当前视觉服务是否正在运行或有待处理请求
 */
bool vision_service_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_SERVICE_H */
