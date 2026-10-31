#ifndef YOLO11_POSE_H
#define YOLO11_POSE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOLO11_POSE_ERR_NONE = 0,
    YOLO11_POSE_ERR_YAW = 1,         /* 歪头 */
    YOLO11_POSE_ERR_BODY_TILT = 2,   /* 身体倾斜 */
    YOLO11_POSE_ERR_TOO_CLOSE = 3,   /* 距离过近 */
    YOLO11_POSE_ERR_HEAD_DOWN = 4,   /* 趴下/低头过多 */
} yolo11_pose_err_type_t;

typedef enum {
    YOLO11_POSE_FRAME_NO_PERSON = 0,
    YOLO11_POSE_FRAME_OK,
    YOLO11_POSE_FRAME_BAD_POSTURE,
    YOLO11_POSE_FRAME_UNKNOWN,   /* 检出人体但关键点不足，无法判定坐姿 */
} yolo11_pose_frame_status_t;

typedef struct {
    yolo11_pose_frame_status_t status;
    yolo11_pose_err_type_t err_type;
    int valid;
    int calibrated;
    float yaw;
    float roll;
    float shoulder_diff;
    float lean;
    float eye_w_ratio;
    float head_down_ratio;
} yolo11_pose_frame_result_t;

esp_err_t yolo11_pose_init(void);
void yolo11_pose_calibration_reset(void);
int yolo11_pose_calibration_count(void);
int yolo11_pose_calibration_add(const yolo11_pose_frame_result_t *sample);

/** 启动早期预分配姿态链路常驻缓冲（解码/旋转/原生RGB），避免运行时抢连续 PSRAM。 */
void yolo11_pose_prealloc_buffers(void);
void yolo11_pose_release_decode_buf(void);

/** UYVY 转常驻 RGB888，不旋转（传感器 240×320 竖图）。成功后 *out_rgb888 不要 free。 */
esp_err_t yolo11_pose_uyvy_to_rgb888(const uint8_t *uyvy,
                                     size_t uyvy_len,
                                     uint16_t src_w,
                                     uint16_t src_h,
                                     uint8_t **out_rgb888,
                                     uint16_t *out_w,
                                     uint16_t *out_h);

/** UYVY 顺时针 90° 到常驻 RGB888（给 YOLO）。成功后 *out_rgb888 不要 free。 */
esp_err_t yolo11_pose_uyvy_to_rgb888_cw90(const uint8_t *uyvy,
                                          size_t uyvy_len,
                                          uint16_t src_w,
                                          uint16_t src_h,
                                          uint8_t **out_rgb888,
                                          uint16_t *out_w,
                                          uint16_t *out_h);

/** UYVY 竖图直接缩进调用方提供的 RGB565（校准预览，不经 RGB888）。 */
esp_err_t yolo11_pose_uyvy_to_rgb565(const uint8_t *uyvy,
                                     size_t uyvy_len,
                                     uint16_t src_w,
                                     uint16_t src_h,
                                     uint8_t *out_rgb565,
                                     uint16_t target_w,
                                     uint16_t target_h);

/** RGB888 顺时针 90° 到另一块常驻缓冲（给 YOLO）。成功后 *out_rgb888 不要 free。 */
esp_err_t yolo11_pose_rgb888_cw90(const uint8_t *rgb888,
                                  uint16_t src_w,
                                  uint16_t src_h,
                                  uint8_t **out_rgb888,
                                  uint16_t *out_w,
                                  uint16_t *out_h);

esp_err_t yolo11_pose_analyze_rgb888(const uint8_t *rgb888,
                                     uint16_t w,
                                     uint16_t h,
                                     const char *image_name,
                                     yolo11_pose_frame_result_t *out);

esp_err_t yolo11_pose_rgb888_to_rgb565(const uint8_t *rgb888,
                                       uint16_t src_w,
                                       uint16_t src_h,
                                       uint8_t **out_rgb565,
                                       uint16_t *out_w,
                                       uint16_t *out_h,
                                       uint16_t target_w,
                                       uint16_t target_h);

esp_err_t yolo11_pose_rotate_jpg_cw90(const uint8_t *jpg_data,
                                      size_t jpg_len,
                                      uint8_t **out_jpg,
                                      size_t *out_len);
esp_err_t yolo11_pose_analyze_jpg_named(const uint8_t *jpg_data,
                                        size_t jpg_len,
                                        const char *image_name,
                                        yolo11_pose_frame_result_t *out);
esp_err_t yolo11_pose_run_jpg_named(const uint8_t *jpg_data, size_t jpg_len, const char *image_name);

esp_err_t yolo11_pose_decode_jpg_to_rgb565(const uint8_t *jpg_data,
                                           size_t jpg_len,
                                           uint8_t **out_rgb565,
                                           uint16_t *out_w,
                                           uint16_t *out_h,
                                           uint16_t target_w,
                                           uint16_t target_h);

#ifdef __cplusplus
}
#endif

#endif
