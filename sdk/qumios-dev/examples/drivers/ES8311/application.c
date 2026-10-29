#include "qm_config.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_i2c.h"
#include "qm_string.h"

#include "es8311.h"

#define LOG_TAG "es8311_example"

#ifndef CONFIG_ES8311_EXAMPLE_I2C_PORT
#define CONFIG_ES8311_EXAMPLE_I2C_PORT 0
#endif

#ifndef CONFIG_ES8311_EXAMPLE_I2C_ADDR
#define CONFIG_ES8311_EXAMPLE_I2C_ADDR ES8311_ADDRESS_0
#endif

#ifndef CONFIG_ES8311_EXAMPLE_I2C_SDA_PIN
#define CONFIG_ES8311_EXAMPLE_I2C_SDA_PIN 17
#endif

#ifndef CONFIG_ES8311_EXAMPLE_I2C_SCL_PIN
#define CONFIG_ES8311_EXAMPLE_I2C_SCL_PIN 18
#endif

#ifndef CONFIG_ES8311_EXAMPLE_SAMPLE_RATE
#define CONFIG_ES8311_EXAMPLE_SAMPLE_RATE 16000
#endif

#ifndef CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN
#define CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN 0
#endif

#ifndef CONFIG_ES8311_EXAMPLE_MCLK_FREQUENCY
#define CONFIG_ES8311_EXAMPLE_MCLK_FREQUENCY 12288000
#endif

#ifndef CONFIG_ES8311_EXAMPLE_USE_DIGITAL_MIC
#define CONFIG_ES8311_EXAMPLE_USE_DIGITAL_MIC 0
#endif

#ifndef CONFIG_ES8311_EXAMPLE_MIC_GAIN
#define CONFIG_ES8311_EXAMPLE_MIC_GAIN ES8311_MIC_GAIN_12DB
#endif

#ifndef CONFIG_ES8311_EXAMPLE_VOLUME
#define CONFIG_ES8311_EXAMPLE_VOLUME 70
#endif

#ifndef CONFIG_ES8311_EXAMPLE_DUMP_REGISTERS
#define CONFIG_ES8311_EXAMPLE_DUMP_REGISTERS 0
#endif

static qm_i2c_dev_t s_i2c;
static es8311_dev_t s_codec;

static int es8311_example_check(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed: %d", name, ret);
    }
    return ret;
}

static void es8311_example_configure_i2c(qm_i2c_dev_t *i2c)
{
    qm_memset(i2c, 0, sizeof(*i2c));
    i2c->port = CONFIG_ES8311_EXAMPLE_I2C_PORT;
    i2c->config.mode = QM_I2C_MODE_MASTER;
    i2c->config.address_width = QM_I2C_ADDRESS_WIDTH_7BIT;
    i2c->config.freq = QM_I2C_BUS_BIT_RATES_400K;
    i2c->pins.sda_pin = CONFIG_ES8311_EXAMPLE_I2C_SDA_PIN;
    i2c->pins.sck_pin = CONFIG_ES8311_EXAMPLE_I2C_SCL_PIN;
}

static void es8311_example_configure_codec(es8311_dev_t *codec, qm_i2c_dev_t *i2c)
{
    qm_memset(codec, 0, sizeof(*codec));
    codec->i2c_dev = i2c;
    codec->dev_addr = CONFIG_ES8311_EXAMPLE_I2C_ADDR;
    codec->clock_config.mclk_from_mclk_pin = CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN;
    codec->clock_config.mclk_frequency = CONFIG_ES8311_EXAMPLE_MCLK_FREQUENCY;
    codec->clock_config.sample_frequency = CONFIG_ES8311_EXAMPLE_SAMPLE_RATE;
    codec->input_resolution = ES8311_RESOLUTION_16;
    codec->output_resolution = ES8311_RESOLUTION_16;
}

static void es8311_example_run(void)
{
    int volume = 0;

    es8311_example_configure_i2c(&s_i2c);
    es8311_example_configure_codec(&s_codec, &s_i2c);

    if (es8311_example_check("qm_i2c_init", qm_i2c_init(&s_i2c)) != QM_EOK) {
        return;
    }

    if (es8311_example_check("es8311_init", es8311_init(&s_codec)) != QM_EOK) {
        goto _exit_i2c;
    }

    /* Keep the demo flow short: initialize codec, apply common controls, then optionally dump registers. */
    if (es8311_example_check("es8311_config_microphone",
                             es8311_config_microphone(&s_codec, CONFIG_ES8311_EXAMPLE_USE_DIGITAL_MIC)) != QM_EOK) {
        goto _exit_codec;
    }

    if (es8311_example_check("es8311_set_microphone_gain",
                             es8311_set_microphone_gain(&s_codec, CONFIG_ES8311_EXAMPLE_MIC_GAIN)) != QM_EOK) {
        goto _exit_codec;
    }

    if (es8311_example_check("es8311_set_voice_volume",
                             es8311_set_voice_volume(&s_codec, CONFIG_ES8311_EXAMPLE_VOLUME, &volume)) != QM_EOK) {
        goto _exit_codec;
    }

    if (es8311_example_check("es8311_set_voice_mute", es8311_set_voice_mute(&s_codec, QM_FALSE)) != QM_EOK) {
        goto _exit_codec;
    }

    QM_LOGI(LOG_TAG,
            "codec ready: addr=0x%02x sample_rate=%d volume=%d digital_mic=%d",
            s_codec.dev_addr,
            s_codec.clock_config.sample_frequency,
            volume,
            CONFIG_ES8311_EXAMPLE_USE_DIGITAL_MIC);

#if CONFIG_ES8311_EXAMPLE_DUMP_REGISTERS
    es8311_register_dump(&s_codec);
#endif

_exit_codec:
    (void)es8311_deinit(&s_codec);
_exit_i2c:
    (void)qm_i2c_deinit(&s_i2c);
}

void qm_application_start(void)
{
    QM_LOGI(LOG_TAG, "start");
    es8311_example_run();
}
