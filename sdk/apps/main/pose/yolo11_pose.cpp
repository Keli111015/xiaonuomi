#include "coco_pose.hpp"
#include "dl_image_jpeg.hpp"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "yolo11_pose.h"

static const char *TAG = "yolo11n-pose";
static COCOPose *s_pose = nullptr;

#define POSE_YAW_BAD_MAX        0.90f
#define POSE_ROLL_BAD_MIN       0.10f
#define POSE_SHOULDER_BAD_MIN   0.03f
#define POSE_LEAN_BAD_MIN       0.50f
#define POSE_CLOSE_EYE_RATIO    0.20f
#define POSE_HEAD_DOWN_RATIO    -0.05f

static const char *kKptNames[17] = {"nose",
                                    "left eye",
                                    "right eye",
                                    "left ear",
                                    "right ear",
                                    "left shoulder",
                                    "right shoulder",
                                    "left elbow",
                                    "right elbow",
                                    "left wrist",
                                    "right wrist",
                                    "left hip",
                                    "right hip",
                                    "left knee",
                                    "right knee",
                                    "left ankle",
                                    "right ankle"};

static bool kpt_valid(int x, int y)
{
    return x != 0 || y != 0;
}

static void log_pose_keypoints(const char *name, const dl::detect::result_t &res)
{
    char log_buf[512];
    char *p = log_buf;
    char *end = log_buf + sizeof(log_buf);

    for (int i = 0; i < 17; ++i) {
        size_t left = (size_t)(end - p);
        int written = snprintf(p,
                               left,
                               "%s: [%d, %d] ",
                               kKptNames[i],
                               res.keypoint[2 * i],
                               res.keypoint[2 * i + 1]);
        if (written < 0 || written >= (int)left) {
            break;
        }
        p += written;
    }
    ESP_LOGI(TAG, "%s %s", name, log_buf);
}

static yolo11_pose_err_type_t pick_primary_pose_err(bool yaw_bad,
                                                    bool tilt_bad,
                                                    bool close_bad,
                                                    bool head_down_bad)
{
    if (head_down_bad) {
        return YOLO11_POSE_ERR_HEAD_DOWN;
    }
    if (close_bad) {
        return YOLO11_POSE_ERR_TOO_CLOSE;
    }
    if (yaw_bad) {
        return YOLO11_POSE_ERR_YAW;
    }
    if (tilt_bad) {
        return YOLO11_POSE_ERR_BODY_TILT;
    }
    return YOLO11_POSE_ERR_NONE;
}


/* Calibration is RAM-only and owned by the serialized camera pose worker. */
static yolo11_pose_frame_result_t s_baseline = {};
static int s_calibration_count = 0;
extern "C" void yolo11_pose_calibration_reset(void)
{
    s_baseline = {};
    s_calibration_count = 0;
}
extern "C" int yolo11_pose_calibration_count(void) { return s_calibration_count; }
extern "C" int yolo11_pose_calibration_add(const yolo11_pose_frame_result_t *sample)
{
    if (sample == nullptr || !sample->valid ||
        !std::isfinite(sample->roll) || !std::isfinite(sample->shoulder_diff) ||
        !std::isfinite(sample->lean) || !std::isfinite(sample->head_down_ratio) ||
        !std::isfinite(sample->eye_w_ratio) || sample->eye_w_ratio <= 0.0f)
        return s_calibration_count;
    if (s_calibration_count >= 3) return s_calibration_count;
    if (s_calibration_count &&
        (fabsf(sample->lean - s_baseline.lean) > 0.12f ||
         fabsf(sample->roll - s_baseline.roll) > 0.12f ||
         fabsf(sample->shoulder_diff - s_baseline.shoulder_diff) > 0.12f ||
         fabsf(sample->head_down_ratio - s_baseline.head_down_ratio) > 0.18f ||
         fabsf(sample->eye_w_ratio / s_baseline.eye_w_ratio - 1.0f) > 0.20f)) {
        yolo11_pose_calibration_reset();
    }
    float n = (float)s_calibration_count;
    s_baseline.roll = (s_baseline.roll*n + sample->roll)/(n+1);
    s_baseline.shoulder_diff = (s_baseline.shoulder_diff*n + sample->shoulder_diff)/(n+1);
    s_baseline.lean = (s_baseline.lean*n + sample->lean)/(n+1);
    s_baseline.eye_w_ratio = (s_baseline.eye_w_ratio*n + sample->eye_w_ratio)/(n+1);
    s_baseline.head_down_ratio = (s_baseline.head_down_ratio*n + sample->head_down_ratio)/(n+1);
    return ++s_calibration_count;
}

