/*
 * Copyright (c) 2020-2021 Siddharth Chandrasekaran <sidcha.dev@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "stddef.h"
#include "qm_errno.h"
#include "circbuf_var.h"

int circbuf_var_init(circbuf_var_t *circbuf, void *buffer, int size)
{
	if ((circbuf == NULL) || (buffer == NULL) || (size <= 0))
	{
		return -QM_EINVAL;
	}

	circbuf->push_index = 0;
	circbuf->pop_index = 0;
	circbuf->size = size;
	circbuf->elem_cnt = 0;
	circbuf->buffer = (uint8_t *)buffer;

	return 0;

}

/**
 * @brief 
 * 
 * @param circ_buf 
 * @param elem 
 * @param read_only : 1:只读,不移动pop指针，实现peek功能;
 * @return int 失败返回<0 成功：返回pop出的数据长度
 */
int circbuf_var_pop(circbuf_var_t *circ_buf, void *elem, int size, int read_only)
{
	int pop_size = 0;

	//判断参数是否有效
	if ((circ_buf == NULL) || (elem == NULL) || (circ_buf->size == 0))
	{
		return -QM_EINVAL;
	}

	//判断当前是否有数据
	if (circ_buf->elem_cnt == 0)
	{
		return -QM_ENOMEM;
	}

	int old_pop_index = circ_buf->pop_index;

	//判断elem缓冲区是否足够
	int elem_size = circ_buf->buffer[circ_buf->pop_index];
	if (size < elem_size)
	{
		pop_size = size;
	}
	else
	{
		pop_size = elem_size;
	}
	//忽略第一个字节，因为第一个字节表示数据长度
	circ_buf->pop_index = (circ_buf->pop_index + 1) % (circ_buf->size);
	for (int i = 0; i < pop_size; i++)
	{
		*((uint8_t*)elem + i) = circ_buf->buffer[circ_buf->pop_index];
#if defined(CRICBUF_CLEAN_ON_POP)
		if (!read_only)
		{
			circ_buf->buffer[circ_buf->pop_index] = 0;
		}
#endif
		circ_buf->pop_index = (circ_buf->pop_index + 1) % (circ_buf->size);
	}
	if (read_only)
	{
		//只读，将pop_index回退
		circ_buf->pop_index = old_pop_index;
	}
	else
	{
		circ_buf->elem_cnt--;
	}

	return pop_size;
}

/**
 * @brief 丢弃当前的元素
 * 
 * @param circbuf 
 * @return int 
 */
int circbuf_var_drop_cur_elem(circbuf_var_t *circbuf)
{
	if ((NULL == circbuf) || (circbuf->elem_cnt == 0) || (circbuf->size == 0)) 
	{
		return -QM_EINVAL;
	}
	uint8_t header = circbuf->buffer[circbuf->pop_index];
	circbuf->pop_index = (circbuf->pop_index + header + 1) % (circbuf->size);
	circbuf->elem_cnt--;
	return 0;
}

int circbuf_var_cur_elem_size(circbuf_var_t *circbuf)
{
	if ((NULL == circbuf) || (circbuf->elem_cnt == 0) || (circbuf->size == 0)) 
	{
		return -QM_EINVAL;
	}
	uint8_t header = circbuf->buffer[circbuf->pop_index % circbuf->size];
	return header + 1;
}

/**
 * @brief 
 * 
 * @param circ_buf 
 * @param elem : 第一个字节表示数据长度
 * @return int: 0:成功，<0:失败
 */
int circbuf_var_push_elem(circbuf_var_t *circ_buf, void *elem, uint8_t size)
{
	uint8_t *head;

	if ((circ_buf == NULL) || (elem == NULL) || (size == 0) || (circ_buf->size == 0))
	{
		return -QM_EINVAL;
	}

	//判断空间是否足够
	if (circbuf_var_free_space(circ_buf) < (size + 1))
	{
		return -QM_ENOMEM;
	}

	//写入数据,需要考虑循环的情况
	//写入长度信息
	circ_buf->buffer[circ_buf->push_index % circ_buf->size] = (uint8_t)size;
	circ_buf->push_index = (circ_buf->push_index + 1) % (circ_buf->size);
	for (int i = 0; i < size; i++)
	{
		circ_buf->buffer[circ_buf->push_index] = *((uint8_t*)elem + i);
		circ_buf->push_index = (circ_buf->push_index + 1) % (circ_buf->size);
	}
	circ_buf->elem_cnt++;
	
	return 0;
}

#if 0
int circbuf_var_push_len(circbuf_var_t *circbuf, uint8_t len)
{
	if ((circbuf == NULL) || (len <= 0) || (circbuf->size == 0))
	{
		return -QM_EINVAL;
	}

	//判断空间是否足够
	if (circbuf_var_free_space(circbuf) < len + 1)
	{
		return -QM_ENOMEM;
	}

	//写入数据,需要考虑循环的情况
	{
		circbuf->buffer[circbuf->push_index] = len;
		circbuf->push_index = (circbuf->push_index + 1) % (circbuf->size);
	}
	circbuf->elem_cnt++;
	
	return 0;

}

int circbuf_var_push_raw(circbuf_var_t *circbuf, void *elem, int len)
{
	if ((circbuf == NULL) || (elem == NULL) || (len <= 0) || (circbuf->size == 0))
	{
		return -QM_EINVAL;
	}

	//判断空间是否足够
	if (circbuf_var_free_space(circbuf) < len)
	{
		return -QM_ENOMEM;
	}

	//写入数据,需要考虑循环的情况
	for (int i = 0; i < len; i++)
	{
		circbuf->buffer[circbuf->push_index] = *((uint8_t*)elem + i);
		circbuf->push_index = (circbuf->push_index + 1) % (circbuf->size);
	}
	circbuf->elem_cnt++;
	
	return 0;
}
#endif

/**
 * @brief  返回当前缓冲区中，剩余的字节数
 * 
 * @param circ_buf 
 * @return int 
 */
int circbuf_var_free_space(circbuf_var_t *circ_buf)
{
	int total;

	total = (circ_buf->push_index - circ_buf->pop_index + circ_buf->size) % circ_buf->size;

	return circ_buf->size - total - 1;
}


/**
 * @brief 当前循环缓冲区使用了多少空间
 * 
 * @param circ_buf 
 * @return int 
 */
int circbuf_var_size(circbuf_var_t *circ_buf)
{
	int total;

	total = (circ_buf->push_index - circ_buf->pop_index + circ_buf->size) % circ_buf->size;

	return total;
}

/**
 * @brief  当前循环缓冲区有多少个元素(element)
 * 
 * @param circ_buf 
 * @return int 
 */
int circbuf_var_elem_counts(circbuf_var_t *circ_buf)
{
	return circ_buf->elem_cnt;
}
