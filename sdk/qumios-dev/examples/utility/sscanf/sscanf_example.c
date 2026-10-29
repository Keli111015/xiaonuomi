#include "qm.h"
#include "qm_types.h"
#include "qm_sscanf.h"
#include "qm_log.h"
#include <inttypes.h>

#define LOG_TAG  "sscanf"

#define NUM_INT8_MIN    (-128)
#define NUM_INT16_MIN   (-32768)
#define NUM_INT32_MIN   (-2147483647L - 1L)
#define NUM_INT64_MIN   (-9223372036854775807LL - 1LL)

#define NUM_INT8_MAX    (127)
#define NUM_INT16_MAX   (32767)
#define NUM_INT32_MAX   (2147483647L)
#define NUM_INT64_MAX   (9223372036854775807LL)

#define NUM_UINT8_MIN   (0)
#define NUM_UINT16_MIN  (0)
#define NUM_UINT32_MIN  (0)
#define NUM_UINT64_MIN  (0)

#define NUM_UINT8_MAX   (255)
#define NUM_UINT16_MAX  (65535)
#define NUM_UINT32_MAX  (4294967295U)
#define NUM_UINT64_MAX  (18446744073709551615ULL)

#define STR_INT8_MIN    "-128"
#define STR_INT16_MIN   "-32768"
#define STR_INT32_MIN   "-2147483648"
#define STR_INT64_MIN   "-9223372036854775808"

#define STR_INT8_MAX    "127"
#define STR_INT16_MAX   "32767"
#define STR_INT32_MAX   "2147483647"
#define STR_INT64_MAX   "9223372036854775807"

#define STR_UINT8_MIN   "0"
#define STR_UINT16_MIN  "0"
#define STR_UINT32_MIN  "0"
#define STR_UINT64_MIN  "0"

#define STR_UINT8_MAX   "255"
#define STR_UINT16_MAX  "65535"
#define STR_UINT32_MAX  "4294967295"
#define STR_UINT64_MAX  "18446744073709551615"

#define STR_XINT8_MAX   "ff"
#define STR_XINT16_MAX  "ffff"
#define STR_XINT32_MAX  "ffffffff"
#define STR_XINT64_MAX  "ffffffffffffffff"

#define STR_FMT_PRINTF  "%hhi %hi %"PRIi32" %"PRIi64" %hhu %hu %"PRIu32" %"PRIu64
#define STR_FMT_SCANF   "%hhi %hi %"SCNi32" %"SCNi64" %hhu %hu %"SCNu32" %"SCNu64

#define STR_XFMT_PRINTF "%hhx %hx %"PRIx32" %"PRIx64" 0x%hhx 0x%hx 0x%"PRIx32" 0x%"PRIx64
#define STR_XFMT_SCANF  "%hhx %hx %"SCNx32" %"SCNx64" %hhx %hx %"SCNx32" %"SCNx64

typedef struct {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;

    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
}__attribute__((packed)) num_t;

static num_t num;                               /**< number holder */
static int res_glob;                            /**< global result */

/** Check if parsed numbers match preset
 */
static char * num_check(
    num_t *numx
)
{
    if (!memcmp(numx, &num, sizeof(num_t))) {
        return "ok";
    }

    res_glob = 1;
    return "fail";
}

static void num_reset(void)
{
    memset(&num, 0, sizeof(num));
}

