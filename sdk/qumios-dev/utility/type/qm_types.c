#include "qm_types.h"

int is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\v' ||
            c == '\f' || c == '\r' || c == '\n');
}

char *skip_spaces(const char *str)
{
    while (is_space(*str))
    {
        ++str;
    }
    return (char *)str;
}



