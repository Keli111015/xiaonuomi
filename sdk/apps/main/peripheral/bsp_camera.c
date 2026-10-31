#include "qm_config.h"
#include "bsp_camera.h"

#include "cam_debug_streamer.h"
#if defined(CONFIG_QM_POSE_SUPPORT) && CONFIG_QM_POSE_SUPPORT
#include "vision_service.h"
#endif

#include "esp_cam_ctlr_spi.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOG_TAG "bsp_camera"

#define CAM_BUF_NUM             2
#define CAM_I2C_FREQ_HZ         100000
#define CAM_STREAM_PRIO         12
#define CAM_STREAM_CORE         1
#define CAM_DQBUF_POLL_MS       20
#define CAM_LOG_EVERY_FRAMES    150
#define CAM_STREAM_STACK        (6 * 1024)
#define CAM_STACK_CAPS          (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

typedef struct {
    int fd;
    uint8_t *buffer[CAM_BUF_NUM];
    size_t buffer_len[CAM_BUF_NUM];
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    bool stream_on;
} bsp_cam_t;

typedef struct {
    bsp_cam_client_t client;
    uint8_t *dst;
    size_t max_len;
    TaskHandle_t task;
    TickType_t requested_at;
    int state; /* 0: idle, 1: requested/waiting frame, 2: captured/worker running */
} bsp_cam_oneshot_req_t;

static bsp_cam_t s_cam = {
    .fd = -1,
    .width = 240,
    .height = 320,
};

static bool s_available = false;
static bool s_buffers_queued = false;
static volatile bool s_stream_stop_req = false;
static volatile bool s_continuous_stream = false;
static bool s_early_init_once = false;
static uint32_t s_warmup_frames = 0;
static TaskHandle_t s_stream_task = NULL;

static portMUX_TYPE s_req_mux = portMUX_INITIALIZER_UNLOCKED;
static bsp_cam_oneshot_req_t s_req = {0};

static esp_err_t cam_video_init(void);
static esp_err_t cam_setup_buffers(void);
static esp_err_t cam_stream_on(void);
static esp_err_t cam_stream_off(void);
static esp_err_t cam_ensure_stream_task(void);

void bsp_camera_get_resolution(uint32_t *width, uint32_t *height)
{
    if (width) *width = s_cam.width;
    if (height) *height = s_cam.height;
}

<<<<<<< Updated upstream
static void cam_exit_vision_window(void)
{
    if (s_voice_paused_by_cam) {
        s_voice_paused_by_cam = false;
        (void)agent_app_set_resource_mode(QM_FALSE);
    }
}