void qm_application_start(void)
{
    num_t num0 = { -1, -2, -3, -4, 1, 2, 3, 4 };
    num_t num1 = { NUM_INT8_MIN, NUM_INT16_MIN, NUM_INT32_MIN, NUM_INT64_MIN, NUM_UINT8_MIN, NUM_UINT16_MIN, NUM_UINT32_MIN, NUM_UINT64_MIN };
    num_t num2 = { NUM_INT8_MAX, NUM_INT16_MAX, NUM_INT32_MAX, NUM_INT64_MAX, NUM_UINT8_MAX, NUM_UINT16_MAX, NUM_UINT32_MAX, NUM_UINT64_MAX };
    num_t num3 = { 0, 0, 0, 0, 0, 0, 0, 0 };
    num_t num4 = { (int8_t) NUM_UINT8_MAX, (int16_t) NUM_UINT16_MAX, (int32_t) NUM_UINT32_MAX, (int64_t) NUM_UINT64_MAX, NUM_UINT8_MAX, NUM_UINT16_MAX, NUM_UINT32_MAX, NUM_UINT64_MAX };
    uint32_t num5;
    uint32_t num6;
    uint32_t num7;
    uint32_t num8;

    num_reset();
    qm_sscanf("-1 -2 -3 -4 1 2 3 4", STR_FMT_SCANF, &num.i8, &num.i16, &num.i32, &num.i64, &num.u8, &num.u16, &num.u32, &num.u64);
    QM_LOGD(LOG_TAG,"in:  -1 -2 -3 -4 1 2 3 4");
    QM_LOGD(LOG_TAG,"out: "STR_FMT_PRINTF"", num.i8, num.i16, num.i32, num.i64, num.u8, num.u16, num.u32, num.u64);
    QM_LOGD(LOG_TAG,"res: %s", num_check(&num0));

    num_reset();
    qm_sscanf(STR_INT8_MIN " " STR_INT16_MIN " " STR_INT32_MIN " " STR_INT64_MIN " " STR_UINT8_MIN " " STR_UINT16_MIN " " STR_UINT32_MIN " " STR_UINT64_MIN, STR_FMT_SCANF, &num.i8, &num.i16, &num.i32, &num.i64, &num.u8, &num.u16, &num.u32, &num.u64);
    QM_LOGD(LOG_TAG,"in:  " STR_INT8_MIN " " STR_INT16_MIN " " STR_INT32_MIN " " STR_INT64_MIN " " STR_UINT8_MIN " " STR_UINT16_MIN " " STR_UINT32_MIN " " STR_UINT64_MIN "");
    QM_LOGD(LOG_TAG,"out: "STR_FMT_PRINTF"", num.i8, num.i16, num.i32, num.i64, num.u8, num.u16, num.u32, num.u64);
    QM_LOGD(LOG_TAG,"res: %s", num_check(&num1));

    num_reset();
    qm_sscanf(STR_INT8_MAX " " STR_INT16_MAX " " STR_INT32_MAX " " STR_INT64_MAX " " STR_UINT8_MAX " " STR_UINT16_MAX " " STR_UINT32_MAX " " STR_UINT64_MAX, STR_FMT_SCANF, &num.i8, &num.i16, &num.i32, &num.i64, &num.u8, &num.u16, &num.u32, &num.u64);
    QM_LOGD(LOG_TAG,"in:  " STR_INT8_MAX " " STR_INT16_MAX " " STR_INT32_MAX " " STR_INT64_MAX " " STR_UINT8_MAX " " STR_UINT16_MAX " " STR_UINT32_MAX " " STR_UINT64_MAX "");
    QM_LOGD(LOG_TAG,"out: "STR_FMT_PRINTF"", num.i8, num.i16, num.i32, num.i64, num.u8, num.u16, num.u32, num.u64);
    QM_LOGD(LOG_TAG,"res: %s", num_check(&num2));

    num_reset();
    qm_sscanf("0 0 0 0 0x00 0x0000 0x00000000 0x0000000000000000", STR_XFMT_SCANF, &num.i8, &num.i16, &num.i32, &num.i64, &num.u8, &num.u16, &num.u32, &num.u64);
    QM_LOGD(LOG_TAG,"in:  0 0 0 0 0x00 0x0000 0x00000000 0x0000000000000000");
    QM_LOGD(LOG_TAG,"out: "STR_XFMT_PRINTF"", num.i8, num.i16, num.i32, num.i64, num.u8, num.u16, num.u32, num.u64);
    QM_LOGD(LOG_TAG,"res: %s", num_check(&num3));

    num_reset();
    qm_sscanf(STR_XINT8_MAX " " STR_XINT16_MAX " " STR_XINT32_MAX " " STR_XINT64_MAX " 0x" STR_XINT8_MAX " 0x" STR_XINT16_MAX " 0x" STR_XINT32_MAX " 0x" STR_XINT64_MAX, STR_XFMT_SCANF, &num.i8, &num.i16, &num.i32, &num.i64, &num.u8, &num.u16, &num.u32, &num.u64);
    QM_LOGD(LOG_TAG,"in:  " STR_XINT8_MAX " " STR_XINT16_MAX " " STR_XINT32_MAX " " STR_XINT64_MAX " 0x" STR_XINT8_MAX " 0x" STR_XINT16_MAX " 0x" STR_XINT32_MAX " 0x" STR_XINT64_MAX "");
    QM_LOGD(LOG_TAG,"out: "STR_XFMT_PRINTF"", num.i8, num.i16, num.i32, num.i64, num.u8, num.u16, num.u32, num.u64);
    QM_LOGD(LOG_TAG,"res: %s", num_check(&num4));

    num_reset();
    qm_sscanf("abc-012.de", "abc-%3u.de", &num5);
    QM_LOGD(LOG_TAG,"in:  abc-012.de");
    QM_LOGD(LOG_TAG,"out: %u", num5);
    QM_LOGD(LOG_TAG,"res: %s", (num5 == 12) ? "ok" : "fail");

    num_reset();
    qm_sscanf("abc-12.de", "abc-%3u.de", &num5);
    QM_LOGD(LOG_TAG,"in:  abc-12.de");
    QM_LOGD(LOG_TAG,"out: %u", num5);
    QM_LOGD(LOG_TAG,"res: %s", (num5 == 12) ? "ok" : "fail");

    num_reset();
    qm_sscanf("abc-1234.de", "abc-%3u.de", &num5);
    QM_LOGD(LOG_TAG,"in:  abc-1234.de");
    QM_LOGD(LOG_TAG,"out: %u", num5);
    QM_LOGD(LOG_TAG,"res: %s", (num5 == 123) ? "ok" : "fail");

    num_reset();
    qm_sscanf("abc-1234.de def:54321", "abc-%3u4.de def:%1u%1u%1u", &num5, &num6, &num7, &num8);
    QM_LOGD(LOG_TAG,"in:  abc-1234.de def:54321");
    QM_LOGD(LOG_TAG,"out: %u, %u, %u, %u", num5, num6, num7, num8);
    QM_LOGD(LOG_TAG,"res: %s", ((num5 == 123) && (num6 == 5) && (num7 == 4) && (num8 == 3)) ? "ok" : "fail");

}