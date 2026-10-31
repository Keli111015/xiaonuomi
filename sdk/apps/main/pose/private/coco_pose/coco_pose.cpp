#include "coco_pose.hpp"
#include "esp_log.h"
#include <filesystem>
//#include <exception>
#if CONFIG_COCO_POSE_MODEL_IN_FLASH_RODATA
extern const uint8_t coco_pose_espdl[] asm("_binary_coco_pose_espdl_start");
static const char *path = (const char *)coco_pose_espdl;
#elif CONFIG_COCO_POSE_MODEL_IN_FLASH_PARTITION
static const char *path = "coco_pose";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif
namespace coco_pose {
  
Yolo11nPose::Yolo11nPose(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_COCO_POSE_MODEL_IN_SDCARD
    bool param_copy = true;
    size_t spiram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (spiram_total < 9 * 1024 * 1024 || spiram_free < 5 * 1024 * 1024) {
        param_copy = false;
    }
    ESP_LOGI("Yolo11nPose", "loading model %s, spiram_free=%u, param_copy=%d",
             model_name, (unsigned)spiram_free, (int)param_copy);
    m_model = new dl::Model(path,
                            model_name,
                            static_cast<fbs::model_location_type_t>(CONFIG_COCO_POSE_MODEL_LOCATION),
                            0,
                            dl::MEMORY_MANAGER_GREEDY,
                            nullptr,
                            param_copy);
#else
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_COCO_POSE_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path, fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
#if CONFIG_IDF_TARGET_ESP32P4
    m_image_preprocessor = new dl::image::ImagePreprocessor(
        m_model, {0, 0, 0}, {255, 255, 255}, dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#else
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
#endif
    m_image_preprocessor->enable_letterbox({114, 114, 114});
    m_postprocessor = new dl::detect::yolo11posePostProcessor(
        m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}
/*Yolo11nPose::Yolo11nPose(const char *model_name, float score_thr, float nms_thr)
{
    // 先初始化所有指针为空，异常时可以安全delete
    m_model = nullptr;
    m_image_preprocessor = nullptr;
    m_postprocessor = nullptr;
    try {
#if !CONFIG_COCO_POSE_MODEL_IN_SDCARD
        bool param_copy = true;
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) < 1024 * 1024 * 9) {
            param_copy = false;
        }
        m_model = new dl::Model(path,
                                model_name,
                                static_cast<fbs::model_location_type_t>(CONFIG_COCO_POSE_MODEL_LOCATION),
                                0,
                                dl::MEMORY_MANAGER_GREEDY,
                                nullptr,
                                param_copy);
#else
        auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_COCO_POSE_MODEL_SDCARD_DIR / model_name;
        m_model = new dl::Model(sd_path, fbs::MODEL_LOCATION_IN_SDCARD);
#endif
        if (!m_model) {
            ESP_LOGE("Yolo11nPose", "Failed to create dl::Model for %s", model_name);
            return;
        }
        m_model->minimize();
#if CONFIG_IDF_TARGET_ESP32P4
        m_image_preprocessor = new dl::image::ImagePreprocessor(
            m_model, {0, 0, 0}, {255, 255, 255}, dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN);
#else
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
#endif
        m_image_preprocessor->enable_letterbox({114, 114, 114});
        m_postprocessor = new dl::detect::yolo11posePostProcessor(
            m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
    } catch (const std::exception& e) {
        // 捕获所有标准异常，打印错误信息
        ESP_LOGE("Yolo11nPose", "Exception while loading model %s: %s", model_name, e.what());
        if (m_postprocessor) { delete m_postprocessor; m_postprocessor = nullptr; }
        if (m_image_preprocessor) { delete m_image_preprocessor; m_image_preprocessor = nullptr; }
        if (m_model) { delete m_model; m_model = nullptr; }
    } catch (...) {
        // 捕获所有其他未知异常
        ESP_LOGE("Yolo11nPose", "Unknown exception while loading model %s", model_name);
        if (m_postprocessor) { delete m_postprocessor; m_postprocessor = nullptr; }
        if (m_image_preprocessor) { delete m_image_preprocessor; m_image_preprocessor = nullptr; }
        if (m_model) { delete m_model; m_model = nullptr; }
    }
}*/

} // namespace coco_pose

COCOPose::COCOPose(model_type_t model_type, bool lazy_load) : m_model_type(model_type)
{
    switch (model_type) {
    case model_type_t::YOLO11N_POSE_S8_V1:
    case model_type_t::YOLO11N_POSE_S8_V2:
        m_score_thr[0] = coco_pose::Yolo11nPose::default_score_thr;
        m_nms_thr[0] = coco_pose::Yolo11nPose::default_nms_thr;
        break;
    }
    if (lazy_load) {
        m_model = nullptr;
    } else {
        load_model();
    }
}

void COCOPose::load_model()
{
    switch (m_model_type) {
    case model_type_t::YOLO11N_POSE_S8_V1:
#if CONFIG_FLASH_COCO_POSE_YOLO11N_POSE_S8_V1 || CONFIG_COCO_POSE_MODEL_IN_SDCARD
        m_model = new coco_pose::Yolo11nPose("coco_pose_yolo11n_pose_s8_v1.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("coco_pose", "coco_pose_yolo11n_pose_s8_v1 is not selected in menuconfig.");
#endif
        break;
    case model_type_t::YOLO11N_POSE_S8_V2:
#if CONFIG_FLASH_COCO_POSE_YOLO11N_POSE_S8_V2 || CONFIG_COCO_POSE_MODEL_IN_SDCARD
        m_model = new coco_pose::Yolo11nPose("coco_pose_yolo11n_pose_s8_v2.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("coco_pose", "coco_pose_yolo11n_pose_s8_v2 is not selected in menuconfig.");
#endif
        break;
    }
}
