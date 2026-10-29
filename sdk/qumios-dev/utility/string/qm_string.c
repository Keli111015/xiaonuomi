#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_string.h"

static int32_t qm_ascii_tolower(int32_t c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

char *qm_strdup(const char *s)
{
    char *t = NULL;

    if (s) {
        uint32_t len = qm_strlen(s) + 1;
        t = (char *)qm_malloc(len);
        if (t) {
            qm_memcpy(t, s, len);
        }
    }

    return t;
} 

uint32_t qm_strlen(const char *s)
{
    const char *p = s;

    if (!s) {
        return 0;
    }

    while (*p) {
        p++;
    }

    return (uint32_t)(p - s);
}

int32_t qm_strcmp(const char *s1, const char *s2)
{
    if (s1 == s2) {
        return 0;
    }

    if (!s1 || !s2) {
        return s1 ? 1 : -1;
    }

    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return (int32_t)(uint8_t)*s1 - (int32_t)(uint8_t)*s2;
}

int32_t qm_strncmp(const char *s1, const char *s2, uint32_t n)
{
    if (!n || s1 == s2) {
        return 0;
    }

    if (!s1 || !s2) {
        return s1 ? 1 : -1;
    }

    while (n--) {
        uint8_t c1 = (uint8_t)*s1++;
        uint8_t c2 = (uint8_t)*s2++;

        if (c1 != c2) {
            return (int32_t)c1 - (int32_t)c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}

int32_t qm_strcasecmp(const char *s1, const char *s2)
{
    int32_t c1;
    int32_t c2;

    if (s1 == s2) {
        return 0;
    }

    if (!s1 || !s2) {
        return s1 ? 1 : -1;
    }

    do {
        c1 = qm_ascii_tolower((uint8_t)*s1++);
        c2 = qm_ascii_tolower((uint8_t)*s2++);
        if (c1 != c2) {
            return c1 - c2;
        }
    } while (c1 != '\0');

    return 0;
}

int32_t qm_strncasecmp(const char *s1, const char *s2, uint32_t n)
{
    if (!n || s1 == s2) {
        return 0;
    }

    if (!s1 || !s2) {
        return s1 ? 1 : -1;
    }

    while (n--) {
        int32_t c1 = qm_ascii_tolower((uint8_t)*s1++);
        int32_t c2 = qm_ascii_tolower((uint8_t)*s2++);

        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}

char *qm_strchr(const char *s, int32_t c)
{
    char ch = (char)c;

    if (!s) {
        return NULL;
    }

    while (*s) {
        if (*s == ch) {
            return (char *)s;
        }
        s++;
    }

    return ch == '\0' ? (char *)s : NULL;
}

char *qm_strrchr(const char *s, int32_t c)
{
    const char *last = NULL;
    char ch = (char)c;

    if (!s) {
        return NULL;
    }

    do {
        if (*s == ch) {
            last = s;
        }
    } while (*s++);

    return (char *)last;
}

char *qm_strstr(const char *haystack, const char *needle)
{
    uint32_t needle_len;

    if (!haystack || !needle) {
        return NULL;
    }

    needle_len = qm_strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }

    for (; *haystack; haystack++) {
        if (*haystack == *needle && qm_strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }

    return NULL;
}

char *qm_strcasestr(const char *haystack, const char *needle)
{
    uint32_t needle_len;

    if (!haystack || !needle) {
        return NULL;
    }

    needle_len = qm_strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }

    for (; *haystack; haystack++) {
        if (qm_strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }

    return NULL;
}

char *qm_strcpy(char *dest, const char *src)
{
    char *ret = dest;

    if (!dest || !src) {
        return dest;
    }

    while ((*dest++ = *src++) != '\0') {
    }

    return ret;
}

char *qm_strncpy(char *dest, const char *src, uint32_t n)
{
    char *ret = dest;

    if (!dest || !src || !n) {
        return dest;
    }

    while (n && *src) {
        *dest++ = *src++;
        n--;
    }

    while (n--) {
        *dest++ = '\0';
    }

    return ret;
}

int32_t qm_memcmp(const void *s1, const void *s2, uint32_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    if (!n || s1 == s2) {
        return 0;
    }

    if (!s1 || !s2) {
        return s1 ? 1 : -1;
    }

    while (n--) {
        if (*p1 != *p2) {
            return (int32_t)*p1 - (int32_t)*p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

void *qm_memcpy(void *dest, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (!dest || !src || !n) {
        return dest;
    }

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

void *qm_memmove(void *dest, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (!dest || !src || !n || dest == src) {
        return dest;
    }

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }

    return dest;
}

void *qm_memset(void *s, int32_t c, uint32_t n)
{
    uint8_t *p = (uint8_t *)s;

    if (!s || !n) {
        return s;
    }

    while (n--) {
        *p++ = (uint8_t)c;
    }

    return s;
}

void *qm_memchr(const void *s, int32_t c, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    uint8_t ch = (uint8_t)c;

    if (!s || !n) {
        return NULL;
    }

    while (n--) {
        if (*p == ch) {
            return (void *)p;
        }
        p++;
    }

    return NULL;
}

void *qm_memstr(const void *haystack, uint32_t haystack_len, const char *needle)
{
    const uint8_t *p = (const uint8_t *)haystack;
    uint32_t needle_len;
    uint32_t i;

    if (!haystack || !needle) {
        return NULL;
    }

    needle_len = qm_strlen(needle);
    if (needle_len == 0) {
        return (void *)haystack;
    }

    if (needle_len > haystack_len) {
        return NULL;
    }

    for (i = 0; i <= haystack_len - needle_len; i++) {
        if (qm_memcmp(p + i, needle, needle_len) == 0) {
            return (void *)(p + i);
        }
    }

    return NULL;
}

int32_t qm_str2uint(char *input, uint8_t input_len, uint32_t *output)
{
    uint8_t index = 0;
    uint32_t temp = 0;

    for (index = 0; index < input_len; index++) {
        if (input[index] < '0' || input[index] > '9') {
            return -QM_EINVAL;
        }
        temp = temp * 10 + input[index] - '0';
    }
    *output = temp;

    return QM_EOK;
}

int32_t qm_str2uint64(char *input, uint8_t input_len, uint64_t *output)
{
    uint8_t index = 0;
    uint64_t temp = 0;

    for (index = 0; index < input_len; index++) {
        if (input[index] < '0' || input[index] > '9') {
            return -QM_EINVAL;
        }
        temp = temp * 10 + input[index] - '0';
    }
    *output = temp;

    return QM_EOK;
}

int32_t qm_uint2str(uint32_t input, char *output, uint8_t *output_len)
{
    uint8_t i = 0, j = 0;
    char temp[10] = {0};

    do {
        temp[i++] = input % 10 + '0';
    } while ((input /= 10) > 0);

    do {
        output[--i] = temp[j++];
    } while (i > 0);

    if (output_len) {
        *output_len = j;
    }

    return QM_EOK;
}

int32_t qm_int2hexstr(int32_t input, char *output, uint8_t *output_len)
{
    char *lower = "0123456789abcdef";
    uint8_t i = 0, j = 0;
    uint8_t nagative = 0;
    char temp[12] = {0};
    if(input < 0){
        nagative = 1;
        input *= -1;
    }

    do {
        temp[i++] = lower[input % 16];
    } while ((input /= 16) > 0);

    temp[i++] = 'x';
    temp[i++] = '0';
    if(nagative){
        temp[i++] = '-';
    }

    do {
        output[--i] = temp[j++];
    } while (i > 0);

    if (output_len) {
        *output_len = j;
    }

    return QM_EOK;
}

int32_t qm_uint642str(uint64_t input, char *output, uint8_t *output_len)
{
    uint8_t i = 0, j = 0;
    char temp[20] = {0};

    do {
        temp[i++] = input % 10 + '0';
    } while ((input /= 10) > 0);

    do {
        output[--i] = temp[j++];
    } while (i > 0);

    if (output_len) {
        *output_len = j;
    }

    return QM_EOK;
}

int32_t qm_int2str(int32_t input, char *output, uint8_t *output_len)
{
    uint8_t i = 0, j = 0, minus = 0;
    char temp[11] = {0};
    int64_t in = input;

    if (in < 0) {
        minus = 1;
        in = -in;
    }

    do {
        temp[minus + (i++)] = in % 10 + '0';
    } while ((in /= 10) > 0);

    do {
        output[minus + (--i)] = temp[minus + (j++)];
    } while (i > 0);

    if (minus == 1) {
        output[0] = '-';
    }

    if (output_len) {
        *output_len = j + minus;
    }

    return QM_EOK;
}

int32_t qm_hex2str(uint8_t *input, uint32_t input_len, char *output, uint8_t lowercase)
{
    char *upper = "0123456789ABCDEF";
    char *lower = "0123456789abcdef";
    char *encode = upper;
    int i = 0, j = 0;

    if (lowercase) {
        encode = lower;
    }

    for (i = 0; i < input_len; i++) {
        output[j++] = encode[(input[i] >> 4) & 0xf];
        output[j++] = encode[(input[i]) & 0xf];
    }

    return QM_EOK;
}

int32_t qm_str2hex(char *input, uint32_t input_len, uint8_t *output)
{
    uint32_t idx = 0;

    if (input_len % 2 != 0) {
        return -QM_EINVAL;
    }

    for (idx = 0; idx < input_len; idx += 2) {
        if (input[idx] >= '0' && input[idx] <= '9') {
            output[idx / 2] = (input[idx] - '0') << 4;
        } else if (input[idx] >= 'A' && input[idx] <= 'F') {
            output[idx / 2] = (input[idx] - 'A' + 0x0A) << 4;
        } else if (input[idx] >= 'a' && input[idx] <= 'f') {
            output[idx / 2] = (input[idx] - 'a' + 0x0A) << 4;
        }
        if (input[idx + 1] >= '0' && input[idx + 1] <= '9') {
            output[idx / 2] |= (input[idx + 1] - '0');
        } else if (input[idx + 1] >= 'A' && input[idx + 1] <= 'F') {
            output[idx / 2] |= (input[idx + 1] - 'A' + 0x0A);
        } else if (input[idx + 1] >= 'a' && input[idx + 1] <= 'f') {
            output[idx / 2] |= (input[idx + 1] - 'a' + 0x0A);
        }
    }

    return QM_EOK;
}

int32_t qm_json_value(const char *input, uint32_t input_len, const char *key, uint32_t key_len, char **value, uint32_t *value_len)
{
    uint32_t idx = 0;

    for (idx = 0; idx < input_len; idx++) {
        if (idx + key_len >= input_len) {
            return -QM_ERROR;
        }
        if ((qm_memcmp(&input[idx], key, key_len) == 0) &&
            ((idx > 0) && (input[idx - 1] == '"')) &&
            ((idx + key_len < input_len) && (input[idx + key_len] == '"'))) {
            idx += key_len;
            /* shortest ":x, or ":x} or ":x] */
            if ((idx + 2 >= input_len) ||
                (input[idx + 1] != ':')) {
                return -QM_ERROR;
            }
            idx += 2;
            if (input[idx] == '"') {
                *value = (char *)&input[++idx];
                for (; idx < input_len; idx++) {
                    if ((input[idx] == '"')) {
                        *value_len = (uint32_t)(idx - (*value - input));
                        return QM_EOK;
                    }
                }
            } else if (input[idx] == '{' || input[idx] == '[') {
                char start = input[idx];
                char end = (start == '{') ? ('}') : (']');
                uint8_t count = 0;
                *value = (char *)&input[idx];
                for (; idx < input_len; idx++) {
                    if ((input[idx] == start)) {
                        count++;
                    } else if ((input[idx] == end)) {
                        if (--count == 0) {
                            *value_len = (uint32_t)(idx - (*value - input) + 1);
                            return QM_EOK;
                        }
                    }
                }
            } else {
                *value = (char *)&input[idx];
                for (; idx < input_len; idx++) {
                    if ((input[idx] == ',' || input[idx] == ']' || input[idx] == '}')) {
                        *value_len = (uint32_t)(idx - (*value - input));
                        return QM_EOK;
                    }
                }
            }
        }
    }

    return -QM_ERROR;
}

int32_t qm_utils_sprintf(char **dest, char *fmt, char *src[], uint8_t count, char *module_name)
{
    char *buffer = NULL, *value = NULL;
    uint8_t idx = 0, percent_idx = 0;
    uint32_t buffer_len = 0;
    uint32_t fmt_len = qm_strlen(fmt);

    if(fmt_len  < 2 * count) {
        return -QM_EINVAL;
    }

    buffer_len += fmt_len - 2 * count;
    for (percent_idx = 0; percent_idx < count; percent_idx++) {
        value = (*(src + percent_idx) == NULL) ? ("") : (*(src + percent_idx));
        buffer_len += qm_strlen(value);
    }

    buffer = (char*)qm_malloc(buffer_len + 1);
    if (buffer == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(buffer, 0, buffer_len + 1);

    for (idx = 0, percent_idx = 0; idx < fmt_len;) {
        if (fmt[idx] == '%' && idx + 1 < fmt_len && fmt[idx + 1] == 's') {
            value = (*(src + percent_idx) == NULL) ? ("") : (*(src + percent_idx));
            qm_memcpy(buffer + qm_strlen(buffer), value, qm_strlen(value));
            percent_idx++;
            idx += 2;
        } else {
            buffer[qm_strlen(buffer)] = fmt[idx++];
        }
    }
    *dest = buffer;
    return QM_EOK;
}
