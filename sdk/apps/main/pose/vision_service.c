#include "vision_service.h"

#include "bsp_camera.h"
#include "agent_app.h"
#include "yolo11_pose.h"
#include "morphcube_study_runtime.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include <string.h>

#define LOG_TAG "vision_service"
#define VISION_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static TaskHandle_t s_pose_task = NULL;
static TaskHandle_t s_pose_req_task = NULL;
static uint8_t *s_pose_raw = NULL;
static portMUX_TYPE s_pose_mux = portMUX_INITIALIZER_UNLOCKED;

static int s_pose_rotation = 0;
static bool s_pose_calibrate = false;
static bool s_pose_req_pending = false;
static int s_pose_state = 0; /* 0: idle, 1: requested/waiting frame, 2: inferring */

static void cam_pose_task(void *arg)
{
    (void)arg;
    int last_rotation = -1;

    for (;;) {
        uint32_t notify_val = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (notify_val == 0) {
            continue;
        }

        uint32_t cam_w = 240, cam_h = 320;
        bsp_camera_get_resolution(&cam_w, &cam_h);
        size_t bytes = (size_t)cam_w * cam_h * 2;

        uint8_t *rgb = NULL;
        uint16_t w = 0, h = 0;
        yolo11_pose_frame_result_t result = {0};
        int64_t start = esp_timer_get_time();
        esp_err_t ret = ESP_OK;

        if (last_rotation != s_pose_rotation) {
            yolo11_pose_calibration_reset();
            last_rotation = s_pose_rotation;
        }

        if (s_pose_rotation == 90 || s_pose_rotation == 270) {
            ret = yolo11_pose_uyvy_to_rgb888_cw90(s_pose_raw, bytes,
                                                  (uint16_t)cam_w, (uint16_t)cam_h,
                                                  &rgb, &w, &h);
        } else {
            ret = yolo11_pose_uyvy_to_rgb888(s_pose_raw, bytes,
                                             (uint16_t)cam_w, (uint16_t)cam_h,
                                             &rgb, &w, &h);
        }

        if (ret == ESP_OK && s_pose_rotation >= 180) {
            size_t pixels = (size_t)w * h;
            for (size_t i = 0; i < pixels / 2; ++i) {
                for (int c = 0; c < 3; ++c) {
                    uint8_t tmp = rgb[3 * i + c];
                    rgb[3 * i + c] = rgb[3 * (pixels - 1 - i) + c];
                    rgb[3 * (pixels - 1 - i) + c] = tmp;
                }
            }
        }

        if (ret == ESP_OK) {
            ret = yolo11_pose_analyze_rgb888(rgb, w, h, "camera", &result);
        }

        int calibration_count = -1;
        if (s_pose_calibrate && ret == ESP_OK) {
            calibration_count = yolo11_pose_calibration_add(&result);
            result.calibrated = (calibration_count >= 3);
        }

        ESP_LOGI(LOG_TAG,
                 "POSE_RESULT ret=%s status=%d err=%d elapsed_ms=%u rotate=%d size=%ux%u valid=%d calibrated=%d cal_count=%d",
                 esp_err_to_name(ret), (int)result.status, (int)result.err_type,
                 (unsigned)((esp_timer_get_time() - start) / 1000),
                 s_pose_rotation, (unsigned)w, (unsigned)h, result.valid, result.calibrated, calibration_count);

#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
        morphcube_pose_t pose_to_report = MORPHCUBE_POSE_UNKNOWN;
        if (ret == ESP_OK && result.valid) {
            if (result.status == YOLO11_POSE_FRAME_OK) {
                pose_to_report = MORPHCUBE_POSE_NORMAL;
            } else if (result.status == YOLO11_POSE_FRAME_BAD_POSTURE) {
                if (result.err_type == YOLO11_POSE_ERR_HEAD_DOWN ||
                    result.err_type == YOLO11_POSE_ERR_TOO_CLOSE) {
                    pose_to_report = MORPHCUBE_POSE_HEAD_LOWERED;
                } else {
                    pose_to_report = MORPHCUBE_POSE_TILTED;
                }
            }
        }
        (void)morphcube_study_runtime_submit_pose(pose_to_report);
#endif

        portENTER_CRITICAL(&s_pose_mux);
        s_pose_state = 0;
        portEXIT_CRITICAL(&s_pose_mux);
        bsp_camera_release_frame(BSP_CAM_CLIENT_POSE);
    }
}

