/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2021 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "esp_log.h"
#include "board.h"
#include "i2c_bus.h"
#include "gx8008.h"

static char *TAG = "GX8008";

esp_err_t gx8008_adc_init()
{
    return ESP_OK;
}

esp_err_t gx8008_adc_deinit()
{
    return ESP_OK;
}

esp_err_t gx8008_adc_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    return ESP_OK;
}

esp_err_t gx8008_adc_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    return ESP_OK;
}

esp_err_t gx8008_set_mute(bool mute)
{
    return ESP_OK;
}

esp_err_t gx8008_adc_set_volume(int volume)
{
    return ESP_OK;
}

/**
 * Operate function of ADC
 */
audio_hal_func_t AUDIO_CODEC_GX8008_DEFAULT_HANDLE = {
    .audio_codec_initialize   = gx8008_adc_init,
    .audio_codec_deinitialize = gx8008_adc_deinit,
    .audio_codec_ctrl         = gx8008_adc_ctrl_state,
    .audio_codec_config_iface = gx8008_adc_config_i2s,
    .audio_codec_set_mute     = gx8008_set_mute,
    .audio_codec_set_volume   = gx8008_adc_set_volume,
    .audio_codec_get_volume   = NULL,
    .audio_codec_enable_pa    = NULL,
    .audio_hal_lock           = NULL,
    .handle                   = NULL,
};
                              