static void snapshot_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        cam_snapshot_dump(s_snapshot_raw);
        s_snapshot_busy=false;
        cam_exit_vision_window();
    }
}
static esp_err_t snapshot_request(void)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    if (agent_app_is_dialog_busy()) {
        ESP_LOGW(LOG_TAG, "snapshot deferred: voice dialog is active");
        return ESP_ERR_INVALID_STATE;
    }
    if(s_snapshot_busy) return ESP_ERR_INVALID_STATE;
    if(s_snapshot_task==NULL) {
        s_snapshot_raw=heap_caps_malloc((size_t)s_cam.width*s_cam.height*2,
                                       MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(s_snapshot_raw==NULL) return ESP_ERR_NO_MEM;
        if(xTaskCreatePinnedToCoreWithCaps(snapshot_worker,"cam_snapshot",8192,
                NULL,2,&s_snapshot_task,0,CAM_STACK_CAPS)!=pdPASS) {
            heap_caps_free(s_snapshot_raw);s_snapshot_raw=NULL;s_snapshot_task=NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    if (cam_ensure_stream_task() != ESP_OK) {
        return ESP_FAIL;
    }
    cam_enter_vision_window();
    if (!s_continuous_stream) {
        s_oneshot_stream = true;
    }
    s_snapshot_busy=true;
    s_snapshot_requested_at=xTaskGetTickCount();
    s_snap=true;
    xTaskNotifyGive(s_stream_task);
    return ESP_OK;
}


#if CONFIG_QM_POSE_SUPPORT
/* One-shot diagnostic only. Copy before returning the camera DMA buffer;
 * inference runs in a low priority worker so streaming/audio can continue. */
static TaskHandle_t s_pose_task;
static uint8_t *s_pose_raw;
static portMUX_TYPE s_pose_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_pose_state; /* 0 idle, 1 requested, 2 copying/running */
static int s_pose_rotation;
static bool s_pose_calibrate;
static TickType_t s_pose_requested_at;

/* 姿态请求的“投递 → 执行”两级拆分：
 * 调用方可能运行在 FreeRTOS 软件定时器任务里（栈仅 CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH），
 * 那里不允许做 heap_caps_malloc / xTaskCreate。所以请求方只记录参数并通知本任务，
 * 真正的分配与建任务全部在 cam_pose_req 任务上下文完成。 */
static TaskHandle_t s_pose_req_task;
static int s_pose_req_rotation;
static bool s_pose_req_calibrate;
static bool s_pose_req_pending;

static void cam_pose_task(void *arg)
{
    (void)arg;
    int last_rotation = -1;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t *rgb = NULL;
        uint16_t w = 0, h = 0;
        yolo11_pose_frame_result_t result = {0};
        int64_t start = esp_timer_get_time();
        size_t bytes = (size_t)s_cam.width * s_cam.height * 2;
        esp_err_t ret;
        if (last_rotation != s_pose_rotation) {
            yolo11_pose_calibration_reset();
            last_rotation = s_pose_rotation;
        }
        if (s_pose_rotation == 90 || s_pose_rotation == 270) {
            ret = yolo11_pose_uyvy_to_rgb888_cw90(s_pose_raw, bytes,
                    s_cam.width, s_cam.height, &rgb, &w, &h);
        } else {
            ret = yolo11_pose_uyvy_to_rgb888(s_pose_raw, bytes,
                    s_cam.width, s_cam.height, &rgb, &w, &h);
        }
        if (ret == ESP_OK && s_pose_rotation >= 180) {
            size_t pixels = (size_t)w*h;
            for (size_t i=0; i<pixels/2; ++i) {
                for (int c=0; c<3; ++c) {
                    uint8_t tmp=rgb[3*i+c];
                    rgb[3*i+c]=rgb[3*(pixels-1-i)+c];
                    rgb[3*(pixels-1-i)+c]=tmp;
                }
            }
        }
        if (s_pose_calibrate) yolo11_pose_calibration_reset();
        if (ret == ESP_OK) {
            ret = yolo11_pose_analyze_rgb888(rgb, w, h, "camera", &result);
        }
        /* cam calibrate restarts collection; each frame is counted only once. */
        int calibration_count = yolo11_pose_calibration_count();
        ESP_LOGI(LOG_TAG,
                 "POSE_RESULT ret=%s status=%d err=%d elapsed_ms=%u rotate=%d size=%ux%u valid=%d calibrated=%d cal_count=%d",
                 esp_err_to_name(ret), (int)result.status, (int)result.err_type,
                 (unsigned)((esp_timer_get_time() - start) / 1000),
                 s_pose_rotation, (unsigned)w, (unsigned)h, result.valid, result.calibrated, calibration_count);

#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
        morphcube_pose_t pose_to_report = MORPHCUBE_POSE_UNKNOWN;
        if (ret == ESP_OK && result.valid && result.calibrated) {
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
        cam_exit_vision_window();
    }
}

/* 真正的请求执行：分配抓拍缓冲、按需创建推理任务、通知流任务。
 * 只在 cam_pose_req 任务上下文里被调用，绝不从定时器/中断上下文进入。 */
static esp_err_t cam_pose_request_exec(int rotation, bool calibrate)
{
    if (s_pose_task == NULL) {
        if (s_pose_raw == NULL) {
            s_pose_raw = heap_caps_malloc((size_t)s_cam.width * s_cam.height * 2,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_pose_raw == NULL) {
                ESP_LOGE(LOG_TAG, "pose raw buffer alloc failed (%u bytes)",
                         (unsigned)((size_t)s_cam.width * s_cam.height * 2));
                return ESP_ERR_NO_MEM;
            }
        }
        if (xTaskCreatePinnedToCoreWithCaps(cam_pose_task, "cam_pose", 8192,
                NULL, 3, &s_pose_task, 1, CAM_STACK_CAPS) != pdPASS) {
            heap_caps_free(s_pose_raw);
            s_pose_raw = NULL;
            s_pose_task = NULL;
            ESP_LOGE(LOG_TAG, "cam_pose task create failed");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(LOG_TAG, "cam_pose task in PSRAM, stack=8192");
    }
    if (cam_ensure_stream_task() != ESP_OK) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_pose_mux);
    bool busy = s_pose_state != 0;
    if (!busy) {
        s_pose_rotation = rotation;
        s_pose_calibrate = calibrate;
        s_pose_requested_at = xTaskGetTickCount();
        s_pose_state = 1;
    }
    portEXIT_CRITICAL(&s_pose_mux);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    cam_enter_vision_window();
    if (!s_continuous_stream) {
        s_oneshot_stream = true;
    }
    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

static void cam_pose_req_task(void *arg)
{
    (void)arg;
    for (;;) {
        int rotation;
        bool calibrate;
        esp_err_t ret;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        portENTER_CRITICAL(&s_pose_mux);
        rotation = s_pose_req_rotation;
        calibrate = s_pose_req_calibrate;
        portEXIT_CRITICAL(&s_pose_mux);

        ret = cam_pose_request_exec(rotation, calibrate);

        /* pending 在 exec 结束后才清零：这样“已排队未执行”的窗口也算忙，
         * 与原来 s_pose_state==1 的语义一致，避免同一拍被连续触发两次。 */
        portENTER_CRITICAL(&s_pose_mux);
        s_pose_req_pending = false;
        portEXIT_CRITICAL(&s_pose_mux);

        if (ret != ESP_OK) {
            ESP_LOGW(LOG_TAG, "pose request exec failed: %s", esp_err_to_name(ret));
        }
    }
}

static esp_err_t cam_ensure_pose_req_task(void)
{
    if (s_pose_req_task != NULL) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCoreWithCaps(cam_pose_req_task, "cam_pose_req", 4096,
                                        NULL, 3, &s_pose_req_task, 1,
                                        CAM_STACK_CAPS) != pdPASS) {
        s_pose_req_task = NULL;
        ESP_LOGE(LOG_TAG, "pose req task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(LOG_TAG, "pose req task in PSRAM, stack=4096");
    return ESP_OK;
}

/* 对外接口：任意任务上下文（含软件定时器任务）都可安全调用。
 * 这里只做参数记录与一次任务通知，不分配内存、不创建任务。 */
esp_err_t bsp_camera_pose_request(int rotation, bool calibrate)
{
    bool busy;

    if (!s_available) return ESP_ERR_INVALID_STATE;
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
        s_pose_req_rotation = rotation;
        s_pose_req_calibrate = calibrate;
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
#else
esp_err_t bsp_camera_pose_request(int rotation, bool calibrate)
{
    (void)rotation;
    (void)calibrate;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

=======
>>>>>>> Stashed changes
static void cam_pwdn_hold_low(void)
{
    gpio_config_t conf = {
        .pin_bit_mask = 1ULL << BSP_CAM_PWDN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&conf));
    ESP_ERROR_CHECK(gpio_set_level(BSP_CAM_PWDN_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level(BSP_CAM_PWDN_GPIO, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(LOG_TAG, "PWDN GPIO%d pulsed then held low", BSP_CAM_PWDN_GPIO);
}

static void cam_teardown(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (s_cam.fd >= 0 && s_cam.stream_on) {
        ioctl(s_cam.fd, VIDIOC_STREAMOFF, &type);
        s_cam.stream_on = false;
    }
    for (int i = 0; i < CAM_BUF_NUM; i++) {
        if (s_cam.buffer[i] && s_cam.buffer[i] != MAP_FAILED) {
            munmap(s_cam.buffer[i], s_cam.buffer_len[i]);
            s_cam.buffer[i] = NULL;
            s_cam.buffer_len[i] = 0;
        }
    }
    if (s_cam.fd >= 0) {
        close(s_cam.fd);
        s_cam.fd = -1;
    }
}

static esp_err_t cam_video_init(void)
{
    static const esp_video_init_spi_config_t spi_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = BSP_CAM_I2C_PORT,
                .scl_pin = BSP_CAM_SCL_GPIO,
                .sda_pin = BSP_CAM_SDA_GPIO,
            },
            .freq = CAM_I2C_FREQ_HZ,
        },
        .intf = ESP_CAM_CTLR_SPI_CAM_INTF_SPI,
        .io_mode = ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT,
        .spi_port = SPI2_HOST,
        .spi_cs_pin = BSP_CAM_CS_GPIO,
        .spi_sclk_pin = BSP_CAM_SCLK_GPIO,
        .spi_data0_io_pin = BSP_CAM_DATA0_GPIO,
        .spi_data1_io_pin = -1,
        .spi_data2_io_pin = -1,
        .spi_data3_io_pin = -1,
        .reset_pin = BSP_CAM_RESET_GPIO,
        .pwdn_pin = -1,
        .xclk_source = ESP_CAM_SENSOR_XCLK_LEDC,
        .xclk_freq = 0,
        .xclk_pin = BSP_CAM_XCLK_GPIO,
#if CONFIG_CAMERA_XCLK_USE_LEDC
        .xclk_ledc_cfg = {
            .timer = LEDC_TIMER_3,
            .clk_cfg = LEDC_AUTO_CLK,
            .channel = LEDC_CHANNEL_7,
        },
#endif
    };
    const esp_video_init_config_t cam_config = {
        .spi = &spi_config,
    };

    ESP_LOGI(LOG_TAG,
             "pins SDA=%d SCL=%d CS=%d SCLK=%d DATA0=%d PWDN=%d SPI_HOST=%d",
             BSP_CAM_SDA_GPIO, BSP_CAM_SCL_GPIO, BSP_CAM_CS_GPIO,
             BSP_CAM_SCLK_GPIO, BSP_CAM_DATA0_GPIO, BSP_CAM_PWDN_GPIO,
             (int)SPI2_HOST);
    return esp_video_init(&cam_config);
}

static esp_err_t cam_setup_buffers(void)
{
    struct v4l2_format format;
    struct v4l2_requestbuffers req;

    if (s_cam.fd >= 0 && s_cam.buffer[0] != NULL) {
        return ESP_OK;
    }

    if (s_cam.fd < 0) {
        s_cam.fd = open(ESP_VIDEO_SPI_DEVICE_NAME, O_RDWR);
        ESP_RETURN_ON_FALSE(s_cam.fd >= 0, ESP_ERR_NOT_FOUND, LOG_TAG,
                            "open %s failed, errno=%d", ESP_VIDEO_SPI_DEVICE_NAME, errno);
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_G_FMT, &format) == 0 ? ESP_OK : ESP_FAIL,
                        LOG_TAG, "VIDIOC_G_FMT failed, errno=%d", errno);
    s_cam.width = format.fmt.pix.width;
    s_cam.height = format.fmt.pix.height;
    s_cam.pixel_format = format.fmt.pix.pixelformat;
    ESP_LOGI(LOG_TAG, "format %" PRIu32 "x%" PRIu32 " fourcc=0x%08" PRIx32,
             s_cam.width, s_cam.height, s_cam.pixel_format);

    memset(&req, 0, sizeof(req));
    req.count = CAM_BUF_NUM;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_REQBUFS, &req) == 0 ? ESP_OK : ESP_FAIL,
                        LOG_TAG, "VIDIOC_REQBUFS failed, errno=%d", errno);

    for (int i = 0; i < CAM_BUF_NUM; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QUERYBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                            LOG_TAG, "VIDIOC_QUERYBUF[%d] failed, errno=%d", i, errno);
        s_cam.buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               s_cam.fd, buf.m.offset);
        ESP_RETURN_ON_FALSE(s_cam.buffer[i] != MAP_FAILED, ESP_ERR_NO_MEM, LOG_TAG,
                            "mmap[%d] failed, errno=%d", i, errno);
        s_cam.buffer_len[i] = buf.length;
        ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                            LOG_TAG, "VIDIOC_QBUF[%d] failed, errno=%d", i, errno);
    }
    s_buffers_queued = true;

    ESP_LOGI(LOG_TAG, "buffers ready, STREAMON deferred");
    return ESP_OK;
}

