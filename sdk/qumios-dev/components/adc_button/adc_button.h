#ifndef _ADC_BUTTON_H_
#define _ADC_BUTTON_H_

#ifdef __cplusplus
extern "C"
{
#endif 

#include "qm_types.h"
#include "qm_config.h"

#ifndef CONFIG_ADC_BUTTON_TICKS_INTERVAL
#define CONFIG_ADC_BUTTON_TICKS_INTERVAL 20 //ms
#endif

#ifndef CONFIG_ADC_BUTTON_DEBOUNCE_TICKS
#define CONFIG_ADC_BUTTON_DEBOUNCE_TICKS 1  //MAX 8
#endif

#ifndef CONFIG_ADC_BUTTON_SHORT_TIMEOUT
#define CONFIG_ADC_BUTTON_SHORT_TIMEOUT    200
#endif

#ifndef CONFIG_ADC_BUTTON_LONG_TIMEOUT
#define CONFIG_ADC_BUTTON_LONG_TIMEOUT     1000
#endif

#define CONFIG_ADC_BUTTON_SHORT_TICKS    (CONFIG_ADC_BUTTON_SHORT_TIMEOUT / CONFIG_ADC_BUTTON_TICKS_INTERVAL)
#define CONFIG_ADC_BUTTON_LONG_TICKS     (CONFIG_ADC_BUTTON_LONG_TIMEOUT / CONFIG_ADC_BUTTON_TICKS_INTERVAL)

typedef void (*button_cb)(void *handle);
typedef uint32_t (*adc_value_get_fun)(void);

typedef enum
{
    PRESS_EVENT_NONE = 0,
    PRESS_EVENT_DOWN,
    PRESS_EVENT_UP,
    PRESS_EVENT_REPEAT,
    PRESS_EVENT_SINGLE_CLICK,
    PRESS_EVENT_DOUBLE_CLICK,
    PRESS_EVENT_LONG_START,
    PRESS_EVENT_LONG_HOLD,
    PRESS_EVENT_MAX,
} press_event_t;

typedef struct adc_button
{
    uint16_t ticks;
    uint8_t repeat : 4;
    uint8_t event : 4;
    uint8_t state : 3;
    uint8_t debounce_cnt : 3;
    uint8_t rsv : 1;
    uint8_t press_down : 1;
    uint32_t adc_value_min;
    uint32_t adc_value_max;
    adc_value_get_fun adc_value_get;
    button_cb cb[PRESS_EVENT_MAX];
    struct adc_button *next;
} adc_button_t;

void adc_button_init(adc_button_t *handle, adc_value_get_fun fn, uint32_t adc_value_min, uint32_t adc_value_max);
void adc_button_attach(adc_button_t *handle, press_event_t event, button_cb cb);
press_event_t adc_button_event_get(adc_button_t *handle);
int adc_button_start(adc_button_t *handle);
void adc_button_stop(adc_button_t *handle);
void adc_button_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* _ADC_BUTTON_H_ */
