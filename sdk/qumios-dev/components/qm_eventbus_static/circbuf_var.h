#ifndef _UTIL_CIRCBUF_VAR_H_
#define _UTIL_CIRCBUF_VAR_H_

#include <stdint.h>

/**
 * @brief 可变数据长度的循环buffer，element的第一个字节用来表示数据的长度
 * 
 */

typedef struct {
	uint16_t push_index; //写入index
	uint16_t pop_index;  //读取index
	uint16_t size;       //总长度
    int16_t elem_cnt;   //当前elem的个数
	uint8_t * buffer;       //缓冲区
} circbuf_var_t;

typedef struct {
    uint8_t len;
	uint8_t data[0];
} circbuf_var_elem_header_t;

int circbuf_var_init(circbuf_var_t *circbuf, void *buffer, int size);
int circbuf_var_push_elem(circbuf_var_t *circbuf, void *elem, uint8_t size);
int circbuf_var_push_len(circbuf_var_t *circbuf, uint8_t len);
int circbuf_var_push_raw(circbuf_var_t *circbuf, void *elem, int len);
int circbuf_var_cur_elem_size(circbuf_var_t *circbuf);
int circbuf_var_pop (circbuf_var_t *circbuf, void *elem, int size, int read_only);
int circbuf_var_free_space(circbuf_var_t *circbuf);
int circbuf_var_elem_counts(circbuf_var_t *circ_buf);
int circbuf_var_drop_cur_elem(circbuf_var_t *circbuf);

#endif 

