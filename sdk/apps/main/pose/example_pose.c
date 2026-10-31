#include "example_pose.h"

#include "qm.h"
#include "yolo11_pose.h"

#define LOG_TAG "example_pose"

extern const uint8_t pose_bus_jpg_start[] asm("_binary_bus_jpg_start");
extern const uint8_t pose_bus_jpg_end[] asm("_binary_bus_jpg_end");

void example_pose_run(void)
{
    yolo11_pose_frame_result_t result;
    size_t len = (size_t)(pose_bus_jpg_end - pose_bus_jpg_start);
    esp_err_t ret;

    /* 先读模型。读 Flash 会关 cache，不要和摄像头一类外设同时进行。 */
    if (yolo11_pose_init() != ESP_OK) {
        QM_LOGE(LOG_TAG, "yolo11_pose_init failed");
        return;
    }

    ret = yolo11_pose_analyze_jpg_named(pose_bus_jpg_start, len, "bus.jpg", &result);
    yolo11_pose_release_decode_buf();
    if (ret != ESP_OK) {
        QM_LOGE(LOG_TAG, "pose bus jpg failed");
        return;
    }
    QM_LOGI(LOG_TAG, "pose test status=%d err=%d", (int)result.status, (int)result.err_type);
}

#if 0
/*
 * 本机摄像头。传感器给出一帧 UYVY（ct_eye_plush 上是 240x320 竖图）。
 * 顺时针转 90° 得到 RGB888，再送给模型。rgb888 是常驻缓冲，不要 free。
 * 摄像头若直接给出 JPEG，用 example_pose_run() 里的 yolo11_pose_analyze_jpg_named()。
 */
static void example_pose_run_camera(const uint8_t *uyvy, size_t uyvy_len,
                                    uint16_t src_w, uint16_t src_h)
{
    uint8_t *rgb888 = NULL;
    uint16_t w = 0;
    uint16_t h = 0;
    yolo11_pose_frame_result_t result;
    esp_err_t ret;

    ret = yolo11_pose_uyvy_to_rgb888_cw90(uyvy, uyvy_len, src_w, src_h,
                                          &rgb888, &w, &h);
    if (ret != ESP_OK) {
        QM_LOGE(LOG_TAG, "uyvy to rgb888 failed");
        return;
    }
    ret = yolo11_pose_analyze_rgb888(rgb888, w, h, "camera", &result);
    if (ret != ESP_OK) {
        QM_LOGE(LOG_TAG, "pose camera failed");
        return;
    }
    QM_LOGI(LOG_TAG, "pose camera status=%d err=%d",
            (int)result.status, (int)result.err_type);
}
#endif
