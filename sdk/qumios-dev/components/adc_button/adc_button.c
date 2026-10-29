#include "adc_button.h"

#define EVENT_CB(ev)    \
    if (handle->cb[ev]) \
    handle->cb[ev]((adc_button_t *)handle)

//button handle list head.
static adc_button_t *head_handle = NULL;

/**
  * @brief  Initializes the button struct handle.
  * @param  handle: the button handle strcut.
  * @param  fn: read the HAL GPIO of the connet button level.
  * @param  active_level: pressed GPIO level.
  * @retval None
  */
void adc_button_init(adc_button_t *handle, adc_value_get_fun fn, uint32_t adc_value_min, uint32_t adc_value_max)
{
    memset(handle, 0, sizeof(adc_button_t));
    handle->event = (uint8_t)PRESS_EVENT_NONE;
    handle->adc_value_get = fn;
    handle->adc_value_min = adc_value_min;
    handle->adc_value_max = adc_value_max;
    uint32_t adc_value = handle->adc_value_get();
    handle->press_down = (adc_value >= handle->adc_value_min && adc_value <= handle->adc_value_max);
}

/**
  * @brief  Attach the button event callback function.
  * @param  handle: the button handle strcut.
  * @param  event: trigger event type.
  * @param  cb: callback function.
  * @retval None
  */
void adc_button_attach(adc_button_t *handle, press_event_t event, button_cb cb)
{
    handle->cb[event] = cb;
}

/**
  * @brief  Inquire the button event happen.
  * @param  handle: the button handle strcut.
  * @retval button event.
  */
press_event_t adc_button_event_get(adc_button_t *handle)
{
    return (press_event_t)(handle->event);
}

/**
  * @brief  Button driver core function, driver state machine.
  * @param  handle: the button handle strcut.
  * @retval None
  */
static void button_handler(adc_button_t *handle)
{
    uint32_t adc_value = handle->adc_value_get();
    uint8_t curr_press_down = (adc_value >= handle->adc_value_min && adc_value <= handle->adc_value_max);

    //ticks counter working..
    if ((handle->state) > 0)
        handle->ticks++;

    /*------------button debounce handle---------------*/
    if (curr_press_down != handle->press_down)
    { //not equal to prev one
        //continue read 3 times same new level change
        if (++(handle->debounce_cnt) >= CONFIG_ADC_BUTTON_DEBOUNCE_TICKS)
        {
            handle->press_down = curr_press_down;
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
        if (handle->press_down)
        { //start press down
            handle->event = (uint8_t)PRESS_EVENT_DOWN;
            EVENT_CB(PRESS_EVENT_DOWN);
            handle->ticks = 0;
            handle->repeat = 1;
            handle->state = 1;
        }
        else
        {
            handle->event = (uint8_t)PRESS_EVENT_NONE;
        }
        break;

    case 1:
        if (!handle->press_down)
        { //released press up
            handle->event = (uint8_t)PRESS_EVENT_UP;
            EVENT_CB(PRESS_EVENT_UP);
            handle->ticks = 0;
            handle->state = 2;
        }
        else if (handle->ticks > CONFIG_ADC_BUTTON_LONG_TICKS)
        {
            handle->event = (uint8_t)PRESS_EVENT_LONG_START;
            EVENT_CB(PRESS_EVENT_LONG_START);
            handle->state = 5;
        }
        break;

    case 2:
        if (handle->press_down)
        { //press down again
            handle->event = (uint8_t)PRESS_EVENT_DOWN;
            EVENT_CB(PRESS_EVENT_DOWN);
            handle->repeat++;
            EVENT_CB(PRESS_EVENT_REPEAT); // repeat hit
            handle->ticks = 0;
            handle->state = 3;
        }
        else if (handle->ticks > CONFIG_ADC_BUTTON_SHORT_TICKS)
        { //released timeout
            if (handle->repeat == 1)
            {
                handle->event = (uint8_t)PRESS_EVENT_SINGLE_CLICK;
                EVENT_CB(PRESS_EVENT_SINGLE_CLICK);
            }
            else if (handle->repeat == 2)
            {
                handle->event = (uint8_t)PRESS_EVENT_DOUBLE_CLICK;
                EVENT_CB(PRESS_EVENT_DOUBLE_CLICK); // repeat hit
            }
            handle->state = 0;
        }
        break;

    case 3:
        if (!handle->press_down)
        { //released press up
            handle->event = (uint8_t)PRESS_EVENT_UP;
            EVENT_CB(PRESS_EVENT_UP);
            if (handle->ticks < CONFIG_ADC_BUTTON_SHORT_TICKS)
            {
                handle->ticks = 0;
                handle->state = 2; //repeat press
            }
            else
            {
                handle->state = 0;
            }
        }
        else if (handle->ticks > CONFIG_ADC_BUTTON_SHORT_TICKS)
        { // long press up
            handle->state = 0;
        }
        break;

    case 5:
        if (handle->press_down)
        {
            //continue hold trigger
            handle->event = (uint8_t)PRESS_EVENT_LONG_HOLD;
            EVENT_CB(PRESS_EVENT_LONG_HOLD);
        }
        else
        { //releasd
            handle->event = (uint8_t)PRESS_EVENT_UP;
            EVENT_CB(PRESS_EVENT_UP);
            handle->state = 0; //reset
        }
        break;
    }
}

/**
  * @brief  Start the button work, add the handle into work list.
  * @param  handle: target handle strcut.
  * @retval 0: succeed. -1: already exist.
  */
int adc_button_start(adc_button_t *handle)
{
    adc_button_t *target = head_handle;
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
void adc_button_stop(adc_button_t *handle)
{
    adc_button_t **curr;
    for (curr = &head_handle; *curr;)
    {
        adc_button_t *entry = *curr;
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
void adc_button_ticks(void)
{
    adc_button_t *target;
    for (target = head_handle; target; target = target->next)
    {
        button_handler(target);
    }
}
