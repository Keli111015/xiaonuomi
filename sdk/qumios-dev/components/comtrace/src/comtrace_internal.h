#ifndef COMTRACE_INTERNAL_H
#define COMTRACE_INTERNAL_H

#include "qm_types.h"

static inline int comtrace_text_is_printable(const char *text, size_t length)
{
    size_t i;

    for (i = 0U; i < length; ++i) {
        if ((uint8_t)text[i] < 0x20U || (uint8_t)text[i] > 0x7eU)
            return 0;
    }
    return 1;
}

#endif
