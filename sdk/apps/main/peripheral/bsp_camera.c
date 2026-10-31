#include "bsp_camera.h"

#include "esp_cam_ctlr_spi.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "qm_config.h"
#include "qm.h"
#include "qm_cli.h"
#include "agent_app.h"
#include "esp_jpeg_enc.h"
#include "esp_timer.h"
#if CONFIG_QM_POSE_SUPPORT
#include "yolo11_pose.h"
#include "morphcube_study_runtime.h"
#endif

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

static bsp_cam_t s_cam = {
    .fd = -1,
};
static bool s_available;
static bool s_video_ready;
static bool s_buffers_queued;
static volatile bool s_stream_stop_req;
static volatile bool s_continuous_stream;
static volatile bool s_oneshot_stream;
static volatile bool s_voice_paused_by_cam;
static uint32_t s_warmup_frames;
static TaskHandle_t s_stream_task;
static volatile bool s_snap;
static TaskHandle_t s_snapshot_task;
static uint8_t *s_snapshot_raw;
static volatile bool s_snapshot_busy;
static TickType_t s_snapshot_requested_at;
static void cam_snapshot_dump(const uint8_t *frame);

static void cam_enter_vision_window(void)
{
    if (!s_voice_paused_by_cam) {
        s_voice_paused_by_cam = true;
        (void)agent_app_set_resource_mode(QM_TRUE);
        ESP_LOGI(LOG_TAG, "linear control: voice upload paused for vision");
    }
}

static void cam_exit_vision_window(void)
{
    if (s_voice_paused_by_cam) {
        s_voice_paused_by_cam = false;
        (void)agent_app_set_resource_mode(QM_FALSE);
        ESP_LOGI(LOG_TAG, "linear control: voice upload resumed after vision");
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
    cam_enter_vision_window();
    if (!s_cam.stream_on && !s_continuous_stream) {
        if (bsp_camera_stream_start() != ESP_OK) {
            cam_exit_vision_window();
            return ESP_FAIL;
        }
        s_continuous_stream = false;
        s_oneshot_stream = true;
    }
    s_snapshot_busy=true;
    s_snapshot_requested_at=xTaskGetTickCount();
    s_snap=true;
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
        cam_exit_vision_window();
    }
}