static esp_err_t cam_pose_request_exec(int rotation, bool calibrate)
{
    uint32_t cam_w = 240, cam_h = 320;
    bsp_camera_get_resolution(&cam_w, &cam_h);
    size_t frame_bytes = (size_t)cam_w * cam_h * 2;

    if (s_pose_task == NULL) {
        if (s_pose_raw == NULL) {
            s_pose_raw = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_pose_raw == NULL) {
                ESP_LOGE(LOG_TAG, "pose raw buffer alloc failed (%u bytes)", (unsigned)frame_bytes);
                return ESP_ERR_NO_MEM;
            }
        }
        if (xTaskCreatePinnedToCoreWithCaps(cam_pose_task, "cam_pose", 8192,
                                            NULL, 3, &s_pose_task, 1, VISION_STACK_CAPS) != pdPASS) {
            heap_caps_free(s_pose_raw);
            s_pose_raw = NULL;
            s_pose_task = NULL;
            ESP_LOGE(LOG_TAG, "cam_pose task create failed");
            return ESP_ERR_NO_MEM;
        }
    }

    portENTER_CRITICAL(&s_pose_mux);
    s_pose_rotation = rotation;
    s_pose_calibrate = calibrate;
    s_pose_state = 1;
    portEXIT_CRITICAL(&s_pose_mux);

    esp_err_t ret = bsp_camera_request_frame(BSP_CAM_CLIENT_POSE, s_pose_raw, frame_bytes, s_pose_task);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_pose_mux);
        s_pose_state = 0;
        portEXIT_CRITICAL(&s_pose_mux);
        ESP_LOGE(LOG_TAG, "bsp_camera_request_frame failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void cam_pose_req_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int rot;
        bool cal;

        portENTER_CRITICAL(&s_pose_mux);
        rot = s_pose_rotation;
        cal = s_pose_calibrate;
        s_pose_req_pending = false;
        portEXIT_CRITICAL(&s_pose_mux);

        esp_err_t err = cam_pose_request_exec(rot, cal);
        if (err != ESP_OK) {
            ESP_LOGW(LOG_TAG, "pose request exec failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t vision_service_init(void)
{
    if (s_pose_req_task != NULL) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCoreWithCaps(cam_pose_req_task, "cam_pose_req", 4096,
                                        NULL, 3, &s_pose_req_task, 1, VISION_STACK_CAPS) != pdPASS) {
        s_pose_req_task = NULL;
        ESP_LOGE(LOG_TAG, "cam_pose_req task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(LOG_TAG, "vision service initialized");
    return ESP_OK;
}

esp_err_t vision_service_pose_request(int rotation, bool calibrate)
{
    bool busy;

    if (!bsp_camera_is_available()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pose_req_task == NULL) {
        ESP_LOGW(LOG_TAG, "pose request dropped: request task not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (agent_app_is_dialog_busy()) {
        ESP_LOGW(LOG_TAG, "pose request deferred: voice dialog is active");
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_pose_mux);
    busy = (s_pose_state != 0) || s_pose_req_pending;
    if (!busy) {
        s_pose_rotation = rotation;
        s_pose_calibrate = calibrate;
        s_pose_req_pending = true;
    }
    portEXIT_CRITICAL(&s_pose_mux);

    if (busy) {
        ESP_LOGI(LOG_TAG, "pose request skipped: previous one still running");
        return ESP_ERR_INVALID_STATE;
    }

    xTaskNotifyGive(s_pose_req_task);
    return ESP_OK;
}

bool vision_service_is_busy(void)
{
    bool busy;
    portENTER_CRITICAL(&s_pose_mux);
    busy = (s_pose_state != 0) || s_pose_req_pending;
    portEXIT_CRITICAL(&s_pose_mux);
    return busy;
}
