#include "qm.h"
#include "crazy_button.h"
#include "syslog.h"

#define EVENT_CB(ev)    \
    if (handle->cb) \
    handle->cb((crazy_button_t *)handle)

//button handle list head.
static crazy_button_t *head_handle = NULL;

/**
  * @brief  Initializes the button struct handle.
  * @param  handle: the button handle strcut.
  * @param  fn: read the HAL GPIO of the connet button level.
  * @param  active_level: pressed GPIO level.
  * @retval None
  */
void crazy_button_init(crazy_button_t *handle, pin_level_get_fn fn, uint8_t active_level)
{
    memset(handle, 0, sizeof(crazy_button_t));
    handle->event = (uint8_t)PRESS_EVENT_NONE;
    handle->pin_level_get = fn;
    handle->button_level = handle->pin_level_get();
    handle->active_level = active_level;
    handle->last_event = (uint8_t)PRESS_EVENT_NONE;
    handle->last_ticks = 0;
    handle->last_repeat = 0;
}

/**
  * @brief  Attach the button event callback function.
  * @param  handle: the button handle strcut.
  * @param  event: trigger event type.
  * @param  cb: callback function.
  * @retval None
  */
void crazy_button_attach(crazy_button_t *handle, button_cb cb)
{
    handle->cb = cb;
}

/**
  * @brief  Inquire the button event happen.
  * @param  handle: the button handle strcut.
  * @retval button event.
  */
press_event_t crazy_button_event_get(crazy_button_t *handle)
{
    return (press_event_t)(handle->event);
}

/**
  * @brief  Button driver core function, driver state machine.
  * @param  handle: the button handle strcut.
  * @retval None
  */
static void button_handler(crazy_button_t *handle, int ticks_cnt)
{
    uint8_t read_gpio_level = handle->pin_level_get();

    //ticks counter working..
    if ((handle->state) > 0)
    {
        handle->ticks += ticks_cnt;
    }

    /*------------button debounce handle---------------*/
    if (read_gpio_level != handle->button_level)
    { //not equal to prev one
        //continue read 3 times same new level change
        if (++(handle->debounce_cnt) >= CONFIG_CRAZY_BUTTON_DEBOUNCE_TICKS)
        {
            handle->button_level = read_gpio_level;
            handle->debounce_cnt = 0;
        }
    }
    else
    { //leved not change ,counter reset.
        handle->debounce_cnt = 0;
    }
    

    /*-----------------State machine-------------------*/
    switch (handle->state)
    {
    case 0:
        if (handle->button_level == handle->active_level)
        { //start press down
            // syslog(LOG_INFO, "0.1<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_DOWN;
            EVENT_CB(PRESS_EVENT_DOWN);
            handle->ticks = 0;
            handle->repeat = 1;
            handle->state = 1;
        }
        else
        {
            if (handle->event != (uint8_t)PRESS_EVENT_NONE)
            { 
                handle->event = (uint8_t)PRESS_EVENT_NONE;
            }
            if (handle->last_event != (uint8_t)PRESS_EVENT_NONE)
            { //check last event
                handle->last_ticks += ticks_cnt;
                if (handle->last_ticks > CONFIG_CRAZY_BUTTON_LAST_TICKS)
                {
                    handle->last_event = (uint8_t)PRESS_EVENT_NONE;
                    handle->last_repeat = 0;
                    handle->last_ticks = 0;
                }
            }

        }
        break;

    case 1:
        if (handle->button_level != handle->active_level)
        { //released press up
            // syslog(LOG_INFO, "1.1<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_UP;
            EVENT_CB(PRESS_EVENT_UP);
            handle->ticks = 0;
            handle->state = 2;
        }
        else if (handle->ticks > CONFIG_CRAZY_BUTTON_LONG_TICKS)
        {
            // syslog(LOG_INFO, "1.2<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_LONG_START;
            EVENT_CB(PRESS_EVENT_LONG_START);
            handle->state = 5;
        }
        break;

    case 2:
        if (handle->button_level == handle->active_level)
        { //press down again
            // syslog(LOG_INFO, "2.1<%x>%d,%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level, handle->repeat);
            handle->event = (uint8_t)PRESS_EVENT_DOWN;
            EVENT_CB(PRESS_EVENT_DOWN);
            handle->repeat++;
            handle->ticks = 0;
            handle->state = 3;
        }
        else if (handle->ticks > CONFIG_CRAZY_BUTTON_SHORT_TICKS)
        { //released timeout
            // syslog(LOG_INFO, "2.2<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_REPEAT;
            EVENT_CB(PRESS_EVENT_REPEAT); // repeat hit
            handle->state = 0;
            handle->last_event = (uint8_t)PRESS_EVENT_REPEAT;
            handle->last_repeat = handle->repeat;
            handle->last_ticks = 0;
            handle->repeat = 0;
        }
        break;

    case 3:
        if (handle->button_level != handle->active_level)
        { //released press up
            // syslog(LOG_INFO, "3.1<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_UP;
            EVENT_CB(PRESS_EVENT_UP);
            if (handle->ticks < CONFIG_CRAZY_BUTTON_SHORT_TICKS)
            {
                handle->ticks = 0;
                handle->state = 2; //repeat press
            }
            else
            {
                handle->state = 0;
            }
        }
        else if (handle->ticks > CONFIG_CRAZY_BUTTON_SHORT_TICKS)
        { // long press up
            // syslog(LOG_INFO, "3.2<%x>%d,%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level, handle->repeat);
            handle->last_event = (uint8_t)PRESS_EVENT_REPEAT;
            handle->last_repeat = handle->repeat - 1;
            handle->state = 0;
            handle->repeat = 0;
        }
        break;

    case 5:
        if (handle->button_level == handle->active_level)
        {
            //continue hold trigger
            // syslog(LOG_INFO, "5.1<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_LONG_HOLD;
            int repeat = handle->ticks / CONFIG_CRAZY_BUTTON_LONG_REPORT_TICKS;
            if (repeat > handle->repeat)
            {
                handle->repeat = repeat;
                EVENT_CB(PRESS_EVENT_LONG_HOLD);
            }
        }
        else
        { //releasd
            // syslog(LOG_INFO, "5.2<%x>%d,%d,%d", handle, handle->state, handle->ticks, handle->button_level);
            handle->event = (uint8_t)PRESS_EVENT_LONG_RELEASE;
            EVENT_CB(PRESS_EVENT_LONG_RELEASE);
            handle->last_event = (uint8_t)PRESS_EVENT_LONG_HOLD;
            handle->last_repeat = handle->repeat;
            handle->last_ticks = 0;
            handle->state = 0; //reset
            handle->repeat = 0;
            handle->ticks = 0;
        }
        break;
    }
}

