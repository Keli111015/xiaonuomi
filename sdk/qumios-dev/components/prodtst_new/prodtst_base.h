#ifndef PRODTST_BASE_H
#define PRODTST_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include "qm_types.h"

char *cjson_str_get(cJSON *cjson, char *key);
int cjson_int_get(cJSON *cjson, char *key, int *data);
void *malloc_and_copy(void *data, int len);
int hexstrs_to_hex(char *str, int len, uint8_t **hex, int *hex_len);
int hex_to_hexstrs(uint8_t *hex, int hex_len, char **str, int *len);

#ifdef __cplusplus
}
#endif

#endif