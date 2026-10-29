#include "bsp_ws2812.h"

#include "led_strip.h"
#include "led_strip_rmt.h"
#include "qm_log.h"

#define LOG_TAG "bsp_ws2812"

static led_strip_handle_t s_strip;
static uint8_t s_inited;

qm_err_t bsp_ws2812_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BSP_WS2812_GPIO,
        .max_leds = BSP_WS2812_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    /*
     * 8 颗 * 24bit = 192 个 symbol，正好占满 S3 的 4 块 RMT 内存（每块 48）。
     * 不开 DMA：PSRAM 上的通道对象过不了 GDMA 的内部 RAM 检查。
     */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = BSP_WS2812_LED_NUM * 24,
        .flags.with_dma = false,
    };

    if (s_inited) {
        return QM_EOK;
    }

    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip) != ESP_OK) {
        QM_LOGE(LOG_TAG, "rmt init fail gpio%d", BSP_WS2812_GPIO);
        return -QM_EIO;
    }
    if (led_strip_clear(s_strip) != ESP_OK) {
        QM_LOGE(LOG_TAG, "clear fail");
        return -QM_EIO;
    }

    s_inited = 1;
    QM_LOGI(LOG_TAG, "init ok gpio%d leds %d", BSP_WS2812_GPIO, BSP_WS2812_LED_NUM);
    return QM_EOK;
}

qm_err_t bsp_ws2812_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "set ignored, not inited");
        return -QM_EIO;
    }
    if (index >= BSP_WS2812_LED_NUM) {
        return -QM_EINVAL;
    }
    if (led_strip_set_pixel(s_strip, index, r, g, b) != ESP_OK) {
        return -QM_EIO;
    }
    return QM_EOK;
}

qm_err_t bsp_ws2812_refresh(void)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "refresh ignored, not inited");
        return -QM_EIO;
    }
    if (led_strip_refresh(s_strip) != ESP_OK) {
        QM_LOGE(LOG_TAG, "refresh fail");
        return -QM_EIO;
    }
    return QM_EOK;
}

qm_err_t bsp_ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t i;

    if (!s_inited) {
        QM_LOGW(LOG_TAG, "fill ignored, not inited");
        return -QM_EIO;
    }

    for (i = 0; i < BSP_WS2812_LED_NUM; i++) {
        if (led_strip_set_pixel(s_strip, i, r, g, b) != ESP_OK) {
            return -QM_EIO;
        }
    }
    return bsp_ws2812_refresh();
}

qm_err_t bsp_ws2812_clear(void)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "clear ignored, not inited");
        return -QM_EIO;
    }
    if (led_strip_clear(s_strip) != ESP_OK) {
        QM_LOGE(LOG_TAG, "clear fail");
        return -QM_EIO;
    }
    return QM_EOK;
}

/*
 * 先 init。set_pixel 只改缓冲，要 bsp_ws2812_refresh() 才发出去。
 * fill 和 clear 自己会发出去。参数顺序是 R、G、B，0 灭，255 最亮。
 * 下面的 48 大约是满亮度的五分之一。8 颗全白时每颗大约十几毫安。
 *
 *   bsp_ws2812_init();
 *   bsp_ws2812_set_pixel(0, 48, 0, 0);   // 第 0 颗红
 *   bsp_ws2812_set_pixel(1, 0, 48, 0);   // 第 1 颗绿
 *   bsp_ws2812_refresh();
 *
 *   bsp_ws2812_fill(0, 0, 48);           // 8 颗一起蓝
 *   bsp_ws2812_clear();                  // 全灭
 */
void bsp_ws2812_example(void)
{
    static const uint8_t colors[BSP_WS2812_LED_NUM][3] = {
        {48, 0,  0},  /* 红 */
        {0,  48, 0},  /* 绿 */
        {0,  0,  48}, /* 蓝 */
        {48, 48, 0},  /* 黄 */
        {0,  48, 48}, /* 青 */
        {48, 0,  48}, /* 紫 */
        {48, 24, 0},  /* 橙，绿通道用一半 */
        {48, 48, 48}, /* 白 */
    };
    uint32_t i;

    if (bsp_ws2812_init() != QM_EOK) {
        return;
    }
    for (i = 0; i < BSP_WS2812_LED_NUM; i++) {
        (void)bsp_ws2812_set_pixel(i, colors[i][0], colors[i][1], colors[i][2]);
    }
    (void)bsp_ws2812_refresh();
}
