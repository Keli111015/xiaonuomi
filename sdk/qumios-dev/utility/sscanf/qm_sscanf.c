#include "qm_sscanf.h"

#if CONFIG_QM_SSCANF_SUPPORT
#include <limits.h>

/*****************************************************************************/
/** Pinkie Just Enough Sscanf To Work
 *
 * Supports the following formatters:
 *   - %i and %u, both with ll and hh modifiers
 *   - %n
 */
int qm_sscanf(
    const char *str,                            /**< input string */
    const char *fmt,                            /**< format string */
    ...                                         /**< variable arguments */
)
{
    va_list ap;                                 /* variable argument list */
    uint32_t flg_format = 0;                /* format flag */
    uint32_t int_width = 0;                 /* integer width */
    uint32_t max_field_width = 0;           /* maximum character count */
    const char *str_beg = str;                  /* string begin */
    int args = 0;                               /* parsed arguments counter */
    uint32_t flg_neg = 0;                   /* negative flag */

    va_start(ap, fmt);
    for (; (*fmt) && (*str); fmt++) {

        if (flg_format) {

            /* length field */
            if ('h' == *fmt) {
                int_width = (!int_width) ? sizeof(uint16_t) : sizeof(uint8_t);
                continue;
            }

            if ('l' == *fmt) {
                int_width = (!int_width) ? sizeof(uint32_t) : sizeof(uint64_t);
                continue;
            }

            /* number width */
            if (('0' <= *fmt) && ('9' >= *fmt)) {
                fmt = qm_s2i(fmt, sizeof(uint32_t), UINT_MAX, &max_field_width, 0, 10, 0);
            }

            /* handle conversion */
            switch (*fmt) {

                case 'i':

                    /* detect negative sign */
                    if ('-' == *str) {
                        flg_neg = 1;
                        fmt--;
                        str++;
                        continue;
                    }

                    /* fallthrough to convert number */
                    break;
                case 'x':
                case 'u':
                    /* uint32_teger */
                    if (!int_width) {
                        str = qm_s2i(str, sizeof(uint32_t), UINT_MAX, va_arg(ap, uint32_t *), flg_neg, ('x' == *fmt) ? 16 : 0, max_field_width);
                    }
                    else if (sizeof(uint8_t) == int_width) {
                        str = qm_s2i(str, sizeof(uint8_t), UINT8_MAX, va_arg(ap, uint8_t *), flg_neg, ('x' == *fmt) ? 16 : 0, max_field_width);
                    }
#if (CONFIG_QM_SSCANF_MAX_INT >= 2) && (UINT16_MAX != UINT_MAX)
                    else if (sizeof(uint16_t) == int_width) {
                        str = qm_s2i(str, sizeof(uint16_t), UINT16_MAX, va_arg(ap, uint16_t *), flg_neg, ('x' == *fmt) ? 16 : 0, max_field_width);
                    }
#endif
#if (CONFIG_QM_SSCANF_MAX_INT >= 4) && (UINT32_MAX != UINT_MAX)
                    else if (sizeof(uint32_t) == int_width) {
                        str = qm_s2i(str, sizeof(uint32_t), UINT32_MAX, va_arg(ap, uint32_t *), flg_neg, ('x' == *fmt) ? 16 : 0, max_field_width);
                    }
#endif
#if (CONFIG_QM_SSCANF_MAX_INT >= 8) && (UINT64_MAX != UINT_MAX)
                    else if (sizeof(uint64_t) == int_width) {
                        str = qm_s2i(str, sizeof(uint64_t), UINT64_MAX, va_arg(ap, uint64_t *), flg_neg, ('x' == *fmt) ? 16 : 0, max_field_width);
                    }
#endif

                    /* reset integer width */
                    int_width = 0;

                    /* reset maximum field width */
                    max_field_width = 0;

                    /* update args */
                    args++;

                    break;

                case '%':
                    /* percent char */
                    flg_format = 0;
                    goto sscanf_match;
                    break;
                case 'n':
                    /* position */
                    *(va_arg(ap, int *)) = (int) (str - str_beg);
                    break;
            }

            flg_format = 0;
            flg_neg = 0;
            continue;
        }

        if ('%' == *fmt) {
            flg_format = 1;
            continue;
        }

sscanf_match:
        /* string content must match format */
        if (*fmt != *str++) {
            break;
        }
    }
    va_end(ap);

    return args;
}


/*****************************************************************************/
/** String To Integer
 */
const char * qm_s2i(
    const char *str,                            /**< string */
	unsigned int width,                         /**< width = sizeof(type) */
    QM_SSCANF_UINT_T num_max,               /**< max num value */
    void *val,                                  /**< value */
	unsigned int flg_neg,                       /**< negative flag */
	unsigned int base,                          /**< base */
	unsigned int max_field_width                /**< maximum field width */
)
{
    QM_SSCANF_UINT_T num = 0;               /* number */
    QM_SSCANF_UINT_T mul = 1;               /* multiplicator */
    QM_SSCANF_UINT_T cur;                   /* current number */
    uint32_t cnt = 0;                       /* counter */
    const char *str_end;                        /* number end */

    /* hex detection */
    if ((0 == base) || (16 == base)) {
        if (((str[0]) && ('0' == str[0])) && ((str[1]) && ('x' == str[1]))) {
            base = 16;
            str += 2;
        }
    }

    /* default to base 10 */
    if (0 == base) {
        base = 10;
    }

    /* count numbers */
    for (; (*str) && (-1 != qm_c2i(*str, base)); str++) {
        if ((max_field_width) && (max_field_width <= cnt)) {
            break;
        }
        cnt++;
    }

    /* store number end */
    str_end = str;

    /* check if anything was detected */
    if (!cnt) {
        goto bail;
    }

    /* convert integers */
    while (cnt--) {
        str--;

        /* apply multiplicator to conv result */
        cur = (QM_SSCANF_UINT_T) qm_c2i(*str, base) * mul;

        if ((num_max - cur) < num) {
            str_end = 0;
            goto bail;
        }

        num += cur;
        mul *= base;
    }

bail:
    /* convert result to given width */
    if (sizeof(uint8_t) == width) {
        *((uint8_t *) val) = (flg_neg) ? (uint8_t) -num : (uint8_t) num;
    }
#if CONFIG_QM_SSCANF_MAX_INT >= 2
    else if (sizeof(uint16_t) == width) {
        *((uint16_t *) val) = (flg_neg) ? (uint16_t) -num : (uint16_t) num;
    }
#endif
#if CONFIG_QM_SSCANF_MAX_INT >= 4
    else if (sizeof(uint32_t) == width) {
        *((uint32_t *) val) = (flg_neg) ? (uint32_t) -num : (uint32_t) num;
    }
#endif
#if CONFIG_QM_SSCANF_MAX_INT >= 8
    else if (sizeof(uint64_t) == width) {
        *((uint64_t *) val) = (flg_neg) ? (uint64_t) -num : (uint64_t) num;
    }
#endif

    return str_end;
}


/*****************************************************************************/
/** Convert character to integer
 *
 * @returns number or -1 if not a number
 */
int qm_c2i(
    const char chr,                             /**< character */
	unsigned int base                           /**< base */
)
{
    if (('0' <= chr) && ('9' >= chr)) {
        return (chr - '0');
    }

    if (16 == base) {
        if (('a' <= chr) && ('f' >= chr)) {
            return (chr - 'a' + 10);
        }

        if (('A' <= chr) && ('F' >= chr)) {
            return (chr - 'A' + 10);
        }
    }

    return -1;
}

#endif