esp_err_t bsp_camera_pose_request(int rotation, bool calibrate)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    if (agent_app_is_dialog_busy()) {
        ESP_LOGW(LOG_TAG, "pose request deferred: voice dialog is active");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pose_task == NULL) {
        s_pose_raw = heap_caps_malloc((size_t)s_cam.width * s_cam.height * 2,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_pose_raw == NULL) return ESP_ERR_NO_MEM;
        if (xTaskCreatePinnedToCoreWithCaps(cam_pose_task, "cam_pose", 8192,
                NULL, 5, &s_pose_task, 1, CAM_STACK_CAPS) != pdPASS) {
            heap_caps_free(s_pose_raw);
            s_pose_raw = NULL;
            s_pose_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    portENTER_CRITICAL(&s_pose_mux);
    bool busy = s_pose_state != 0;
    portEXIT_CRITICAL(&s_pose_mux);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    cam_enter_vision_window();
    if (!s_cam.stream_on && !s_continuous_stream) {
        if (bsp_camera_stream_start() != ESP_OK) {
            cam_exit_vision_window();
            return ESP_FAIL;
        }
        s_continuous_stream = false;
        s_oneshot_stream = true;
    }

    portENTER_CRITICAL(&s_pose_mux);
    s_pose_rotation = rotation;
    s_pose_calibrate = calibrate;
    s_pose_requested_at = xTaskGetTickCount();
    s_pose_state = 1;
    portEXIT_CRITICAL(&s_pose_mux);
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
    s_oneshot_stream = false;

#if CONFIG_QM_POSE_SUPPORT
    bool abort_pending_pose = false;
    portENTER_CRITICAL(&s_pose_mux);
    if (s_pose_state == 1) {
        s_pose_state = 0;
        abort_pending_pose = true;
    }
    portEXIT_CRITICAL(&s_pose_mux);
    if (abort_pending_pose) {
        cam_exit_vision_window();
    }
#endif
    if (s_snap) {
        s_snap = false;
        s_snapshot_busy = false;
        cam_exit_vision_window();
    }

    ESP_LOGI(LOG_TAG, "stream off, %s", ESP_VIDEO_SPI_DEVICE_NAME);
    return ESP_OK;
}

/* JPEG-compress one UYVY frame and dump it over the debug UART as:
 *   JPEG <n>\r\n<raw jpeg bytes>\r\nJPEG_END\r\n
 * The PC side reads <n> bytes after the marker and saves them as a .jpg. */
static void cam_snapshot_dump(const uint8_t *frame)
{
    jpeg_enc_handle_t enc = NULL;
    const size_t in_size = s_cam.width * s_cam.height * 2;
    const size_t out_cap = in_size + 32768;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    int out_len = 0;
    jpeg_enc_config_t cfg = {
        .width = (int)s_cam.width,
        .height = (int)s_cam.height,
        .src_type = JPEG_PIXEL_FORMAT_CbYCrY,   /* UYVY == camera fourcc */
        .subsampling = JPEG_SUBSAMPLE_422,
        .quality = 45,
        .rotate = JPEG_ROTATE_0D,
        .task_enable = false,
    };

    if (frame == NULL) {
        ESP_LOGE(LOG_TAG, "snapshot: null frame");
        return;
    }

    in_buf = jpeg_calloc_align(in_size, 16);
    out_buf = jpeg_calloc_align(out_cap, 16);
    if (in_buf == NULL || out_buf == NULL) {
        ESP_LOGE(LOG_TAG, "snapshot: alloc failed");
        goto out;
    }
    memcpy(in_buf, frame, in_size);

    if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
        ESP_LOGE(LOG_TAG, "snapshot: jpeg_enc_open failed");
        goto out;
    }
    if (jpeg_enc_process(enc, in_buf, (int)in_size, out_buf, (int)out_cap, &out_len) != JPEG_ERR_OK) {
        ESP_LOGE(LOG_TAG, "snapshot: jpeg_enc_process failed");
        goto out;
    }


    /* ASCII chunks allow unrelated UART log lines between chunks. The receiver
     * validates offsets, decoded size and whole-frame FNV-1a before displaying. */
    static unsigned frame_id;
    unsigned id = ++frame_id;
    uint32_t hash = 2166136261u;
    for (int i=0; i<out_len; ++i) hash=(hash^out_buf[i])*16777619u;
    char line[256];
    int len=snprintf(line,sizeof(line),"\r\nJPG2_BEGIN %u %u %08x\r\n",id,(unsigned)out_len,(unsigned)hash);
    qm_dbg_write(line,len,QM_WAIT_FOREVER);
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int off=0; off<out_len; off+=96) {
        int n=out_len-off; if(n>96)n=96;
        char encoded[129]; int k=0;
        for(int j=0;j<n;j+=3) {
            uint32_t v=(uint32_t)out_buf[off+j]<<16;
            if(j+1<n)v|=(uint32_t)out_buf[off+j+1]<<8;
            if(j+2<n)v|=out_buf[off+j+2];
            encoded[k++]=alphabet[(v>>18)&63];encoded[k++]=alphabet[(v>>12)&63];
            encoded[k++]=j+1<n?alphabet[(v>>6)&63]:'=';
            encoded[k++]=j+2<n?alphabet[v&63]:'=';
        }
        encoded[k]=0;
        len=snprintf(line,sizeof(line),"\r\nJPG2_DATA %u %u %s\r\n",id,(unsigned)off,encoded);
        qm_dbg_write(line,len,QM_WAIT_FOREVER);
        if (((off / 96) & 7) == 7) {
            vTaskDelay(1);
        }
    }
    len=snprintf(line,sizeof(line),"\r\nJPG2_END %u\r\n",id);
    qm_dbg_write(line,len,QM_WAIT_FOREVER);

