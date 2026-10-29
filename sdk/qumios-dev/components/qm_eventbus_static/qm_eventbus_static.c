#include "stdint.h"
#include "stddef.h"
#include "qm_eventbus_static.h"
#include "circbuf_var.h"
#include "qm_errno.h"

typedef struct {
	circbuf_var_t event_queue;
	circbuf_var_t emerge_event_queue;
	qm_eventbus_event_table_t *event_table;
}qm_eventbus_t;

static qm_eventbus_t _eventbus[CONFIG_QM_EVENTBUS_MAX_BUS_NUM] = {0};
// static qm_eventbus_info_t _eventbus_info[CONFIG_QM_EVENTBUS_MAX_BUS_NUM] = {0};

int qm_eventbus_init(int bus_id, void *buffer, int size, qm_eventbus_event_table_t *event_table, int emergency_percent)
{
	if ((bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) || (buffer == NULL) || (size <= 0) || (event_table == NULL))
	{
		return -QM_EINVAL;
	}

	//判断size是否4字节对齐
	if (size % 4 != 0)
	{
		return -QM_EINVAL;
	}

	_eventbus[bus_id].event_table = event_table;

	//计算emergency buffer的大小。如果CONFIG_QM_EVENTBUS_EMERGENCY_PERCENT为0，则不分配emergency buffer
	//并且需要注意4字节对齐
	int emerge_buf_size = 0;
	if (emergency_percent > 0)
	{
		emerge_buf_size = size * emergency_percent / 100;
	}
	if (emerge_buf_size % 4 != 0)
	{
		emerge_buf_size = (emerge_buf_size / 4 + 1) * 4;
	}

	//初始化circbuf_var
	circbuf_var_init(&_eventbus[bus_id].event_queue, buffer, size-emerge_buf_size);
	circbuf_var_init(&_eventbus[bus_id].emerge_event_queue, (uint8_t*)buffer+size-emerge_buf_size, emerge_buf_size);


	return 0;
}

int qm_eventbus_post_event(int bus_id, qm_eventbus_event_t *ev, int size)
{
	if ((bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) || (ev == NULL) || (size <= 0))
	{
		return -QM_EINVAL;
	}
	//判断eventbus是否初始化
	if (_eventbus[bus_id].event_table == NULL)
	{
		return -QM_ENOENT;
	}

	//判断是否有注册的handler,
	//  1. 存在module_id, 并且event_id为ALL_EVENT 或者
	//  2. 存在module_id, 并且event_id为ev.event_id
	int i;
	for (i = 0; _eventbus[bus_id].event_table[i].event.module_id != NULL_MODULE_ID; i++)
	{
		if ((_eventbus[bus_id].event_table[i].event.module_id == ev->module_id) &&
			((_eventbus[bus_id].event_table[i].event.event_id == ALL_EVENT) || (EVENT_ID(_eventbus[bus_id].event_table[i].event.event_id) == EVENT_ID(ev->event_id))))
		{
			break;
		}
	}
	//没有找到对应的handler
	if (_eventbus[bus_id].event_table[i].event.module_id == NULL_MODULE_ID)
	{
		return -QM_ENOENT;
	}


	//写入数据
	// 1. 写入event_id
	// 2. 写入data
	circbuf_var_t *circbuf = IS_EMERGE_EVENT(ev->event_id) ? &_eventbus[bus_id].emerge_event_queue : &_eventbus[bus_id].event_queue;

	return circbuf_var_push_elem(circbuf, ev, size);
}