static void analyze_pose_result(const dl::detect::result_t &res,
                                int img_w, int img_h,
                                yolo11_pose_frame_result_t *out)
{
    *out = {};
    out->status = YOLO11_POSE_FRAME_UNKNOWN;
    out->calibrated = s_calibration_count >= 3;
    const auto &kp = res.keypoint;
    if (img_w <= 0 || img_h <= 0 || kp.size() < 34 || res.score < 0.65f) return;
    const int required[] = {0,1,2,5,6};
    for (int k : required) {
        int x=kp[2*k], y=kp[2*k+1];
        if (!kpt_valid(x,y) || x < 2 || y < 2 || x >= img_w-2 || y >= img_h-2) return;
    }
    float ex=(float)(kp[4]-kp[2]), ey=(float)(kp[5]-kp[3]);
    float sx=(float)(kp[12]-kp[10]), sy=(float)(kp[13]-kp[11]);
    float ew=sqrtf(ex*ex+ey*ey), sw=sqrtf(sx*sx+sy*sy);
    float eye_y=(kp[3]+kp[5])*0.5f;
    float shoulder_y=(kp[11]+kp[13])*0.5f;
    /* Reject tiny, clipped, upside-down or geometrically inconsistent detections. */
    if (ew < 8 || sw < img_w*0.18f || ew > sw*0.80f ||
        shoulder_y-eye_y < img_h*0.06f || fabsf(ex) < ew*0.75f || fabsf(sx) < sw*0.65f ||
        kp[1] < eye_y-img_h*0.04f || kp[1] > shoulder_y) return;
    out->valid=1;
    out->roll=ey/ex;
    out->shoulder_diff=sy/sx;
    out->lean=(kp[0]-(kp[10]+kp[12])*0.5f)/sw;
    out->eye_w_ratio=ew/img_w;
    out->head_down_ratio=(kp[1]-shoulder_y)/sw;

    /* 若尚未完成 3 帧基准校准：
     * 1. 先用绝对阈值判断当前是否为明显不良坐姿；
     * 2. 若当前坐姿基本端正，则自动将其纳入 3 帧个人坐正基准校准（实现开启专注后自动无感校准）。 */
    if (!out->calibrated) {
        bool abs_tilt = fabsf(out->roll) > POSE_ROLL_BAD_MIN * 1.8f ||
                        fabsf(out->shoulder_diff) > POSE_SHOULDER_BAD_MIN * 4.0f ||
                        fabsf(out->lean) > POSE_LEAN_BAD_MIN;
        bool abs_close = out->eye_w_ratio > POSE_CLOSE_EYE_RATIO * 1.4f;
        bool abs_down = out->head_down_ratio > POSE_HEAD_DOWN_RATIO;
        if (!abs_tilt && !abs_close && !abs_down) {
            int cnt = yolo11_pose_calibration_add(out);
            out->calibrated = (cnt >= 3);
            ESP_LOGI(TAG, "auto-calibration sample accepted: %d/3", cnt);
        }
        if (!out->calibrated) {
            /* Calibration candidates are measurements, not posture verdicts. */
            out->status = YOLO11_POSE_FRAME_UNKNOWN;
            return;
        }
    }

    bool tilt = fabsf(out->roll-s_baseline.roll)>0.15f ||
                fabsf(out->shoulder_diff-s_baseline.shoulder_diff)>0.12f ||
                fabsf(out->lean-s_baseline.lean)>0.18f;
    bool close = out->eye_w_ratio > s_baseline.eye_w_ratio*1.30f;
    bool down = out->head_down_ratio-s_baseline.head_down_ratio>0.22f;
    out->err_type = pick_primary_pose_err(false,tilt,close,down);
    out->status = out->err_type == YOLO11_POSE_ERR_NONE ? YOLO11_POSE_FRAME_OK : YOLO11_POSE_FRAME_BAD_POSTURE;
}

extern "C" esp_err_t yolo11_pose_init(void)
{
#if CONFIG_COCO_POSE_MODEL_IN_SDCARD
    ESP_LOGE(TAG, "SD card model loading is not wired in this project");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_pose != nullptr) {
        return ESP_OK;
    }

    s_pose = new (std::nothrow) COCOPose(static_cast<COCOPose::model_type_t>(CONFIG_DEFAULT_COCO_POSE_MODEL), false);
    if (s_pose == nullptr) {
        ESP_LOGE(TAG, "alloc pose model failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
#endif
}
/*extern "C" esp_err_t yolo11_pose_init(void)
{
#if CONFIG_COCO_POSE_MODEL_IN_SDCARD
    ESP_LOGE(TAG, "SD card model loading is not wired in this project");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_pose != nullptr) {
        // 二次检查：即使对象存在，也要确认内部模型真的加载成功了
        if (s_pose->get_raw_model() == nullptr) {
            ESP_LOGE(TAG, "pose model was not properly initialized, retrying...");
            delete s_pose;
            s_pose = nullptr;
        } else {
            return ESP_OK;
        }
    }

    s_pose = new (std::nothrow) COCOPose(static_cast<COCOPose::model_type_t>(CONFIG_DEFAULT_COCO_POSE_MODEL), false);
    if (s_pose == nullptr) {
        ESP_LOGE(TAG, "alloc pose model failed");
        return ESP_ERR_NO_MEM;
    }

    // 检查内部dl::Model是否真的创建成功
    if (s_pose->get_raw_model() == nullptr) {
        ESP_LOGE(TAG, "pose model load failed, model data may be corrupted, please erase flash and reflash");
        delete s_pose;
        s_pose = nullptr;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "pose model loaded successfully");
    return ESP_OK;
#endif
}*/

/* ===== 旋转用的常驻目标缓冲 =====
 * 这块内存是 320*240*3 = 230400 字节，在碎片化的 PSRAM 里经常凑不出连续空间，
 * 于是旋转失败（日志 "rotate dst alloc failed"）。
 * 改成一经分配就永不释放（常驻）后，它不再参与每轮拍照约 1.2MB 的 malloc/free 轮转，
 * 碎片化对它就没有影响了。
 * 线程安全：全工程只有 cam_upload_task() 会调用旋转，而 s_capture_busy 保证
 * 同一时刻只有一个上传任务在跑，所以这里不需要加锁。 */
static uint8_t *s_rotate_dst_buf = nullptr;
static size_t s_rotate_dst_size = 0;
static uint8_t *s_native_rgb_buf = nullptr;
static size_t s_native_rgb_size = 0;

static uint8_t *resident_psram_buf_get(uint8_t **slot, size_t *slot_size, size_t size, const char *name)
{
    if (*slot != nullptr && *slot_size >= size) {
        return *slot;
    }

    if (*slot != nullptr) {
        heap_caps_free(*slot);
        *slot = nullptr;
        *slot_size = 0;
    }

    uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf = (uint8_t *)heap_caps_aligned_alloc(16, size, MALLOC_CAP_DEFAULT);
    }
    if (buf == nullptr) {
        return nullptr;
    }

    *slot = buf;
    *slot_size = size;
    ESP_LOGI(TAG, "%s allocated (resident), size=%u from=%s",
             name, (unsigned)size, esp_ptr_external_ram(buf) ? "psram" : "internal");
    return buf;
}