static esp_err_t cam_stream_on(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (s_cam.stream_on) {
        return ESP_OK;
    }
    if (!s_buffers_queued) {
        for (int i = 0; i < CAM_BUF_NUM; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                                LOG_TAG, "re-QBUF[%d] failed, errno=%d", i, errno);
        }
        s_buffers_queued = true;
    }
    s_stream_stop_req = false;
    s_warmup_frames = 0;
    ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_STREAMON, &type) == 0 ? ESP_OK : ESP_FAIL,
                        LOG_TAG, "VIDIOC_STREAMON failed, errno=%d", errno);
    s_cam.stream_on = true;
    ESP_LOGI(LOG_TAG, "stream on, %s", ESP_VIDEO_SPI_DEVICE_NAME);
    return ESP_OK;
}

static esp_err_t cam_stream_off(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (!s_cam.stream_on) {
        s_stream_stop_req = false;
        return ESP_OK;
    }
    if (ioctl(s_cam.fd, VIDIOC_STREAMOFF, &type) != 0) {
        ESP_LOGE(LOG_TAG, "VIDIOC_STREAMOFF failed, errno=%d", errno);
        return ESP_FAIL;
    }
    s_cam.stream_on = false;
    s_buffers_queued = false;
    s_stream_stop_req = false;

    portENTER_CRITICAL(&s_req_mux);
    if (s_req.state == 1) {
        s_req.state = 0;
    }
    portEXIT_CRITICAL(&s_req_mux);

    ESP_LOGI(LOG_TAG, "stream off, %s", ESP_VIDEO_SPI_DEVICE_NAME);
    return ESP_OK;
}

