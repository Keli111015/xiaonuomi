#ifndef _QM_UTILS_STRINGS_H_
#define _QM_UTILS_STRINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


uint32_t hex_str_to_num(const char*str, uint8_t len);
uint32_t int_str_to_num(const char *str, uint8_t len);

int32_t hex_str_to_nums(char* hex_str, int len, uint8_t* num_str );

int32_t hex_to_strs(uint8_t*in_buf, uint8_t len, char* out_buf);

int32_t hex_to_strs_print(uint8_t*in_buf, uint8_t len, char* out_buf);

#ifdef __cplusplus
}
#endif

#endif