out:
    if (enc != NULL) {
        jpeg_enc_close(enc);
    }
    if (out_buf != NULL) {
        jpeg_free_align(out_buf);
    }
    if (in_buf != NULL) {
        jpeg_free_align(in_buf);
    }
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
            bool pose_busy = false;
#if CONFIG_QM_POSE_SUPPORT
            portENTER_CRITICAL(&s_pose_mux);
            pose_busy = (s_pose_state == 2);
            portEXIT_CRITICAL(&s_pose_mux);
#endif
            if (s_continuous_stream && !s_stream_stop_req && !pose_busy && !s_snapshot_busy) {
                (void)cam_stream_on();
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
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

#if CONFIG_QM_POSE_SUPPORT
        bool take_pose = false;
        bool pose_timed_out = false;
        if (buf.bytesused == expect && frame_ready_for_oneshot) {
            portENTER_CRITICAL(&s_pose_mux);
            if (s_pose_state == 1) {
                s_pose_state = 2;
                take_pose = true;
            }
            portEXIT_CRITICAL(&s_pose_mux);
        }
        portENTER_CRITICAL(&s_pose_mux);
        if (s_pose_state == 1 &&
            (TickType_t)(xTaskGetTickCount() - s_pose_requested_at) >= pdMS_TO_TICKS(5000)) {
            s_pose_state = 0;
            pose_timed_out = true;
        }
        portEXIT_CRITICAL(&s_pose_mux);
        if (pose_timed_out) {
            ESP_LOGW(LOG_TAG, "POSE_RESULT ret=ESP_ERR_TIMEOUT status=3 err=0 elapsed_ms=5000 no_valid_camera_frame");
            if (s_oneshot_stream) {
                stop_after_frame = true;
            }
            cam_exit_vision_window();
        }
        if (take_pose) {
            memcpy(s_pose_raw, s_cam.buffer[buf.index], expect);
            stop_after_frame = true;
        }
#endif

        bool take_snap = false;
        if (s_snap && buf.bytesused == expect && frame_ready_for_oneshot) {
            s_snap=false;
            memcpy(s_snapshot_raw,s_cam.buffer[buf.index],expect);
            take_snap = true;
            stop_after_frame = true;
        } else if (s_snap && (TickType_t)(xTaskGetTickCount()-s_snapshot_requested_at)>=pdMS_TO_TICKS(5000)) {
            s_snap=false;s_snapshot_busy=false;
            qm_cli_printf("SNAP_ERROR no_valid_camera_frame\r\n");
            if (s_oneshot_stream) {
                stop_after_frame = true;
            }
            cam_exit_vision_window();
        }

        if (ioctl(s_cam.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(LOG_TAG, "VIDIOC_QBUF return failed, errno=%d", errno);
        }

        /* [MODIFIED] 线性控制：在唤醒 YOLO 推理/JPEG 压缩前先关闭 SPI 视频流，彻底释放 DMA 与中断 */
        if (stop_after_frame || s_stream_stop_req) {
            (void)cam_stream_off();
        }
#if CONFIG_QM_POSE_SUPPORT
        if (take_pose) {
            xTaskNotifyGive(s_pose_task);
        }
#endif
        if (take_snap) {
            xTaskNotifyGive(s_snapshot_task);
        }
    }
}

esp_err_t bsp_camera_early_init(void)
{
    /* Route stdio/ESP logs through the same UART driver TX mutex as CLI. */
    esp_vfs_dev_uart_use_driver((uart_port_t)CONFIG_QM_DEBUG_UART_PORT);
    cam_pwdn_hold_low();

    if (!s_video_ready) {
        if (cam_video_init() != ESP_OK) {
            ESP_LOGE(LOG_TAG, "esp_video_init failed (check 3V3/GND/I2C/PWDN wiring)");
            return ESP_FAIL;
        }
        s_video_ready = true;
    }
    if (cam_setup_buffers() != ESP_OK) {
        ESP_LOGE(LOG_TAG, "camera buffer alloc failed");
        cam_teardown();
        return ESP_FAIL;
    }
    s_available = true;
    ESP_LOGI(LOG_TAG, "early init ok, wait for stream_start");
    return ESP_OK;
}