/**
  * @brief  Start the button work, add the handle into work list.
  * @param  handle: target handle strcut.
  * @retval 0: succeed. -1: already exist.
  */
int crazy_button_start(crazy_button_t *handle)
{
    crazy_button_t *target = head_handle;
    while (target)
    {
        if (target == handle)
            return -1; //already exist.
        target = target->next;
    }
    handle->next = head_handle;
    head_handle = handle;
    return 0;
}

/**
  * @brief  Stop the button work, remove the handle off work list.
  * @param  handle: target handle strcut.
  * @retval None
  */
void crazy_button_stop(crazy_button_t *handle)
{
    crazy_button_t **curr;
    for (curr = &head_handle; *curr;)
    {
        crazy_button_t *entry = *curr;
        if (entry == handle)
        {
            *curr = entry->next;
            return;
        }
        else
            curr = &entry->next;
    }
}

/**
  * @brief  background ticks, timer repeat invoking interval 5ms.
  * @param  None.
  * @retval None
  */
void crazy_button_ticks(void)
{
    static uint32_t last_time_ms = 0;
    uint32_t now = qm_now_ms();
    int cnt = 0;
    if (last_time_ms == 0)
    {
        cnt = 1;
    }
    else
    {
        //根据时间，计算ticks增加数量
        //考虑负数，时间反转
        cnt = now > last_time_ms ? (now - last_time_ms) / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL : (last_time_ms - now) / CONFIG_CRAZY_BUTTON_TICKS_INTERVAL;
        if (cnt == 0)
        {
            cnt = 1;
        }
    }
    last_time_ms = now;
    crazy_button_t *target;
    for (target = head_handle; target; target = target->next)
    {
        button_handler(target, cnt);
    }
}

int  crazy_button_ticks_to_ms(uint16_t ticks)
{
    return ticks * CONFIG_CRAZY_BUTTON_TICKS_INTERVAL;
}