static void cam_stream_task(void *arg)
{
    uint32_t frames = 0;
    uint32_t good = 0;
    uint32_t empty = 0;
    TickType_t t0 = xTaskGetTickCount();
    const uint32_t expect = s_cam.width * s_cam.height * 2;

    (void)arg;
    ESP_LOGI(LOG_TAG, "stream task on core %d", (int)xPortGetCoreID());

    for (;;) {
        struct v4l2_buffer buf;

        if (!s_cam.stream_on) {
            bool req_pending = false;
            bool worker_busy = false;

            portENTER_CRITICAL(&s_req_mux);
            req_pending = (s_req.state == 1);
            worker_busy = (s_req.state == 2);
            portEXIT_CRITICAL(&s_req_mux);

            bool need_stream = (s_continuous_stream || req_pending);

            if (need_stream && !s_stream_stop_req && !worker_busy) {
                if (cam_stream_on() == ESP_OK) {
                    continue;
                }
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        if (s_stream_stop_req) {
            (void)cam_stream_off();
            continue;
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s_cam.fd, VIDIOC_DQBUF, &buf) != 0) {
            if (errno != EAGAIN && errno != EINTR) {
                ESP_LOGE(LOG_TAG, "VIDIOC_DQBUF failed, errno=%d", errno);
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(CAM_DQBUF_POLL_MS));
            }
            continue;
        }

        frames++;
        if (buf.bytesused == expect) {
            good++;
            s_warmup_frames++;
        } else {
            empty++;
        }

        if ((frames % CAM_LOG_EVERY_FRAMES) == 0) {
            TickType_t now = xTaskGetTickCount();
            uint32_t ms = (uint32_t)((now - t0) * portTICK_PERIOD_MS);
            const uint8_t *raw = s_cam.buffer[buf.index];
            ESP_LOGI(LOG_TAG,
                     "got %u frames good=%u empty=%u last_bytes=%u dt=%u ms "
                     "raw[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x",
                     (unsigned)frames, (unsigned)good, (unsigned)empty,
                     (unsigned)buf.bytesused, (unsigned)ms,
                     raw ? raw[0] : 0, raw ? raw[1] : 0, raw ? raw[2] : 0,
                     raw ? raw[3] : 0, raw ? raw[4] : 0, raw ? raw[5] : 0,
                     raw ? raw[6] : 0, raw ? raw[7] : 0);
            t0 = now;
        }

        bool frame_ready_for_oneshot = (s_warmup_frames >= 2);
        bool stop_after_frame = false;
        TaskHandle_t notify_target = NULL;

        portENTER_CRITICAL(&s_req_mux);
        if (s_req.state == 1) {
            if (buf.bytesused == expect && frame_ready_for_oneshot) {
                if (s_req.dst && s_req.max_len >= expect) {
                    memcpy(s_req.dst, s_cam.buffer[buf.index], expect);
                }
                s_req.state = 2;
                notify_target = s_req.task;
                stop_after_frame = true;
            } else if ((TickType_t)(xTaskGetTickCount() - s_req.requested_at) >= pdMS_TO_TICKS(5000)) {
                s_req.state = 0;
                stop_after_frame = true;
                ESP_LOGW(LOG_TAG, "oneshot request timed out (client=%d)", (int)s_req.client);
            }
        }
        portEXIT_CRITICAL(&s_req_mux);

        if (!s_continuous_stream && s_req.state == 0) {
            stop_after_frame = true;
        }

        if (ioctl(s_cam.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(LOG_TAG, "VIDIOC_QBUF return failed, errno=%d", errno);
        }

        /* 抓拍完成后立即停流以完全释放 SPI/DMA 资源给 CPU/PSRAM 推理使用 */
        if (stop_after_frame || s_stream_stop_req) {
            (void)cam_stream_off();
        }

        if (notify_target != NULL) {
            xTaskNotifyGive(notify_target);
        }
    }
}

static esp_err_t cam_ensure_stream_task(void)
{
    if (!s_available) {
        ESP_LOGE(LOG_TAG, "stream task requested before early_init");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_task != NULL) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCoreWithCaps(cam_stream_task, "cam_stream", CAM_STREAM_STACK,
                                        NULL, CAM_STREAM_PRIO, &s_stream_task,
                                        CAM_STREAM_CORE, CAM_STACK_CAPS) != pdPASS) {
        s_stream_task = NULL;
        ESP_LOGE(LOG_TAG, "camera task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(LOG_TAG, "stream task in PSRAM, stack=%d", CAM_STREAM_STACK);
    return ESP_OK;
}

esp_err_t bsp_camera_early_init(void)
{
    if (!s_early_init_once) {
        s_early_init_once = true;
        /* Route stdio/ESP logs through UART driver */
        uart_vfs_dev_use_driver(0);
        cam_pwdn_hold_low();
    }

    if (s_available) {
        return ESP_OK;
    }

    esp_err_t ret = cam_video_init();
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "cam_video_init failed, ret=%d", ret);
        return ret;
    }

    ret = cam_setup_buffers();
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "cam_setup_buffers failed, ret=%d", ret);
        cam_teardown();
        return ret;
    }

    s_available = true;
    (void)cam_ensure_stream_task();
    ESP_LOGI(LOG_TAG, "early init done: buffers ready in PSRAM");
    return ESP_OK;
}

