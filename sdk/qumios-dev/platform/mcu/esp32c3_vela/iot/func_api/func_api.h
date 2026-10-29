#ifndef _FUNC_API_H_
#define _FUNC_API_H_

#include "qm_types.h"

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap); //自行实现 vsnprintf
int sscanf(const char *str, const char *format, ...);               //自行实现 sscanf
void get_last_str(char *data, char *recv_data, uint8_t char_n);     //自行实现提取指定字符前的字符串
#endif

