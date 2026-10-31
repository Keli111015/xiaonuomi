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
#include "esp_video_ioctl.h"
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
#include <sys/time.h>
#include <unistd.h>

#define LOG_TAG "bsp_camera"

#define CAM_BUF_NUM             2
#define CAM_I2C_FREQ_HZ         100000
#define CAM_STREAM_PRIO         12
#define CAM_STREAM_CORE         1
#define CAM_DQBUF_POLL_MS       20
#define CAM_DQBUF_EPERM_RETRY_MAX 3
#define CAM_STREAM_RECOVERY_MAX 2
#define CAM_EMPTY_FRAME_RECOVERY_THRESHOLD 8
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
    esp_err_t result;
    int state; /* 0: idle, 1: requested/waiting frame, 2: captured/worker running */
} bsp_cam_oneshot_req_t;

static bsp_cam_t s_cam = {
    .fd = -1,
    .width = 240,
    .height = 320,
};

static bool s_available = false;
static uint32_t s_queued_buffers;
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

static bool cam_stream_desired(void)
{
    bool desired;

    portENTER_CRITICAL(&s_req_mux);
    desired = s_continuous_stream || s_req.state == 1;
    if (s_stream_stop_req) desired = false;
    portEXIT_CRITICAL(&s_req_mux);
    return desired;
}

/* All V4L2 operations remain owned by cam_stream_task. A recovery deliberately
 * rebuilds the queue instead of reusing a buffer index returned by a failed
 * DQBUF, because that index has no valid ownership on error. */
static esp_err_t cam_stream_recover(void)
{
    esp_err_t ret;

    ret = cam_stream_off();
    if (ret != ESP_OK) return ret;
    if (!cam_stream_desired()) return ESP_OK;

    ret = cam_stream_on();
    if (ret == ESP_OK) {
        ESP_LOGI(LOG_TAG, "stream recovery complete");
    }
    return ret;
}

void bsp_camera_get_resolution(uint32_t *width, uint32_t *height)
{
    if (width) *width = s_cam.width;
    if (height) *height = s_cam.height;
}

/* Capture ownership lives here; snapshot and inference workers live in their
 * respective services. Do not duplicate their state or voice policy here. */
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
    s_queued_buffers = 0;
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
    ESP_RETURN_ON_FALSE(s_cam.width > 0 && s_cam.height > 0 &&
                        s_cam.pixel_format == V4L2_PIX_FMT_UYVY,
                        ESP_ERR_NOT_SUPPORTED, LOG_TAG, "expected nonempty UYVY frames");

    /* The ESP video VFS ignores O_NONBLOCK. Bound the actual driver's dequeue
     * wait so stop/capture deadlines are checked even when the sensor stalls. */
    struct timeval dq_timeout = {
        .tv_sec = 0,
        .tv_usec = CAM_DQBUF_POLL_MS * 1000,
    };
    ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_S_DQBUF_TIMEOUT, &dq_timeout) == 0 ? ESP_OK : ESP_FAIL,
                        LOG_TAG, "setting DQBUF timeout failed, errno=%d", errno);

    memset(&req, 0, sizeof(req));
    req.count = CAM_BUF_NUM;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_REQBUFS, &req) == 0 ? ESP_OK : ESP_FAIL,
                        LOG_TAG, "VIDIOC_REQBUFS failed, errno=%d", errno);
    ESP_RETURN_ON_FALSE(req.count >= CAM_BUF_NUM, ESP_ERR_NO_MEM, LOG_TAG,
                        "insufficient capture buffers");

    for (int i = 0; i < CAM_BUF_NUM; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QUERYBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                            LOG_TAG, "VIDIOC_QUERYBUF[%d] failed, errno=%d", i, errno);
        ESP_RETURN_ON_FALSE(buf.length >= (size_t)s_cam.width * s_cam.height * 2,
                            ESP_ERR_INVALID_SIZE, LOG_TAG, "capture buffer too small");
        s_cam.buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               s_cam.fd, buf.m.offset);
        ESP_RETURN_ON_FALSE(s_cam.buffer[i] != MAP_FAILED, ESP_ERR_NO_MEM, LOG_TAG,
                            "mmap[%d] failed, errno=%d", i, errno);
        s_cam.buffer_len[i] = buf.length;
        ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                            LOG_TAG, "VIDIOC_QBUF[%d] failed, errno=%d", i, errno);
        s_queued_buffers |= 1U << i;
    }

    ESP_LOGI(LOG_TAG, "buffers ready, STREAMON deferred");
    return ESP_OK;
}