int qm_eventbus_process_events(int bus_id)
{
	if (bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM)
	{
		return -QM_EINVAL;
	}

	//判断eventbus是否初始化
	if (_eventbus[bus_id].event_table == NULL)
	{
		return -QM_ENOENT;
	}

	//处理紧急事件
	while (circbuf_var_elem_counts(&_eventbus[bus_id].emerge_event_queue) > 0)
	{
		//读取event_id
		qm_eventbus_event_t ev;
		//以只读方式是读取event
		circbuf_var_pop(&_eventbus[bus_id].emerge_event_queue, &ev, 2, 1); 

		//找到对应的handler, 可能有多个handler
		int i;
		for (i = 0; _eventbus[bus_id].event_table[i].event.module_id != NULL_MODULE_ID; i++)
		{
			if ((_eventbus[bus_id].event_table[i].event.module_id == ev.module_id) &&
				((_eventbus[bus_id].event_table[i].event.event_id == ALL_EVENT) || (EVENT_ID(_eventbus[bus_id].event_table[i].event.event_id) == EVENT_ID(ev.event_id))))
			{
				//找到对应的handler
				//调用handler
				_eventbus[bus_id].event_table[i].handler(bus_id, &ev);
			}
		}
		//处理完event，将其从queue中删除
		circbuf_var_drop_cur_elem(&_eventbus[bus_id].emerge_event_queue);
	}

	//处理普通事件, 每次处理一个
	// if (circbuf_var_elem_counts(&_eventbus[bus_id].event_queue) > 0)
	while (circbuf_var_elem_counts(&_eventbus[bus_id].event_queue) > 0)
	{
		//读取event_id
		qm_eventbus_event_t ev;
		//以只读方式是读取event
		circbuf_var_pop(&_eventbus[bus_id].event_queue, &ev, 2, 1); 

		//找到对应的handler, 可能有多个handler
		int i;
		for (i = 0; _eventbus[bus_id].event_table[i].event.module_id != NULL_MODULE_ID; i++)
		{
			if ((_eventbus[bus_id].event_table[i].event.module_id == ev.module_id) &&
				((_eventbus[bus_id].event_table[i].event.event_id == ALL_EVENT) || (EVENT_ID(_eventbus[bus_id].event_table[i].event.event_id) == EVENT_ID(ev.event_id))))
			{
				//找到对应的handler
				//调用handler
				_eventbus[bus_id].event_table[i].handler(bus_id, &ev);
			}
		}
		//处理完event，将其从queue中删除
		circbuf_var_drop_cur_elem(&_eventbus[bus_id].event_queue);
	}

	return circbuf_var_elem_counts(&_eventbus[bus_id].event_queue) + circbuf_var_elem_counts(&_eventbus[bus_id].emerge_event_queue);
}

int qm_eventbus_peek_event_data(int bus_id, qm_eventbus_event_t *ev, void *data, int size)
{
	//正常情况下ev应该是eventqueue 中的第一个元素
	if ((bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) || (data == NULL) || (size <= 0))
	{
		return -QM_EINVAL;
	}

	//只读方式读取
	if (IS_EMERGE_EVENT(ev->event_id))
	{
		return circbuf_var_pop(&_eventbus[bus_id].emerge_event_queue, data, size, 1);
	}
	else
	{
		return circbuf_var_pop(&_eventbus[bus_id].event_queue, data, size, 1);
	}
}


int qm_eventbus_event_counts(int bus_id, int type)
{
	if (bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM)
	{
		return -QM_EINVAL;
	}
	int normal_counts = circbuf_var_elem_counts(&_eventbus[bus_id].event_queue);
	int emerge_counts = circbuf_var_elem_counts(&_eventbus[bus_id].emerge_event_queue);

	if (type == QM_EVENTBUS_NORMAL_EVENT)
	{
		return normal_counts;
	}
	else if (type == QM_EVENTBUS_EMERGENCY_EVENT)
	{
		return emerge_counts;
	}
	else if (type == QM_EVENTBUS_ALL_EVENT)
	{
		return normal_counts + emerge_counts;
	}
	else
	{
		return -QM_EINVAL;
	}
}

int qm_eventbus_call_event(int bus_id, qm_eventbus_event_t *ev, int size)
{
	int ret = 0;
	if ((bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) || (ev == NULL) || (size <= 0))
	{
		return -QM_EINVAL;
	}

	//判断eventbus是否初始化
	if (_eventbus[bus_id].event_table == NULL)
	{
		return -QM_ENOENT;
	}

	//判断是否需要将event_id和data写入queue
	if (size > sizeof(qm_eventbus_event_t))
	{
		//push data into queue
		ret = qm_eventbus_post_event(bus_id, ev, size); 
		if (ret < 0)
		{
			return ret;
		}
	}

	//找到对应的handler, 可能有多个handler
	int i;
	for (i = 0; _eventbus[bus_id].event_table[i].event.module_id != NULL_MODULE_ID; i++)
	{
		if ((_eventbus[bus_id].event_table[i].event.module_id == ev->module_id) &&
			((_eventbus[bus_id].event_table[i].event.event_id == ALL_EVENT) || (EVENT_ID(_eventbus[bus_id].event_table[i].event.event_id) == EVENT_ID(ev->event_id))))
		{
			//找到对应的handler
			//调用handler
			_eventbus[bus_id].event_table[i].handler(bus_id, ev);
		}
	}
	if (size > sizeof(qm_eventbus_event_t))
	{
		//处理完event，将其从queue中删除
		if (IS_EMERGE_EVENT(ev->event_id))
		{
			circbuf_var_drop_cur_elem(&_eventbus[bus_id].emerge_event_queue);
		}
		else
		{
			circbuf_var_drop_cur_elem(&_eventbus[bus_id].event_queue);
		}
	}

	return 0;
}