esp_err_t bsp_camera_stream_start(void)
{
    if (!s_available) {
        ESP_LOGE(LOG_TAG, "stream_start before early_init");
        return ESP_ERR_INVALID_STATE;
    }
    s_continuous_stream = true;
    s_oneshot_stream = false;
    if (cam_stream_on() != ESP_OK) {
        return ESP_FAIL;
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

esp_err_t bsp_camera_stream_stop(void)
{
    s_continuous_stream = false;
    s_oneshot_stream = false;
    if (!s_cam.stream_on) {
        return ESP_OK;
    }
    if (s_stream_task == NULL) {
        return cam_stream_off();
    }
    s_stream_stop_req = true;
    for (int i = 0; i < 50 && s_cam.stream_on; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return s_cam.stream_on ? ESP_ERR_TIMEOUT : ESP_OK;
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

#if CONFIG_QM_CLI_SUPPORT
static void cam_cli(char *buf, int len, int argc, char **argv)
{
    (void)buf;
    (void)len;
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        qm_cli_printf("camera available=%d stream=%s fd=%d %ux%u fourcc=0x%08x\r\n",
                      s_available ? 1 : 0,
                      s_cam.stream_on ? "on" : "off",
                      s_cam.fd, (unsigned)s_cam.width, (unsigned)s_cam.height,
                      (unsigned)s_cam.pixel_format);
    } else if (argc == 2 && strcmp(argv[1], "start") == 0) {
        s_oneshot_stream = false;
        if (bsp_camera_stream_start() == ESP_OK) {
            qm_cli_printf("camera stream start ok\r\n");
        } else {
            qm_cli_printf("camera stream start failed (early_init ok? already running?)\r\n");
        }
    } else if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        if (bsp_camera_stream_stop() == ESP_OK) {
            qm_cli_printf("camera stream stop ok\r\n");
        } else {
            qm_cli_printf("camera stream stop failed\r\n");
        }

#if CONFIG_QM_POSE_SUPPORT
    } else if ((argc == 2 || argc == 3) &&
               (strcmp(argv[1], "pose") == 0 || strcmp(argv[1], "calibrate") == 0)) {
        int rotation = 0;
        if (argc == 3) {
            if (strcmp(argv[2],"90")==0) rotation=90;
            else if (strcmp(argv[2],"180")==0) rotation=180;
            else if (strcmp(argv[2],"270")==0) rotation=270;
            else if (strcmp(argv[2],"0")!=0) {
                qm_cli_printf("cam pose|calibrate [0|90|180|270]\r\n"); return;
            }
        }
        esp_err_t ret = bsp_camera_pose_request(rotation, strcmp(argv[1],"calibrate")==0);
        qm_cli_printf("camera pose request: %s\r\n",esp_err_to_name(ret));
#endif
    } else if (argc == 2 && strcmp(argv[1], "snap") == 0) {
        esp_err_t ret=snapshot_request();
        qm_cli_printf("snapshot request %s\r\n",esp_err_to_name(ret));
    } else {
        qm_cli_printf("cam status|start|stop|snap|pose|calibrate [0|90|180|270]\r\n");
    }
}

static const struct qm_cli_command s_cam_cmd = {
    .name = "cam",
    .help = "cam status|start|stop|snap|pose|calibrate [0|90|180|270] (BF3901 stream debug)",
    .function = cam_cli,
};
#endif

int bsp_camera_cli_init(void)
{
#if CONFIG_QM_CLI_SUPPORT
    return qm_cli_register_command(&s_cam_cmd);
#else
    return -QM_EIO;
#endif
}