static esp_err_t cam_stream_on(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (s_cam.stream_on) {
        return ESP_OK;
    }
    if (s_queued_buffers != (1U << CAM_BUF_NUM) - 1U) {
        for (int i = 0; i < CAM_BUF_NUM; i++) {
            if (s_queued_buffers & (1U << i)) continue;
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            ESP_RETURN_ON_ERROR(ioctl(s_cam.fd, VIDIOC_QBUF, &buf) == 0 ? ESP_OK : ESP_FAIL,
                                LOG_TAG, "re-QBUF[%d] failed, errno=%d", i, errno);
            s_queued_buffers |= 1U << i;
        }
    }
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
        return ESP_OK;
    }
    if (ioctl(s_cam.fd, VIDIOC_STREAMOFF, &type) != 0) {
        ESP_LOGE(LOG_TAG, "VIDIOC_STREAMOFF failed, errno=%d", errno);
        return ESP_FAIL;
    }
    s_cam.stream_on = false;
    s_queued_buffers = 0;

    ESP_LOGI(LOG_TAG, "stream off, %s", ESP_VIDEO_SPI_DEVICE_NAME);
    return ESP_OK;
}

/* Success and failure both retain ownership until the notified client releases
 * it. A failed request must never silently strand a worker waiting for a frame. */
static void cam_complete_request(esp_err_t result)
{
    TaskHandle_t target = NULL;
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.state == 1) {
        s_req.result = result;
        s_req.state = 2;
        target = s_req.task;
    }
    portEXIT_CRITICAL(&s_req_mux);
    if (target != NULL) xTaskNotifyGive(target);
}