static uint8_t *rotate_dst_buf_get(size_t size)
{
    return resident_psram_buf_get(&s_rotate_dst_buf, &s_rotate_dst_size, size, "rotate dst buf");
}

static uint8_t *native_rgb_buf_get(size_t size)
{
    return resident_psram_buf_get(&s_native_rgb_buf, &s_native_rgb_size, size, "native rgb buf");
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static inline void yuv_to_rgb888(int y, int u, int v, uint8_t *rgb)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    rgb[0] = clamp_u8((298 * c + 409 * e + 128) >> 8);
    rgb[1] = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    rgb[2] = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

extern "C" esp_err_t yolo11_pose_uyvy_to_rgb888(const uint8_t *uyvy,
                                                size_t uyvy_len,
                                                uint16_t src_w,
                                                uint16_t src_h,
                                                uint8_t **out_rgb888,
                                                uint16_t *out_w,
                                                uint16_t *out_h)
{
    if (uyvy == nullptr || out_rgb888 == nullptr || out_w == nullptr || out_h == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_w < 2 || (src_w & 1u) || src_h == 0 ||
        uyvy_len < (size_t)src_w * src_h * 2) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb888 = nullptr;
    *out_w = 0;
    *out_h = 0;

    uint8_t *dst = native_rgb_buf_get((size_t)src_w * src_h * 3);
    if (dst == nullptr) {
        ESP_LOGE(TAG, "uyvy rgb888 dst alloc failed, %ux%u", src_w, src_h);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t y = 0; y < src_h; y++) {
        const uint8_t *row = uyvy + (size_t)y * src_w * 2;
        uint8_t *dst_row = dst + (size_t)y * src_w * 3;
        for (uint16_t x = 0; x < src_w; x += 2) {
            int u = row[x * 2];
            int y0 = row[x * 2 + 1];
            int v = row[x * 2 + 2];
            int y1 = row[x * 2 + 3];
            yuv_to_rgb888(y0, u, v, dst_row + (size_t)x * 3);
            yuv_to_rgb888(y1, u, v, dst_row + (size_t)(x + 1) * 3);
        }
    }

    *out_rgb888 = dst;
    *out_w = src_w;
    *out_h = src_h;
    ESP_LOGI(TAG, "uyvy %ux%u -> rgb888 %ux%u", src_w, src_h, src_w, src_h);
    return ESP_OK;
}

extern "C" esp_err_t yolo11_pose_uyvy_to_rgb888_cw90(const uint8_t *uyvy,
                                                     size_t uyvy_len,
                                                     uint16_t src_w,
                                                     uint16_t src_h,
                                                     uint8_t **out_rgb888,
                                                     uint16_t *out_w,
                                                     uint16_t *out_h)
{
    if (uyvy == nullptr || out_rgb888 == nullptr || out_w == nullptr || out_h == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_w < 2 || (src_w & 1u) || src_h == 0 ||
        uyvy_len < (size_t)src_w * src_h * 2) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb888 = nullptr;
    *out_w = 0;
    *out_h = 0;

    uint16_t dst_w = src_h;
    uint16_t dst_h = src_w;
    uint8_t *dst = rotate_dst_buf_get((size_t)dst_w * dst_h * 3);
    if (dst == nullptr) {
        ESP_LOGE(TAG, "uyvy cw90 dst alloc failed, %ux%u", dst_w, dst_h);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t y = 0; y < src_h; y++) {
        const uint8_t *row = uyvy + (size_t)y * src_w * 2;
        for (uint16_t x = 0; x < src_w; x += 2) {
            int u = row[x * 2];
            int y0 = row[x * 2 + 1];
            int v = row[x * 2 + 2];
            int y1 = row[x * 2 + 3];
            uint8_t *p0 = dst + ((size_t)x * dst_w + (dst_w - 1 - y)) * 3;
            uint8_t *p1 = dst + ((size_t)(x + 1) * dst_w + (dst_w - 1 - y)) * 3;
            yuv_to_rgb888(y0, u, v, p0);
            yuv_to_rgb888(y1, u, v, p1);
        }
    }

    *out_rgb888 = dst;
    *out_w = dst_w;
    *out_h = dst_h;
    ESP_LOGI(TAG, "uyvy cw90 %ux%u -> rgb888 %ux%u", src_w, src_h, dst_w, dst_h);
    return ESP_OK;
}

extern "C" esp_err_t yolo11_pose_uyvy_to_rgb565(const uint8_t *uyvy,
                                                size_t uyvy_len,
                                                uint16_t src_w,
                                                uint16_t src_h,
                                                uint8_t *out_rgb565,
                                                uint16_t target_w,
                                                uint16_t target_h)
{
    if (uyvy == nullptr || out_rgb565 == nullptr || src_w < 2 || (src_w & 1u) ||
        src_h == 0 || target_w == 0 || target_h == 0 ||
        uyvy_len < (size_t)src_w * src_h * 2) {
        return ESP_ERR_INVALID_ARG;
    }

    float scale = (float)target_w / src_w;
    if ((float)target_h / src_h < scale) {
        scale = (float)target_h / src_h;
    }
    uint16_t scaled_w = (uint16_t)(src_w * scale);
    uint16_t scaled_h = (uint16_t)(src_h * scale);
    uint16_t offset_x = (target_w - scaled_w) / 2;
    uint16_t offset_y = (target_h - scaled_h) / 2;

    memset(out_rgb565, 0, (size_t)target_w * target_h * 2);
    for (uint16_t dy = 0; dy < scaled_h; dy++) {
        uint16_t sy = (uint16_t)(dy / scale);
        if (sy >= src_h) {
            sy = src_h - 1;
        }
        const uint8_t *src_row = uyvy + (size_t)sy * src_w * 2;
        uint16_t *dst_row = (uint16_t *)(out_rgb565 + ((offset_y + dy) * target_w + offset_x) * 2);
        for (uint16_t dx = 0; dx < scaled_w; dx++) {
            uint16_t sx = (uint16_t)(dx / scale);
            if (sx >= src_w) {
                sx = src_w - 1;
            }
            uint16_t pair = (uint16_t)(sx & ~1u);
            int u = src_row[pair * 2];
            int y0 = src_row[pair * 2 + 1];
            int v = src_row[pair * 2 + 2];
            int y1 = src_row[pair * 2 + 3];
            uint8_t rgb[3];
            yuv_to_rgb888((sx & 1u) ? y1 : y0, u, v, rgb);
            dst_row[dx] = (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
        }
    }
    return ESP_OK;
}

extern "C" esp_err_t yolo11_pose_rgb888_cw90(const uint8_t *rgb888,
                                             uint16_t src_w,
                                             uint16_t src_h,
                                             uint8_t **out_rgb888,
                                             uint16_t *out_w,
                                             uint16_t *out_h)
{
    if (rgb888 == nullptr || out_rgb888 == nullptr || out_w == nullptr || out_h == nullptr ||
        src_w == 0 || src_h == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb888 = nullptr;
    *out_w = 0;
    *out_h = 0;

    uint16_t dst_w = src_h;
    uint16_t dst_h = src_w;
    uint8_t *dst = rotate_dst_buf_get((size_t)dst_w * dst_h * 3);
    if (dst == nullptr) {
        ESP_LOGE(TAG, "rgb888 cw90 dst alloc failed, %ux%u", dst_w, dst_h);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t y = 0; y < src_h; y++) {
        const uint8_t *src_row = rgb888 + (size_t)y * src_w * 3;
        for (uint16_t x = 0; x < src_w; x++) {
            size_t dst_idx = ((size_t)x * dst_w + (dst_w - 1 - y)) * 3;
            const uint8_t *src_px = src_row + (size_t)x * 3;
            dst[dst_idx] = src_px[0];
            dst[dst_idx + 1] = src_px[1];
            dst[dst_idx + 2] = src_px[2];
        }
    }

    *out_rgb888 = dst;
    *out_w = dst_w;
    *out_h = dst_h;
    ESP_LOGI(TAG, "rgb888 cw90 %ux%u -> %ux%u", src_w, src_h, dst_w, dst_h);
    return ESP_OK;
}

static esp_err_t analyze_rgb888_img(const dl::image::img_t &img,
                                    const char *name,
                                    yolo11_pose_frame_result_t *out)
{
    auto &pose_results = s_pose->run(img);
    if (pose_results.empty()) {
        ESP_LOGI(TAG, "%s no pose detected", name);
        return ESP_OK;
    }

    /* A different person must not silently replace the calibration subject. */
    if (pose_results.size() != 1) {
        if (out != nullptr) { *out = {}; out->status = YOLO11_POSE_FRAME_UNKNOWN; }
        return ESP_OK;
    }
    const auto &res = pose_results.front();
    ESP_LOGI(TAG,
             "%s [score: %f, x1: %d, y1: %d, x2: %d, y2: %d]",
             name,
             res.score,
             res.box[0],
             res.box[1],
             res.box[2],
             res.box[3]);
    log_pose_keypoints(name, res);

    if (out != nullptr) {
        analyze_pose_result(res, (int)img.width, (int)img.height, out);
        ESP_LOGI(TAG,
                 "%s pose metrics yaw=%.3f roll=%.3f shoulder=%.3f lean=%.3f eye=%.3f head=%.3f status=%d err=%d",
                 name,
                 out->yaw,
                 out->roll,
                 out->shoulder_diff,
                 out->lean,
                 out->eye_w_ratio,
                 out->head_down_ratio,
                 (int)out->status,
                 (int)out->err_type);
    }
    return ESP_OK;
}

extern "C" esp_err_t yolo11_pose_analyze_rgb888(const uint8_t *rgb888,
                                                uint16_t w,
                                                uint16_t h,
                                                const char *image_name,
                                                yolo11_pose_frame_result_t *out)
{
    const char *name = (image_name != nullptr && image_name[0] != '\0') ? image_name : "-";

    if (out != nullptr) {
        memset(out, 0, sizeof(*out));
        out->status = YOLO11_POSE_FRAME_NO_PERSON;
    }

    if (rgb888 == nullptr || w == 0 || h == 0) {
        ESP_LOGE(TAG, "%s invalid rgb888", name);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = yolo11_pose_init();
    if (ret != ESP_OK) {
        return ret;
    }

    dl::image::img_t img = {
        .data = (void *)rgb888,
        .width = w,
        .height = h,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };
    return analyze_rgb888_img(img, name, out);
}

extern "C" esp_err_t yolo11_pose_rgb888_to_rgb565(const uint8_t *rgb888,
                                                  uint16_t src_w,
                                                  uint16_t src_h,
                                                  uint8_t **out_rgb565,
                                                  uint16_t *out_w,
                                                  uint16_t *out_h,
                                                  uint16_t target_w,
                                                  uint16_t target_h)
{
    if (rgb888 == nullptr || out_rgb565 == nullptr || out_w == nullptr || out_h == nullptr ||
        src_w == 0 || src_h == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb565 = nullptr;
    *out_w = 0;
    *out_h = 0;

    bool do_scale = (target_w > 0 && target_h > 0);
    uint16_t out_width = do_scale ? target_w : src_w;
    uint16_t out_height = do_scale ? target_h : src_h;
    size_t rgb565_size = (size_t)out_width * out_height * 2;
    uint8_t *rgb565 = (uint8_t *)heap_caps_malloc(rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb565 == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    if (do_scale) {
        float scale = (float)target_w / src_w;
        if ((float)target_h / src_h < scale) {
            scale = (float)target_h / src_h;
        }
        uint16_t scaled_w = (uint16_t)(src_w * scale);
        uint16_t scaled_h = (uint16_t)(src_h * scale);
        uint16_t offset_x = (target_w - scaled_w) / 2;
        uint16_t offset_y = (target_h - scaled_h) / 2;

        memset(rgb565, 0, rgb565_size);
        for (uint16_t dy = 0; dy < scaled_h; dy++) {
            uint16_t sy = (uint16_t)(dy / scale);
            if (sy >= src_h) {
                sy = src_h - 1;
            }
            uint16_t *dst_row = (uint16_t *)(rgb565 + ((offset_y + dy) * target_w + offset_x) * 2);
            const uint8_t *src_row = rgb888 + (size_t)sy * src_w * 3;
            for (uint16_t dx = 0; dx < scaled_w; dx++) {
                uint16_t sx = (uint16_t)(dx / scale);
                if (sx >= src_w) {
                    sx = src_w - 1;
                }
                uint8_t r = src_row[sx * 3 + 0];
                uint8_t g = src_row[sx * 3 + 1];
                uint8_t b = src_row[sx * 3 + 2];
                dst_row[dx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else {
        uint16_t *dst = (uint16_t *)rgb565;
        for (size_t i = 0; i < (size_t)src_w * src_h; i++) {
            uint8_t r = rgb888[i * 3 + 0];
            uint8_t g = rgb888[i * 3 + 1];
            uint8_t b = rgb888[i * 3 + 2];
            dst[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }

    *out_rgb565 = rgb565;
    *out_w = out_width;
    *out_h = out_height;
    return ESP_OK;
}

/* ===== 解码输出用的常驻缓冲（rotate 与 analyze 共用）=====
 * 这块 320*240*3 = 230400 字节原来是「每次解码临时申请」，在碎片化的 PSRAM 里
 * 长时间运行后会分配失败，实测日志：
 *     E dl_image_jpeg: Failed to alloc output buffer.
 *     E yolo11n-pose: 373.jpg decode jpg failed
 *     W bsp_camera: pose run failed, key=373.jpg ret=ESP_FAIL
 * 改成常驻后，它不再参与每轮约 1.2MB 的 malloc/free 轮转，碎片化就影响不到它。
 *
 * 线程安全：只有 rotate_jpg_cw90() 与 analyze_jpg_named() 用它，而这两个函数
 * 只在 cam_upload_task() 里**顺序**调用（rotate 返回后才 analyze），且
 * s_capture_busy 保证同一时刻只有一个上传任务 —— 所以共用一块是安全的。
 * 注意：yolo11_pose_decode_jpg_to_rgb565()（校准页预览，跑在 UI 任务）
 * 不使用这块缓冲，所以两条线程不会冲突。 */
static uint8_t *s_decode_buf = nullptr;
static size_t s_decode_buf_size = 0;

static uint8_t *decode_buf_get(size_t size)
{
    if (s_decode_buf != nullptr && s_decode_buf_size >= size) {
        return s_decode_buf;        /* 已有且够大：直接复用，不碰堆 */
    }

    if (s_decode_buf != nullptr) {
        heap_caps_free(s_decode_buf);
        s_decode_buf = nullptr;
        s_decode_buf_size = 0;
    }

    /* JPEG 解码器要求输出缓冲 16 字节对齐 */
    uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf = (uint8_t *)heap_caps_aligned_alloc(16, size, MALLOC_CAP_DEFAULT);
    }
    if (buf == nullptr) {
        return nullptr;
    }

    s_decode_buf = buf;
    s_decode_buf_size = size;
    ESP_LOGI(TAG, "decode buf allocated (resident), size=%u from=%s",
             (unsigned)size, esp_ptr_external_ram(buf) ? "psram" : "internal");
    return buf;
}

/* 用常驻缓冲把 JPEG 解码成 RGB888。
 * 成功后 img 指向常驻缓冲，**调用方不要 free**。
 * 这里自己调 esp_new_jpeg 的 jpeg_dec_*，因为 esp-dl 的 sw_decode_jpeg()
 * 内部写死了「自己分配输出缓冲」，换不出常驻缓冲。 */
static bool decode_jpg_to_resident_rgb888(const uint8_t *jpg, size_t jpg_len, dl::image::img_t *img)
{
    if (jpg == nullptr || jpg_len == 0 || img == nullptr) {
        return false;
    }

    jpeg_dec_config_t cfg = {.output_type = JPEG_PIXEL_FORMAT_RGB888,
                             .scale = {.width = 0, .height = 0},
                             .clipper = {.width = 0, .height = 0},
                             .rotate = JPEG_ROTATE_0D,
                             .block_enable = false};

    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "resident decode: open failed");
        return false;
    }

    jpeg_dec_io_t io = {};
    io.inbuf = (uint8_t *)jpg;
    io.inbuf_len = (int)jpg_len;

    jpeg_dec_header_info_t info = {};
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "resident decode: parse header failed");
        jpeg_dec_close(dec);
        return false;
    }

    int out_len = 0;
    if (jpeg_dec_get_outbuf_len(dec, &out_len) != JPEG_ERR_OK || out_len <= 0) {
        ESP_LOGE(TAG, "resident decode: get outbuf len failed");
        jpeg_dec_close(dec);
        return false;
    }

    uint8_t *buf = decode_buf_get((size_t)out_len);
    if (buf == nullptr) {
        ESP_LOGE(TAG, "resident decode: alloc failed, need=%d", out_len);
        jpeg_dec_close(dec);
        return false;
    }

    io.outbuf = buf;
    if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "resident decode: process failed");
        jpeg_dec_close(dec);
        return false;
    }
    jpeg_dec_close(dec);

    img->data = buf;
    img->width = info.width;
    img->height = info.height;
    img->pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
    return true;
}

extern "C" esp_err_t yolo11_pose_rotate_jpg_cw90(const uint8_t *jpg_data,
                                                 size_t jpg_len,
                                                 uint8_t **out_jpg,
                                                 size_t *out_len)
{
    if (jpg_data == nullptr || jpg_len == 0 || out_jpg == nullptr || out_len == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_jpg = nullptr;
    *out_len = 0;

    /* 解码到常驻缓冲：不再每轮申请 230400，碎片化影响不到它 */
    dl::image::img_t src = {};
    if (!decode_jpg_to_resident_rgb888(jpg_data, jpg_len, &src)) {
        ESP_LOGE(TAG, "rotate decode jpg failed");
        return ESP_FAIL;
    }

    uint16_t src_w = src.width;
    uint16_t src_h = src.height;
    uint16_t dst_w = src_h;
    uint16_t dst_h = src_w;
    size_t dst_size = (size_t)dst_w * dst_h * 3;
    /* 用常驻缓冲（见文件上方 rotate_dst_buf_get 的说明），不再每轮 malloc/free */
    uint8_t *dst_buf = rotate_dst_buf_get(dst_size);
    if (dst_buf == nullptr) {
        ESP_LOGE(TAG, "rotate dst alloc failed, %ux%u", dst_w, dst_h);
        return ESP_ERR_NO_MEM;      /* src 是常驻缓冲，不能在这里释放 */
    }

    const uint8_t *src_buf = (const uint8_t *)src.data;
    for (uint16_t y = 0; y < src_h; y++) {
        for (uint16_t x = 0; x < src_w; x++) {
            size_t src_idx = ((size_t)y * src_w + x) * 3;
            size_t dst_idx = ((size_t)x * dst_w + (dst_w - 1 - y)) * 3;
            dst_buf[dst_idx] = src_buf[src_idx];
            dst_buf[dst_idx + 1] = src_buf[src_idx + 1];
            dst_buf[dst_idx + 2] = src_buf[src_idx + 2];
        }
    }
    /* src 指向常驻解码缓冲，这里不能释放（下次解码会复用它） */

    dl::image::img_t dst = {
        .data = dst_buf,
        .width = dst_w,
        .height = dst_h,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };
    auto rotated_jpg = dl::image::sw_encode_jpeg(dst, 0, 80);
    /* dst_buf 已改常驻，这里不能释放。
     * sw_encode_jpeg 只读输入，输出 JPEG 是它自己新分配的，不需要我们接手。 */
    if (rotated_jpg.data == nullptr || rotated_jpg.data_len == 0) {
        ESP_LOGE(TAG, "rotate encode jpg failed");
        return ESP_FAIL;
    }

    *out_jpg = (uint8_t *)rotated_jpg.data;
    *out_len = rotated_jpg.data_len;
    ESP_LOGI(TAG, "rotate jpg cw90 %ux%u -> %ux%u, %u -> %u bytes",
             src_w, src_h, dst_w, dst_h, (unsigned)jpg_len, (unsigned)*out_len);
    return ESP_OK;
}

extern "C" esp_err_t yolo11_pose_analyze_jpg_named(const uint8_t *jpg_data,
                                                   size_t jpg_len,
                                                   const char *image_name,
                                                   yolo11_pose_frame_result_t *out)
{
#if CONFIG_COCO_POSE_MODEL_IN_SDCARD
    ESP_LOGE(TAG, "SD card model loading is not wired in this project");
    return ESP_ERR_NOT_SUPPORTED;
#else
    const char *name = (image_name != nullptr && image_name[0] != '\0') ? image_name : "-";

    if (out != nullptr) {
        memset(out, 0, sizeof(*out));
        out->status = YOLO11_POSE_FRAME_NO_PERSON;
    }

    if (jpg_data == nullptr || jpg_len == 0) {
        ESP_LOGE(TAG, "%s invalid jpg", name);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = yolo11_pose_init();
    if (ret != ESP_OK) {
        return ret;
    }

    /* 解码到常驻缓冲（与 rotate 共用同一块，二者在本任务里顺序执行） */
    dl::image::img_t img = {};
    if (!decode_jpg_to_resident_rgb888(jpg_data, jpg_len, &img)) {
        ESP_LOGE(TAG, "%s decode jpg failed", name);
        return ESP_FAIL;
    }

    auto &pose_results = s_pose->run(img);
    if (pose_results.empty()) {
        ESP_LOGI(TAG, "%s no pose detected", name);
        return ESP_OK;              /* img.data 是常驻缓冲，不能释放 */
    }

    /* A different person must not silently replace the calibration subject. */
    if (pose_results.size() != 1) {
        if (out != nullptr) { *out = {}; out->status = YOLO11_POSE_FRAME_UNKNOWN; }
        return ESP_OK;
    }
    const auto &res = pose_results.front();
    ESP_LOGI(TAG,
             "%s [score: %f, x1: %d, y1: %d, x2: %d, y2: %d]",
             name,
             res.score,
             res.box[0],
             res.box[1],
             res.box[2],
             res.box[3]);
    log_pose_keypoints(name, res);

    if (out != nullptr) {
        analyze_pose_result(res, (int)img.width, (int)img.height, out);
        ESP_LOGI(TAG,
                 "%s pose metrics yaw=%.3f roll=%.3f shoulder=%.3f lean=%.3f eye=%.3f head=%.3f status=%d err=%d",
                 name,
                 out->yaw,
                 out->roll,
                 out->shoulder_diff,
                 out->lean,
                 out->eye_w_ratio,
                 out->head_down_ratio,
                 (int)out->status,
                 (int)out->err_type);
    }

    return ESP_OK;                  /* img.data 是常驻缓冲，不能释放 */
#endif
}

extern "C" esp_err_t yolo11_pose_run_jpg_named(const uint8_t *jpg_data, size_t jpg_len, const char *image_name)
{
    return yolo11_pose_analyze_jpg_named(jpg_data, jpg_len, image_name, nullptr);
}

/*extern "C" esp_err_t yolo11_pose_decode_jpg_to_rgb565(const uint8_t *jpg_data,
                                                      size_t jpg_len,
                                                      uint8_t **out_rgb565,
                                                      uint16_t *out_w,
                                                      uint16_t *out_h)
{
    if (out_rgb565 == nullptr || out_w == nullptr || out_h == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb565 = nullptr;
    *out_w = 0;
    *out_h = 0;

    dl::image::jpeg_img_t jpeg_img = {.data = (void *)jpg_data, .data_len = jpg_len};
    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (img.data == nullptr) {
        ESP_LOGE(TAG, "decode jpg to rgb565 failed");
        return ESP_FAIL;
    }

    uint16_t w = (uint16_t)img.width;
    uint16_t h = (uint16_t)img.height;
    size_t rgb565_size = (size_t)w * h * 2;
    uint8_t *rgb565 = (uint8_t *)heap_caps_malloc(rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb565 == nullptr) {
        heap_caps_free(img.data);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *src = (uint8_t *)img.data;
    uint16_t *dst = (uint16_t *)rgb565;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t r = src[i * 3 + 0];
        uint8_t g = src[i * 3 + 1];
        uint8_t b = src[i * 3 + 2];
        dst[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    heap_caps_free(img.data);
    *out_rgb565 = rgb565;
    *out_w = w;
    *out_h = h;
    return ESP_OK;
}*/

extern "C" esp_err_t yolo11_pose_decode_jpg_to_rgb565(const uint8_t *jpg_data,
                                                      size_t jpg_len,
                                                      uint8_t **out_rgb565,
                                                      uint16_t *out_w,
                                                      uint16_t *out_h,
                                                      uint16_t target_w,
                                                      uint16_t target_h)
{
    if (out_rgb565 == nullptr || out_w == nullptr || out_h == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_rgb565 = nullptr;
    *out_w = 0;
    *out_h = 0;

    dl::image::jpeg_img_t jpeg_img = {.data = (void *)jpg_data, .data_len = jpg_len};
    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (img.data == nullptr) {
        ESP_LOGE(TAG, "decode jpg to rgb565 failed");
        return ESP_FAIL;
    }

    uint16_t w = (uint16_t)img.width;
    uint16_t h = (uint16_t)img.height;
    uint8_t *src = (uint8_t *)img.data;

    bool do_scale = (target_w > 0 && target_h > 0);
    uint16_t out_width, out_height;
    size_t rgb565_size;

    if (do_scale) {
        out_width = target_w;
        out_height = target_h;
        rgb565_size = (size_t)target_w * target_h * 2;
    } else {
        out_width = w;
        out_height = h;
        rgb565_size = (size_t)w * h * 2;
    }

    uint8_t *rgb565 = (uint8_t *)heap_caps_malloc(rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb565 == nullptr) {
        heap_caps_free(img.data);
        return ESP_ERR_NO_MEM;
    }

    if (do_scale) {
        float scale = (float)target_w / w;
        if ((float)target_h / h < scale) {
            scale = (float)target_h / h;
        }
        uint16_t scaled_w = (uint16_t)(w * scale);
        uint16_t scaled_h = (uint16_t)(h * scale);
        uint16_t offset_x = (target_w - scaled_w) / 2;
        uint16_t offset_y = (target_h - scaled_h) / 2;

        memset(rgb565, 0, rgb565_size);

        for (uint16_t dy = 0; dy < scaled_h; dy++) {
            uint16_t sy = (uint16_t)(dy / scale);
            if (sy >= h) sy = h - 1;
            uint16_t *dst_row = (uint16_t *)(rgb565 + ((offset_y + dy) * target_w + offset_x) * 2);
            uint8_t *src_row = src + sy * w * 3;
            for (uint16_t dx = 0; dx < scaled_w; dx++) {
                uint16_t sx = (uint16_t)(dx / scale);
                if (sx >= w) sx = w - 1;
                uint8_t r = src_row[sx * 3 + 0];
                uint8_t g = src_row[sx * 3 + 1];
                uint8_t b = src_row[sx * 3 + 2];
                dst_row[dx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else {
        uint16_t *dst = (uint16_t *)rgb565;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            uint8_t r = src[i * 3 + 0];
            uint8_t g = src[i * 3 + 1];
            uint8_t b = src[i * 3 + 2];
            dst[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }

    heap_caps_free(img.data);
    *out_rgb565 = rgb565;
    *out_w = out_width;
    *out_h = out_height;
    return ESP_OK;
}

/* 启动早期预分配姿态链路常驻缓冲：远程实现已是懒分配，这里在内存最充裕时
   提前把解码/旋转目标/原生 RGB 三块连续 PSRAM 占下；失败会在首帧自动重试。 */
extern "C" void yolo11_pose_prealloc_buffers(void)
{
    size_t px = (size_t)240 * 320;
    (void)rotate_dst_buf_get(px * 3);
    (void)native_rgb_buf_get(px * 3);
    ESP_LOGI(TAG, "prealloc resident bufs done, psram free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

extern "C" void yolo11_pose_release_decode_buf(void)
{
    if (s_decode_buf != nullptr) {
        size_t freed = s_decode_buf_size;
        heap_caps_free(s_decode_buf);
        s_decode_buf = nullptr;
        s_decode_buf_size = 0;
        ESP_LOGI(TAG, "decode buf released, freed=%u bytes, psram free=%u",
                 (unsigned)freed,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
}
