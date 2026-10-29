#ifndef _MULTI_BUTTON_H_
#define _MULTI_BUTTON_H_

#ifdef __cplusplus
extern "C"
{
#endif 

#include "qm_types.h"
#include "qm_config.h"

#ifndef CONFIG_MULTI_BUTTON_TICKS_INTERVAL
#define CONFIG_MULTI_BUTTON_TICKS_INTERVAL 20 //ms
#endif

#ifndef CONFIG_MULTI_BUTTON_DEBOUNCE_TICKS
#define CONFIG_MULTI_BUTTON_DEBOUNCE_TICKS 1  //MAX 8
#endif

#ifndef CONFIG_MULTI_BUTTON_SHORT_TIMEOUT
#define CONFIG_MULTI_BUTTON_SHORT_TIMEOUT    200
#endif

#ifndef CONFIG_MULTI_BUTTON_LONG_TIMEOUT
#define CONFIG_MULTI_BUTTON_LONG_TIMEOUT     1000
#endif

#define CONFIG_MULTI_BUTTON_SHORT_TICKS    (CONFIG_MULTI_BUTTON_SHORT_TIMEOUT / CONFIG_MULTI_BUTTON_TICKS_INTERVAL)
#define CONFIG_MULTI_BUTTON_LONG_TICKS     (CONFIG_MULTI_BUTTON_LONG_TIMEOUT / CONFIG_MULTI_BUTTON_TICKS_INTERVAL)

typedef void (*button_cb)(void *handle);
typedef uint8_t (*pin_level_get_fn)(void);

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

typedef struct multi_button
{
    uint16_t ticks;
    uint8_t repeat : 4;
    uint8_t event : 4;
    uint8_t state : 3;
    uint8_t debounce_cnt : 3;
    uint8_t active_level : 1;
    uint8_t button_level : 1;
    pin_level_get_fn pin_level_get;
    button_cb cb[PRESS_EVENT_MAX];
    struct multi_button *next;
} multi_button_t;

void multi_button_init(multi_button_t *handle, pin_level_get_fn fn, uint8_t active_level);
void multi_button_attach(multi_button_t *handle, press_event_t event, button_cb cb);
press_event_t multi_button_event_get(multi_button_t *handle);
int multi_button_start(multi_button_t *handle);
void multi_button_stop(multi_button_t *handle);
void multi_button_ticks(void);

#ifdef __cplusplus
}
#endif

#endif