static void cam_stream_task(void *arg)
{
    uint32_t frames = 0;
    uint32_t good = 0;
    uint32_t empty = 0;
    uint8_t dqbuf_eperm_count = 0;
    uint8_t recovery_count = 0;
    uint8_t empty_frame_count = 0;
    TickType_t t0 = xTaskGetTickCount();
    const uint32_t expect = s_cam.width * s_cam.height * 2;

    (void)arg;
    ESP_LOGI(LOG_TAG, "stream task on core %d", (int)xPortGetCoreID());

    for (;;) {
        struct v4l2_buffer buf;
        bool stop_requested;
        bool req_pending;
        bool worker_busy;
        bool continuous;
        bool timed_out;

        portENTER_CRITICAL(&s_req_mux);
        stop_requested = s_stream_stop_req;
        req_pending = (s_req.state == 1);
        worker_busy = (s_req.state == 2);
        continuous = s_continuous_stream;
        timed_out = req_pending &&
            (TickType_t)(xTaskGetTickCount() - s_req.requested_at) >= pdMS_TO_TICKS(5000);
        portEXIT_CRITICAL(&s_req_mux);

        /* A start may supersede a queued stop before this task runs. Treat the
         * latest desired state as authoritative so an old stop cannot tear
         * down a newly requested stream. */
        if ((stop_requested && !continuous) || timed_out) {
            esp_err_t off_ret = cam_stream_off();
            cam_complete_request(timed_out ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE);
            if (stop_requested && !continuous && off_ret == ESP_OK) {
                portENTER_CRITICAL(&s_req_mux);
                if (!s_continuous_stream) {
                    s_stream_stop_req = false; /* Consume the stop only if no newer start won. */
                }
                portEXIT_CRITICAL(&s_req_mux);
            }
            if (off_ret != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if ((worker_busy || (!continuous && !req_pending)) && s_cam.stream_on) {
            /* Retry a failed STREAMOFF before taking more frames during inference. */
            if (cam_stream_off() != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!s_cam.stream_on) {
            bool need_stream = (continuous || req_pending);

            if (need_stream && !worker_busy) {
                if (cam_stream_on() == ESP_OK) {
                    dqbuf_eperm_count = 0;
                    empty_frame_count = 0;
                    recovery_count = 0;
                    continue;
                }
                cam_complete_request(ESP_FAIL);
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s_cam.fd, VIDIOC_DQBUF, &buf) != 0) {
            int dq_errno = errno;
            if (dq_errno == EPERM) {
                dqbuf_eperm_count++;
                if (dqbuf_eperm_count < CAM_DQBUF_EPERM_RETRY_MAX) {
                    ESP_LOGW(LOG_TAG, "VIDIOC_DQBUF EPERM retry=%u/%u",
                             (unsigned)dqbuf_eperm_count,
                             (unsigned)CAM_DQBUF_EPERM_RETRY_MAX);
                    vTaskDelay(pdMS_TO_TICKS(CAM_DQBUF_POLL_MS));
                    continue;
                }

                if (recovery_count >= CAM_STREAM_RECOVERY_MAX) {
                    ESP_LOGE(LOG_TAG, "VIDIOC_DQBUF EPERM recovery exhausted=%u",
                             (unsigned)recovery_count);
                    cam_complete_request(ESP_FAIL);
                    (void)cam_stream_off();
                    dqbuf_eperm_count = 0;
                    continue;
                }

                recovery_count++;
                ESP_LOGW(LOG_TAG, "VIDIOC_DQBUF EPERM: recover stream attempt=%u/%u",
                         (unsigned)recovery_count,
                         (unsigned)CAM_STREAM_RECOVERY_MAX);
                if (cam_stream_recover() != ESP_OK) {
                    ESP_LOGE(LOG_TAG, "stream recovery failed attempt=%u",
                             (unsigned)recovery_count);
                    cam_complete_request(ESP_FAIL);
                    (void)cam_stream_off();
                }
                dqbuf_eperm_count = 0;
                continue;
            }
            if (dq_errno != EAGAIN && dq_errno != EINTR && dq_errno != ETIMEDOUT) {
                ESP_LOGE(LOG_TAG, "VIDIOC_DQBUF failed, errno=%d", dq_errno);
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(CAM_DQBUF_POLL_MS));
            }
            continue;
        }
        dqbuf_eperm_count = 0;

        if (buf.index >= CAM_BUF_NUM || s_cam.buffer[buf.index] == NULL ||
            buf.bytesused > s_cam.buffer_len[buf.index]) {
            ESP_LOGE(LOG_TAG, "invalid capture buffer index=%u bytes=%u",
                     (unsigned)buf.index, (unsigned)buf.bytesused);
            (void)cam_stream_off();
            cam_complete_request(ESP_FAIL);
            continue;
        }
        s_queued_buffers &= ~(1U << buf.index);
        frames++;
        if (buf.bytesused == expect) {
            good++;
            s_warmup_frames++;
            recovery_count = 0;
            empty_frame_count = 0;
        } else {
            empty++;
            if (empty_frame_count < 0xffU) empty_frame_count++;
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
        bool recover_after_qbuf = empty_frame_count >= CAM_EMPTY_FRAME_RECOVERY_THRESHOLD;
        TaskHandle_t notify_target = NULL;
        uint8_t *copy_dst = NULL;

        portENTER_CRITICAL(&s_req_mux);
        if (s_req.state == 1) {
            if (s_stream_stop_req ||
                (TickType_t)(xTaskGetTickCount() - s_req.requested_at) >= pdMS_TO_TICKS(5000)) {
                s_req.result = s_stream_stop_req ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
                s_req.state = 2;
                notify_target = s_req.task;
                stop_after_frame = true;
            } else if (buf.bytesused == expect && frame_ready_for_oneshot) {
                if (s_req.dst && s_req.max_len >= expect) {
                    copy_dst = s_req.dst;
                    s_req.result = ESP_OK;
                } else {
                    s_req.result = ESP_ERR_INVALID_SIZE;
                }
                s_req.state = 2;
                notify_target = s_req.task;
                stop_after_frame = true;
            }
        }
        if (!s_continuous_stream && s_req.state == 0) stop_after_frame = true;
        portEXIT_CRITICAL(&s_req_mux);

        /* The request owns dst until its worker is notified and releases it.
         * Copy a full frame outside the spinlock (interrupts stay enabled). */
        if (copy_dst != NULL) memcpy(copy_dst, s_cam.buffer[buf.index], expect);

        if (ioctl(s_cam.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(LOG_TAG, "VIDIOC_QBUF return failed, errno=%d", errno);
            stop_after_frame = true; /* Rebuild the queue before more capture. */
            cam_complete_request(ESP_FAIL);
        } else {
            s_queued_buffers |= 1U << buf.index;
            if (recover_after_qbuf && !stop_after_frame) {
                ESP_LOGW(LOG_TAG,
                         "camera invalid frame streak=%u bytes=%u/%u, recover after QBUF",
                         (unsigned)empty_frame_count,
                         (unsigned)buf.bytesused,
                         (unsigned)expect);
                if (recovery_count >= CAM_STREAM_RECOVERY_MAX) {
                    ESP_LOGE(LOG_TAG,
                             "camera invalid frame recovery exhausted=%u",
                             (unsigned)recovery_count);
                    cam_complete_request(ESP_FAIL);
                    (void)cam_stream_off();
                } else {
                    recovery_count++;
                    ESP_LOGW(LOG_TAG,
                             "camera invalid frame: recover stream attempt=%u/%u",
                             (unsigned)recovery_count,
                             (unsigned)CAM_STREAM_RECOVERY_MAX);
                    if (cam_stream_recover() != ESP_OK) {
                        ESP_LOGE(LOG_TAG, "invalid frame stream recovery failed attempt=%u",
                                 (unsigned)recovery_count);
                        cam_complete_request(ESP_FAIL);
                        (void)cam_stream_off();
                    }
                }
                empty_frame_count = 0;
            }
        }

        /* 抓拍完成后立即停流以完全释放 SPI/DMA 资源给 CPU/PSRAM 推理使用 */
        if (stop_after_frame) {
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
        return cam_ensure_stream_task();
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
    ret = cam_ensure_stream_task();
    if (ret != ESP_OK) return ret;
    ESP_LOGI(LOG_TAG, "early init done: buffers ready in PSRAM");
    return ESP_OK;
}

esp_err_t bsp_camera_stream_start(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cam_ensure_stream_task() != ESP_OK) {
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_req_mux);
    s_continuous_stream = true;
    s_stream_stop_req = false;
    portEXIT_CRITICAL(&s_req_mux);
    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

esp_err_t bsp_camera_stream_stop(void)
{
    portENTER_CRITICAL(&s_req_mux);
    s_continuous_stream = false;
    s_stream_stop_req = true;
    portEXIT_CRITICAL(&s_req_mux);
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
    if ((client != BSP_CAM_CLIENT_POSE && client != BSP_CAM_CLIENT_SNAP) ||
        dst == NULL || notify_task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dst_size < (size_t)s_cam.width * s_cam.height * 2) return ESP_ERR_INVALID_SIZE;
    if (cam_ensure_stream_task() != ESP_OK) return ESP_FAIL;

    esp_err_t ret = ESP_OK;
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.state != 0 || s_stream_stop_req) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        s_req.client = client;
        s_req.dst = dst;
        s_req.max_len = dst_size;
        s_req.task = notify_task;
        s_req.requested_at = xTaskGetTickCount();
        s_req.result = ESP_ERR_INVALID_STATE;
        s_req.state = 1;
    }
    portEXIT_CRITICAL(&s_req_mux);

    if (ret != ESP_OK) {
        return ret;
    }

    if (s_stream_task != NULL) {
        xTaskNotifyGive(s_stream_task);
    }
    return ESP_OK;
}

esp_err_t bsp_camera_frame_result(bsp_cam_client_t client)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.client == client && s_req.state == 2) result = s_req.result;
    portEXIT_CRITICAL(&s_req_mux);
    return result;
}

void bsp_camera_release_frame(bsp_cam_client_t client)
{
    portENTER_CRITICAL(&s_req_mux);
    if (s_req.client == client && s_req.state == 2) {
        memset(&s_req, 0, sizeof(s_req));
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
