#include "qm_delay_action.h"
#include "qm_eventbus_static.h"
#include "qm.h"
#include "qm_errno.h"
#include "syslog.h"

#ifndef CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM 
#define CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM  4
#endif
#ifndef CONFIG_EVENTBUS_DELAY_ACTION_TIMER_PERIOD_MS
#define CONFIG_EVENTBUS_DELAY_ACTION_TIMER_PERIOD_MS  1000
#endif

static uint32_t last_ms = 0;
static qm_timer_t _delay_action_timer = {0};

static qm_eventbus_delayaction_t _delay_action_list[CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM] = {0};

static void _delay_action_timer_cb(qm_timer_t *timer, void *arg);


int qm_eventbus_delay_action_init(void)
{
	memset(_delay_action_list, 0, sizeof(_delay_action_list));
	return 0;
}

static int _get_id(void)
{
	static uint8_t id = 0;
	id++;
	if (id == DELAYACTION_INVALID_ID)
	{
		//skip invalid id
		id++;
	}

	return id;
}

int qm_eventbus_delay_action_deinit(void)
{
	memset(_delay_action_list, 0, sizeof(_delay_action_list));
	if (qm_timer_is_valid(&_delay_action_timer))
	{
		qm_timer_free(&_delay_action_timer);
	}

	return 0;
}

static int _is_fn_exists(int busid, qm_void_callback_t *fn)
{
	for (int i = 0; i < CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM; i++)
	{
		if ((_delay_action_list[i].id != DELAYACTION_INVALID_ID)
			&& (_delay_action_list[i].busid == busid)
			&& (_delay_action_list[i].type == DELAYACTION_TYPE_FN)
			&& (_delay_action_list[i].data.fn.callback == fn->callback)
			&& (_delay_action_list[i].data.fn.arg == fn->arg))
		{
			//found, update it
			return i;
		}
	}

	return -1;
}

int qm_eventbus_delay_fn_start(int busid, uint32_t delay_ms, uint8_t repeat, qm_void_callback_t *fn)
{
	//find free slot
	int index;
	//1. find same fn
	index = _is_fn_exists(busid, fn);
	if (index >= 0)
	{
		//already exists, update parameters
		_delay_action_list[index].time_ms = delay_ms;
		_delay_action_list[index].current_ms = 0;
		_delay_action_list[index].repeat = repeat;
	}
	else
	{
		//2. find free slot
		for (index = 0; index < CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM; index++)
		{
			if (_delay_action_list[index].id == DELAYACTION_INVALID_ID)
			{
				break;
			}
		}
		//is thre any free slot?
		if (index >= CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM)
		{
			return -QM_ENOMEM;
		}
		//construct delay action
		_delay_action_list[index].id = _get_id();
		_delay_action_list[index].busid = busid;
		_delay_action_list[index].time_ms = delay_ms;
		_delay_action_list[index].repeat = repeat;
		_delay_action_list[index].current_ms = 0;
		_delay_action_list[index].type = DELAYACTION_TYPE_FN;
		_delay_action_list[index].data.fn.callback = fn->callback;
		_delay_action_list[index].data.fn.arg = fn->arg;
	}


	if (!qm_timer_is_valid(&_delay_action_timer))
	{
		qm_timer_new(&_delay_action_timer, _delay_action_timer_cb, NULL, CONFIG_EVENTBUS_DELAY_ACTION_TIMER_PERIOD_MS, 1);
		// if (ret != QM_EOK)
		// {
		// 	memset(&_delay_action_list[index], 0, sizeof(qm_eventbus_delayaction_t));
		// 	return ret;
		// }
		qm_timer_start(&_delay_action_timer);
		//update last_ms
		last_ms = qm_now_ms();
	}

	return index;
}

int qm_eventbus_delay_fn_stop(int busid, qm_void_callback_t *fn)
{
	//find action equal fn->callback fn->arg
	for (int i = 0; i < CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM; i++)
	{
		if ((_delay_action_list[i].id != DELAYACTION_INVALID_ID)
			&& (_delay_action_list[i].busid == busid)
			&& (_delay_action_list[i].type == DELAYACTION_TYPE_FN)
			&& (_delay_action_list[i].data.fn.callback == fn->callback)
			&& (_delay_action_list[i].data.fn.arg == fn->arg))
		{
			//found, remove it
			memset(&_delay_action_list[i], 0, sizeof(qm_eventbus_delayaction_t));
		}
	}

	//check if all action are free, if so, free timer
	for (int i = 0; i < CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM; i++)
	{
		if (_delay_action_list[i].id != DELAYACTION_INVALID_ID)
		{
			//found one valid action
			return QM_EOK;
		}
	}

	//here all action are free
	if (qm_timer_is_valid(&_delay_action_timer))
	{
		qm_timer_free(&_delay_action_timer);
	}

	return 0;
}

static void _delay_action_timer_cb(qm_timer_t *timer, void *arg)
{
	//post event to 
	uint32_t now_ms = qm_now_ms();

	qm_eventbus_tick_ev_t tick_ev;
	tick_ev.base_ev.module_id = SYS_MODULE_ID;
	tick_ev.base_ev.event_id = EMERGENCY_EVENT(SYS_EVENT_TICK_DELTA);
	tick_ev.time.delta = (now_ms > last_ms) ? (now_ms - last_ms) : (last_ms - now_ms);

	for (int i = 0; i < CONFIG_QM_EVENTBUS_MAX_BUS_NUM; i++)
	{
		qm_eventbus_post_event(i, (qm_eventbus_event_t *)&tick_ev, sizeof(qm_eventbus_tick_ev_t));
	}
	last_ms = now_ms;
}

int qm_eventbus_delay_action_handler(int busid, qm_eventbus_event_t *ev)
{
	//traverse all delay actions,
	//1. add action->current_ms, if >= action->time_ms, execute action
	//if repeat, action->current_ms = 0; else free action
	qm_eventbus_tick_ev_t tick_ev;
	int ret = qm_eventbus_peek_event_data(busid, ev, &tick_ev, sizeof(qm_eventbus_tick_ev_t));
	if (ret <= QM_EOK)
	{
		return ret;
	}
	if (EVENT_ID(ev->event_id) != SYS_EVENT_TICK_DELTA)
	{
		return QM_EOK;
	}
	for (int i = 0; i < CONFIG_EVENTBUS_DEALY_ACTION_MAX_NUM; i++)
	{
		if (_delay_action_list[i].id != DELAYACTION_INVALID_ID)
		{
			_delay_action_list[i].current_ms += tick_ev.time.delta;
			//busid not euqal, skip
			if ((busid ==_delay_action_list[i].busid) && (_delay_action_list[i].current_ms >= _delay_action_list[i].time_ms))
			{
				//execute action
				if (_delay_action_list[i].type == DELAYACTION_TYPE_FN)
				{
					if (_delay_action_list[i].data.fn.callback)
					{
						_delay_action_list[i].data.fn.callback(_delay_action_list[i].data.fn.arg);
					}
				}

				if (_delay_action_list[i].repeat)
				{
					_delay_action_list[i].current_ms = 0;
				}
				else
				{
					//free action
					memset(&_delay_action_list[i], 0, sizeof(qm_eventbus_delayaction_t));
				}
			}
		}
	}

	return 0;
}

