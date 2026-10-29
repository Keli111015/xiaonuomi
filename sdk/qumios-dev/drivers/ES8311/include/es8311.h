#ifndef _ES8311_H_
#define _ES8311_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup es8311 ES8311
 *  es8311 driver API.
 *
 *  @{
 */

#include "qm_types.h"
#include "qm_errno.h"
#include "qm_i2c.h"

/**
 * @brief ES8311 I2C address when CE pin is low
 */
#define ES8311_ADDRESS_0 0x18u

/**
 * @brief ES8311 I2C address when CE pin is high
 */
#define ES8311_ADDRESS_1 0x19u

/**
 * @brief ES8311 microphone gain
 */
typedef enum {
    ES8311_MIC_GAIN_MIN = -1, /*!< Invalid minimum marker */
    ES8311_MIC_GAIN_0DB,      /*!< 0 dB */
    ES8311_MIC_GAIN_6DB,      /*!< 6 dB */
    ES8311_MIC_GAIN_12DB,     /*!< 12 dB */
    ES8311_MIC_GAIN_18DB,     /*!< 18 dB */
    ES8311_MIC_GAIN_24DB,     /*!< 24 dB */
    ES8311_MIC_GAIN_30DB,     /*!< 30 dB */
    ES8311_MIC_GAIN_36DB,     /*!< 36 dB */
    ES8311_MIC_GAIN_42DB,     /*!< 42 dB */
    ES8311_MIC_GAIN_MAX       /*!< Invalid maximum marker */
} es8311_mic_gain_t;

/**
 * @brief ES8311 fade ramp
 */
typedef enum {
    ES8311_FADE_OFF = 0,      /*!< Disable fade */
    ES8311_FADE_4LRCK,        /*!< 4 LRCK */
    ES8311_FADE_8LRCK,        /*!< 8 LRCK */
    ES8311_FADE_16LRCK,       /*!< 16 LRCK */
    ES8311_FADE_32LRCK,       /*!< 32 LRCK */
    ES8311_FADE_64LRCK,       /*!< 64 LRCK */
    ES8311_FADE_128LRCK,      /*!< 128 LRCK */
    ES8311_FADE_256LRCK,      /*!< 256 LRCK */
    ES8311_FADE_512LRCK,      /*!< 512 LRCK */
    ES8311_FADE_1024LRCK,     /*!< 1024 LRCK */
    ES8311_FADE_2048LRCK,     /*!< 2048 LRCK */
    ES8311_FADE_4096LRCK,     /*!< 4096 LRCK */
    ES8311_FADE_8192LRCK,     /*!< 8192 LRCK */
    ES8311_FADE_16384LRCK,    /*!< 16384 LRCK */
    ES8311_FADE_32768LRCK,    /*!< 32768 LRCK */
    ES8311_FADE_65536LRCK,    /*!< 65536 LRCK */
} es8311_fade_t;

/**
 * @brief ES8311 serial port resolution
 */
typedef enum {
    ES8311_RESOLUTION_16 = 16, /*!< 16-bit */
    ES8311_RESOLUTION_18 = 18, /*!< 18-bit */
    ES8311_RESOLUTION_20 = 20, /*!< 20-bit */
    ES8311_RESOLUTION_24 = 24, /*!< 24-bit */
    ES8311_RESOLUTION_32 = 32, /*!< 32-bit */
} es8311_resolution_t;

/**
 * @brief ES8311 clock configuration
 */
typedef struct {
    bool_t mclk_inverted;         /*!< Set to true to invert MCLK */
    bool_t sclk_inverted;         /*!< Set to true to invert BCLK/SCLK */
    bool_t mclk_from_mclk_pin;    /*!< Set to true to use MCLK pin, false to derive clock from SCLK */
    int32_t mclk_frequency;       /*!< MCLK frequency in Hz when using MCLK pin */
    int32_t sample_frequency;     /*!< Audio sample rate in Hz */
} es8311_clock_config_t;

/**
 * @brief ES8311 device descriptor
 */
typedef struct {
    qm_i2c_dev_t *i2c_dev;                  /*!< Initialized qumi I2C device */
    uint16_t dev_addr;                      /*!< ES8311 I2C address */
    es8311_clock_config_t clock_config;     /*!< Codec clock configuration */
    es8311_resolution_t input_resolution;   /*!< ADC serial port resolution */
    es8311_resolution_t output_resolution;  /*!< DAC serial port resolution */
} es8311_dev_t;

/**
 * @brief Initialize ES8311
 *
 * @param[in] es8311  ES8311 device descriptor
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_init(es8311_dev_t *es8311);

/**
 * @brief Deinitialize ES8311
 *
 * @param[in] es8311  ES8311 device descriptor
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_deinit(es8311_dev_t *es8311);

/**
 * @brief Configure ES8311 sample frequency
 *
 * @param[in] es8311            ES8311 device descriptor
 * @param[in] mclk_frequency    MCLK frequency in Hz
 * @param[in] sample_frequency  Sample frequency in Hz
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_sample_frequency(es8311_dev_t *es8311, int32_t mclk_frequency, int32_t sample_frequency);

/**
 * @brief Set ES8311 output volume
 *
 * @param[in]  es8311      ES8311 device descriptor
 * @param[in]  volume      Volume value in range [0, 100]
 * @param[out] volume_set  Actual applied volume, can be NULL
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_voice_volume(es8311_dev_t *es8311, int32_t volume, int32_t *volume_set);

/**
 * @brief Get ES8311 output volume
 *
 * @param[in]  es8311  ES8311 device descriptor
 * @param[out] volume  Volume value in range [0, 100]
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_get_voice_volume(es8311_dev_t *es8311, int32_t *volume);

/**
 * @brief Set ES8311 output mute
 *
 * @param[in] es8311  ES8311 device descriptor
 * @param[in] mute    Set to true to mute
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_voice_mute(es8311_dev_t *es8311, bool_t mute);

/**
 * @brief Configure ES8311 microphone path
 *
 * @param[in] es8311       ES8311 device descriptor
 * @param[in] digital_mic  Set to true for digital microphone
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_config_microphone(es8311_dev_t *es8311, bool_t digital_mic);

/**
 * @brief Set ES8311 microphone gain
 *
 * @param[in] es8311   ES8311 device descriptor
 * @param[in] gain_db  Microphone gain
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_microphone_gain(es8311_dev_t *es8311, es8311_mic_gain_t gain_db);

/**
 * @brief Configure ES8311 DAC fade
 *
 * @param[in] es8311  ES8311 device descriptor
 * @param[in] fade    Fade ramp rate
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_voice_fade(es8311_dev_t *es8311, es8311_fade_t fade);

/**
 * @brief Configure ES8311 ADC fade
 *
 * @param[in] es8311  ES8311 device descriptor
 * @param[in] fade    Fade ramp rate
 *
 * @return 0 on success, otherwise negative error code
 */
qm_err_t es8311_set_microphone_fade(es8311_dev_t *es8311, es8311_fade_t fade);

/**
 * @brief Dump ES8311 register values
 *
 * @param[in] es8311  ES8311 device descriptor
 */
void es8311_register_dump(es8311_dev_t *es8311);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _ES8311_H_ */
