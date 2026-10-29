#ifndef QM_ADC_H
#define QM_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_adc ADC
 *  qm adc API.
 *
 *  @{
 */

#include "qm_types.h"

/**
 * @brief adc pin
 */
typedef struct {
    uint8_t adc_pin;
} qm_adc_pin_t;

/* Define ADC dev hal handle */
typedef struct {
    uint8_t      port;   /**< adc port */
    qm_adc_pin_t pin;    /**< adc pin */
    void        *priv;   /**< priv data */
} qm_adc_dev_t;

/**
 * Initialises an ADC interface, Prepares an ADC hardware interface for sampling
 *
 * @param[in]  adc  the interface which should be initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_adc_init(qm_adc_dev_t *adc);

/**
 * Takes a single sample from an ADC interface
 *
 * @param[in]   adc      the interface which should be sampled
 * @param[out]  output   pointer to a variable which will receive the sample
 * @param[in]   timeout  ms timeout
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout);

/**
 * ADC sampling start
 *
 * @param[in]   adc             the ADC interface
 * @param[in]   data            adc data buffer
 * @param[in]   size            data buffer size aligned with resolution (until the next power of two)
 *
 * @return  0 : on success, EIO : if an error occurred with any step
 */
int32_t  qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size);

/**
 * ADC sampling stop
 *
 * @param[in]   adc             the ADC interface
 *
 * @return  0 : on success, EIO : if an error occurred with any step
 */
int32_t qm_adc_stop(qm_adc_dev_t *adc);

/**
 * De-initialises an ADC interface, Turns off an ADC hardware interface
 *
 * @param[in]  adc  the interface which should be de-initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_adc_deinit(qm_adc_dev_t *adc);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_ADC_H */