esp_err_t bsp_camera_stream_start(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    s_continuous_stream = true;
    s_stream_stop_req = false;
    if (cam_ensure_stream_task() != ESP_OK) {
        return ESP_FAIL;
    }
    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

esp_err_t bsp_camera_stream_stop(void)
{
    s_continuous_stream = false;
    s_stream_stop_req = true;
    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

esp_err_t bsp_camera_start(void)
{
    if (bsp_camera_early_init() != ESP_OK) {
        return ESP_FAIL;
    }
    return bsp_camera_stream_start();
}

bool bsp_camera_is_available(void)
{
    return s_available;
}

bool bsp_camera_is_streaming(void)
{
    return s_cam.stream_on;
}

esp_err_t bsp_camera_request_frame(bsp_cam_client_t client, uint8_t *dst, size_t dst_size, TaskHandle_t notify_task)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dst == NULL || notify_task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.state != 0) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        s_req.client = client;
        s_req.dst = dst;
        s_req.max_len = dst_size;
        s_req.task = notify_task;
        s_req.requested_at = xTaskGetTickCount();
        s_req.state = 1;
    }
    portEXIT_CRITICAL(&s_req_mux);

    if (ret != ESP_OK) {
        return ret;
    }

    if (cam_ensure_stream_task() != ESP_OK) {
        portENTER_CRITICAL(&s_req_mux);
        s_req.state = 0;
        portEXIT_CRITICAL(&s_req_mux);
        return ESP_FAIL;
    }

    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

void bsp_camera_release_frame(bsp_cam_client_t client)
{
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.client == client) {
        s_req.state = 0;
        s_req.client = BSP_CAM_CLIENT_NONE;
    }
    portEXIT_CRITICAL(&s_req_mux);

    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
}

/* 兼容性向前转发 */
esp_err_t bsp_camera_pose_request(int rotation, bool calibrate)
{
#if defined(CONFIG_QM_POSE_SUPPORT) && CONFIG_QM_POSE_SUPPORT
    return vision_service_pose_request(rotation, calibrate);
#else
    (void)rotation;
    (void)calibrate;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

int bsp_camera_cli_init(void)
{
    return cam_debug_cli_init();
}
