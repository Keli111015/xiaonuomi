#include "cam_debug_streamer.h"

#include "bsp_camera.h"
#include "agent_app.h"
#if defined(CONFIG_QM_POSE_SUPPORT) && CONFIG_QM_POSE_SUPPORT
#include "vision_service.h"
#endif

#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "qm.h"
#include "qm_cli.h"

#include <stdio.h>
#include <string.h>

#define LOG_TAG "cam_debug"
#define CAM_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static TaskHandle_t s_snapshot_task = NULL;
static uint8_t *s_snapshot_raw = NULL;
static size_t s_snapshot_capacity;
static bool s_snapshot_busy = false;
static portMUX_TYPE s_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;

static void cam_snapshot_dump(const uint8_t *frame)
{
    jpeg_enc_handle_t enc = NULL;
    uint32_t cam_w = 240, cam_h = 320;
    bsp_camera_get_resolution(&cam_w, &cam_h);
    const size_t in_size = (size_t)cam_w * cam_h * 2;
    const size_t out_cap = in_size + 32768;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    int out_len = 0;
    jpeg_enc_config_t cfg = {
        .width = (int)cam_w,
        .height = (int)cam_h,
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
    for (int i = 0; i < out_len; ++i) {
        hash = (hash ^ out_buf[i]) * 16777619u;
    }
    char line[256];
    int len = snprintf(line, sizeof(line), "\r\nJPG2_BEGIN %u %u %08x\r\n", id, (unsigned)out_len, (unsigned)hash);
    qm_dbg_write(line, len, QM_WAIT_FOREVER);
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int off = 0; off < out_len; off += 96) {
        int n = out_len - off;
        if (n > 96) n = 96;
        char encoded[129];
        int k = 0;
        for (int j = 0; j < n; j += 3) {
            uint32_t v = (uint32_t)out_buf[off + j] << 16;
            if (j + 1 < n) v |= (uint32_t)out_buf[off + j + 1] << 8;
            if (j + 2 < n) v |= out_buf[off + j + 2];
            encoded[k++] = alphabet[(v >> 18) & 63];
            encoded[k++] = alphabet[(v >> 12) & 63];
            encoded[k++] = j + 1 < n ? alphabet[(v >> 6) & 63] : '=';
            encoded[k++] = j + 2 < n ? alphabet[v & 63] : '=';
        }
        encoded[k] = 0;
        len = snprintf(line, sizeof(line), "\r\nJPG2_DATA %u %u %s\r\n", id, (unsigned)off, encoded);
        qm_dbg_write(line, len, QM_WAIT_FOREVER);
        if (((off / 96) & 7) == 7) {
            vTaskDelay(1);
        }
    }
    len = snprintf(line, sizeof(line), "\r\nJPG2_END %u\r\n", id);
    qm_dbg_write(line, len, QM_WAIT_FOREVER);

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

static void snapshot_worker(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t val = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (val == 0) continue;
        esp_err_t ret = bsp_camera_frame_result(BSP_CAM_CLIENT_SNAP);
        if (ret == ESP_OK) cam_snapshot_dump(s_snapshot_raw);
        else ESP_LOGW(LOG_TAG, "snapshot capture failed: %s", esp_err_to_name(ret));
        bsp_camera_release_frame(BSP_CAM_CLIENT_SNAP);
        portENTER_CRITICAL(&s_snapshot_mux);
        s_snapshot_busy = false;
        portEXIT_CRITICAL(&s_snapshot_mux);
    }
}

esp_err_t cam_debug_streamer_init(void)
{
    return ESP_OK;
}

esp_err_t cam_debug_streamer_request_snap(void)
{
    if (!bsp_camera_is_available()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (agent_app_is_dialog_busy()) {
        ESP_LOGW(LOG_TAG, "snapshot deferred: voice dialog is active");
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_snapshot_mux);
    bool busy = s_snapshot_busy;
    if (!busy) s_snapshot_busy = true;
    portEXIT_CRITICAL(&s_snapshot_mux);
    if (busy) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ESP_ERR_NO_MEM;

    uint32_t cam_w = 240, cam_h = 320;
    bsp_camera_get_resolution(&cam_w, &cam_h);
    size_t in_size = (size_t)cam_w * cam_h * 2;

    if (in_size > s_snapshot_capacity) {
        uint8_t *raw = heap_caps_malloc(in_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (raw == NULL) goto failed;
        heap_caps_free(s_snapshot_raw);
        s_snapshot_raw = raw;
        s_snapshot_capacity = in_size;
    }
    if (s_snapshot_task == NULL) {
        if (xTaskCreatePinnedToCoreWithCaps(snapshot_worker, "cam_snapshot", 8192,
                                            NULL, 2, &s_snapshot_task, 0, CAM_STACK_CAPS) != pdPASS) {
            heap_caps_free(s_snapshot_raw);
            s_snapshot_raw = NULL;
            s_snapshot_capacity = 0;
            s_snapshot_task = NULL;
            goto failed;
        }
    }

    ret = bsp_camera_request_frame(BSP_CAM_CLIENT_SNAP, s_snapshot_raw, in_size, s_snapshot_task);
    if (ret == ESP_OK) return ESP_OK;
failed:
    portENTER_CRITICAL(&s_snapshot_mux);
    s_snapshot_busy = false;
    portEXIT_CRITICAL(&s_snapshot_mux);
    return ret;
}

#if CONFIG_QM_CLI_SUPPORT
static void cam_cli(char *buf, int len, int argc, char **argv)
{
    (void)buf;
    (void)len;
    uint32_t w = 0, h = 0;
    bsp_camera_get_resolution(&w, &h);

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        qm_cli_printf("camera available=%d stream=%s %ux%u fourcc=UYVY\r\n",
                      bsp_camera_is_available() ? 1 : 0,
                      bsp_camera_is_streaming() ? "on" : "off",
                      (unsigned)w, (unsigned)h);
    } else if (argc == 2 && strcmp(argv[1], "start") == 0) {
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
            if (strcmp(argv[2], "90") == 0) rotation = 90;
            else if (strcmp(argv[2], "180") == 0) rotation = 180;
            else if (strcmp(argv[2], "270") == 0) rotation = 270;
            else if (strcmp(argv[2], "0") != 0) {
                qm_cli_printf("cam pose|calibrate [0|90|180|270]\r\n");
                return;
            }
        }
        esp_err_t ret = vision_service_pose_request(rotation, strcmp(argv[1], "calibrate") == 0);
        qm_cli_printf("camera pose request: %s\r\n", esp_err_to_name(ret));
#endif
    } else if (argc == 2 && strcmp(argv[1], "snap") == 0) {
        esp_err_t ret = cam_debug_streamer_request_snap();
        qm_cli_printf("snapshot request %s\r\n", esp_err_to_name(ret));
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

int cam_debug_cli_init(void)
{
#if CONFIG_QM_CLI_SUPPORT
    return qm_cli_register_command(&s_cam_cmd);
#else
    return -QM_EIO;
#endif
}
