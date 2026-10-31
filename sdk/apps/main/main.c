#include "qm.h"
#include "qm_work.h"
#include "bsp_io.h"
/* #include "bsp_mpu6050.h" */
#include "bsp_servo.h"
#include "bsp_vibrator.h"
#include "bsp_ws2812.h"
#include "voice.h"
#include "agent_app.h"
#include "ljyd_cloud.h"
#include "ljyd_provision.h"
#include "iot_control.h"
#include "pomodoro.h"
#include "ljyd_course.h"
#if defined(CONFIG_QM_POSE_SUPPORT) && CONFIG_QM_POSE_SUPPORT
#include "yolo11_pose.h"
#include "vision_service.h"
#endif

#if defined(CONFIG_APP_CAMERA_TEST) && CONFIG_APP_CAMERA_TEST
#include "bsp_camera.h"
#endif

#if defined(CONFIG_QM_ALARM_SUPPORT) && CONFIG_QM_ALARM_SUPPORT
#include "app_alarm.h"
#endif

#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
#include "playback.h"
#endif

#include <stdio.h>
#include <stdarg.h>

int qm_vsnprintf(char *str, size_t count, const char *fmt, va_list ap)
{
    return vsnprintf(str, count, fmt, ap);
}

void app_main(void)
{
    qm_main();
}

void qm_application_start(void)
{
#if defined(CONFIG_QM_POSE_SUPPORT) && CONFIG_QM_POSE_SUPPORT
    /* 在开机主任务（内部 SRAM 栈）中预加载 YOLO 模型与常驻图像缓冲，
     * 避免后续在 PSRAM 栈的 cam_pose 任务中首次调用 esp_partition_mmap 触发断言，
     * 同时在网络/声网启动前完成连续内存分配。 */
    yolo11_pose_prealloc_buffers();
    if (yolo11_pose_init() != ESP_OK)
    {
        QM_LOGE("main", "yolo11_pose_init failed");
    }
    if (vision_service_init() != ESP_OK)
    {
        QM_LOGE("main", "vision_service_init failed");
    }
#endif
    /* 舵机：仅注册 CLI，未收到指令前不启动 PWM */
    if (bsp_servo_cli_init() != QM_EOK)
    {
        QM_LOGE("main", "servo CLI registration failed");
    }
    if (qm_work_on_init() != QM_EOK)
    {
        QM_LOGE("main", "qm_work_on_init failed");
        return;
    }
    if (bsp_io_init() != QM_EOK)
    {
        QM_LOGE("main", "io init failed");
        return;
    }
#if defined(CONFIG_APP_CAMERA_TEST) && CONFIG_APP_CAMERA_TEST
    if (bsp_camera_early_init() != ESP_OK)
    {
        QM_LOGE("main", "camera early init failed");
    }
    if (bsp_camera_cli_init() != 0)
    {
        QM_LOGE("main", "camera CLI registration failed");
    }
#endif
    /* MPU6050 默认 GPIO8 与摄像头 DATA0 冲突，当前保持关闭 */
    if (voice_start() != QM_EOK)
    {
        QM_LOGE("main", "audio init failed");
        return;
    }
#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
    if (playback_init() != QM_EOK)
    {
        QM_LOGE("main", "playback init failed");
    }
#endif

    if (ljyd_cloud_init() != QM_EOK)
    {
        QM_LOGE("main", "ljyd_cloud_init failed");
        return;
    }
    if (iot_control_init(ljyd_cloud_get_handle()) != QM_EOK)
    {
        QM_LOGE("main", "iot_control_init failed");
    }
    if (ljyd_course_init(ljyd_cloud_get_handle()) != QM_EOK)
    {
        QM_LOGE("main", "ljyd_course_init failed");
    }
    if (ljyd_provision_init() != QM_EOK)
    {
        QM_LOGE("main", "ljyd_provision_init failed");
    }
    if (agent_app_init(ljyd_cloud_get_handle()) != QM_EOK)
    {
        QM_LOGE("main", "agent_app_init failed");
    }

#if defined(CONFIG_QM_ALARM_SUPPORT) && CONFIG_QM_ALARM_SUPPORT
    /* 闹钟：开机即初始化（不要求已联网）。云端同步与响铃行为注册都在模块内部完成。 */
    if (app_alarm_init(ljyd_cloud_get_handle()) != QM_EOK)
    {
        QM_LOGE("main", "app_alarm_init failed");
    }
#endif

#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
    pomodoro_init();
#endif
}
