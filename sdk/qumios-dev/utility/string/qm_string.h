#ifndef _QM_STRING_H_
#define _QM_STRING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

char *qm_strdup(const char *s);
uint32_t qm_strlen(const char *s);
int32_t qm_strcmp(const char *s1, const char *s2);
int32_t qm_strncmp(const char *s1, const char *s2, uint32_t n);
/** Case-insensitive string comparison. */
int32_t qm_strcasecmp(const char *s1, const char *s2);
/** Case-insensitive string comparison, limited to n characters. */
int32_t qm_strncasecmp(const char *s1, const char *s2, uint32_t n);
char *qm_strchr(const char *s, int32_t c);
/** Find the last occurrence of a character in a string. */
char *qm_strrchr(const char *s, int32_t c);
char *qm_strstr(const char *haystack, const char *needle);
/** Case-insensitive substring search. */
char *qm_strcasestr(const char *haystack, const char *needle);
char *qm_strcpy(char *dest, const char *src);
char *qm_strncpy(char *dest, const char *src, uint32_t n);
int32_t qm_memcmp(const void *s1, const void *s2, uint32_t n);
void *qm_memcpy(void *dest, const void *src, uint32_t n);
void *qm_memmove(void *dest, const void *src, uint32_t n);
void *qm_memset(void *s, int32_t c, uint32_t n);
/** Find one byte in a memory region. */
void *qm_memchr(const void *s, int32_t c, uint32_t n);
/** Find a NUL-terminated string inside a bounded memory region. */
void *qm_memstr(const void *haystack, uint32_t haystack_len, const char *needle);
int32_t qm_str2uint(char *input, uint8_t input_len, uint32_t *output);
int32_t qm_str2uint64(char *input, uint8_t input_len, uint64_t *output);
int32_t qm_uint2str(uint32_t input, char *output, uint8_t *output_len);
int32_t qm_int2hexstr(int32_t input, char *output, uint8_t *output_len);
int32_t qm_uint642str(uint64_t input, char *output, uint8_t *output_len);
int32_t qm_int2str(int32_t input, char *output, uint8_t *output_len);
int32_t qm_hex2str(uint8_t *input, uint32_t input_len, char *output, uint8_t lowercase);
int32_t qm_str2hex(char *input, uint32_t input_len, uint8_t *output);
int32_t qm_json_value(const char *input, uint32_t input_len, const char *key, uint32_t key_len, char **value, uint32_t *value_len);
int32_t qm_utils_sprintf(char **dest, char *fmt, char *src[], uint8_t count, char *module_name);

#ifdef __cplusplus
}
#endif

#endif
