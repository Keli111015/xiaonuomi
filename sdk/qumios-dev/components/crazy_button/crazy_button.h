#ifndef _CRAZY_BUTTON_H_
#define _CRAZY_BUTTON_H_

#ifdef __cplusplus
extern "C"
{
#endif 

#include "qm_types.h"
#include "qm_config.h"

#ifndef CONFIG_CRAZY_BUTTON_TICKS_INTERVAL
#define CONFIG_CRAZY_BUTTON_TICKS_INTERVAL 20 //ms
#endif

#ifndef CONFIG_CRAZY_BUTTON_DEBOUNCE_TICKS
#define CONFIG_CRAZY_BUTTON_DEBOUNCE_TICKS 1  //MAX 8
#endif

#ifndef CONFIG_CRAZY_BUTTON_SHORT_TIMEOUT
#define CONFIG_CRAZY_BUTTON_SHORT_TIMEOUT    500 //连续单击的超时时间，超过就重新计数
#endif

#ifndef CONFIG_CRAZY_BUTTON_REPEAT_TIMEOUT
#define CONFIG_CRAZY_BUTTON_REPEAT_TIMEOUT   400 //连续操作之间的超时时间，比如单击之后，立马长按，这两个操作之间的超时时间
#endif

#ifndef CONFIG_CRAZY_BUTTON_LONG_TIMEOUT
#define CONFIG_CRAZY_BUTTON_LONG_TIMEOUT     1000
#endif

#ifndef CONFIG_CRAZY_BUTTON_LONG_REPORT_TIME_MS
#define CONFIG_CRAZY_BUTTON_LONG_REPORT_TIME_MS 1000 //长按上报的时间间隔, 默认长按时，每秒报一次
#endif

#ifndef CONFIG_CRAZY_BUTTON_LAST_TIMEOUT
#define CONFIG_CRAZY_BUTTON_LAST_TIMEOUT         1000  //超过这个时间，清除之前的按键状态(last_event, last_repeat)
#endif

#define CONFIG_CRAZY_BUTTON_SHORT_TICKS    (CONFIG_CRAZY_BUTTON_SHORT_TIMEOUT / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL)
#define CONFIG_CRAZY_BUTTON_LONG_TICKS     (CONFIG_CRAZY_BUTTON_LONG_TIMEOUT / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL)
#define CONFIG_CRAZY_BUTTON_LONG_REPORT_TICKS (CONFIG_CRAZY_BUTTON_LONG_REPORT_TIME_MS / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL)
#define CONFIG_CRAZY_BUTTON_LAST_TICKS   (CONFIG_CRAZY_BUTTON_LAST_TIMEOUT / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL)

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
    PRESS_EVENT_LONG_RELEASE,
    PRESS_EVENT_MAX,
} press_event_t;

typedef struct crazy_button
{
    uint8_t repeat : 4;
    uint8_t event : 4;
    uint8_t last_repeat : 4;
    uint8_t last_event : 4;
    uint8_t state : 3;
    uint8_t debounce_cnt : 3;
    uint8_t active_level : 1;
    uint8_t button_level : 1;
    uint16_t ticks;
    uint16_t last_ticks;
    pin_level_get_fn pin_level_get;
    button_cb cb;
    struct crazy_button *next;
} crazy_button_t;

void crazy_button_init(crazy_button_t *handle, pin_level_get_fn fn, uint8_t active_level);
void crazy_button_attach(crazy_button_t *handle, button_cb cb);
press_event_t crazy_button_event_get(crazy_button_t *handle);
int crazy_button_start(crazy_button_t *handle);
void crazy_button_stop(crazy_button_t *handle);
void crazy_button_ticks(void);
int  crazy_button_ticks_to_ms(uint16_t ticks);

#ifdef __cplusplus
}
#endif

#endif
