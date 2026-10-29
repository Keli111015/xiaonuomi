#ifndef __QM_TYPES_H__
#define __QM_TYPES_H__

#include "qm_config.h"

#ifndef CONFIG_QM_WITHOUT_MATH_H_SUPPORT
#define CONFIG_QM_WITHOUT_MATH_H_SUPPORT   0
#endif

#ifndef CONFIG_QM_WITHOUT_STDIO_H_SUPPORT
#define CONFIG_QM_WITHOUT_STDIO_H_SUPPORT   0
#endif

#ifndef CONFIG_QM_WITHOUT_STDBOOL_H_SUPPORT
#define CONFIG_QM_WITHOUT_STDBOOL_H_SUPPORT   0
#endif

#include <stdlib.h>
#include <string.h>
#if !CONFIG_QM_WITHOUT_STDIO_H_SUPPORT
#include <stdio.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#if !CONFIG_QM_WITHOUT_STDBOOL_H_SUPPORT
#include <stdbool.h>
#endif
#include <ctype.h>
#include <time.h>
#include <string.h>
#if !CONFIG_QM_WITHOUT_MATH_H_SUPPORT
#include <math.h>
#endif

#ifdef CONFIG_QM_TYPE_INT8_SUPPORT
#ifndef __INT8_TYPE__
#ifndef int8_t
typedef char             int8_t;
#endif
#endif

#endif

#ifdef CONFIG_QM_TYPE_UINT8_SUPPORT
#ifndef __INT8_TYPE__
#ifndef uint8_t
typedef unsigned char    uint8_t;
#endif
#endif

#endif

#ifdef CONFIG_QM_TYPE_INT16_SUPPORT
#ifndef __INT16_TYPE__

#ifndef int16_t
typedef short            int16_t;
#endif
#endif
#endif

#ifdef CONFIG_QM_TYPE_UINT16_SUPPORT
#ifndef __INT16_TYPE__
#ifndef uint16_t
typedef unsigned short   uint16_t;
#endif
#endif

#endif

#ifdef CONFIG_QM_TYPE_INT32_SUPPORT
#ifndef __INT32_TYPE__

#ifndef int32_t
typedef short            int32_t;
#endif
#endif
#endif

#ifdef CONFIG_QM_TYPE_UINT32_SUPPORT
#ifndef __INT32_TYPE__
#ifndef uint32_t
typedef unsigned int   uint32_t;
#endif
#endif

#endif

#ifdef CONFIG_QM_TYPE_SIZE_SUPPORT
#ifndef _SIZE_T
typedef unsigned int     size_t;
#endif
#endif

#ifdef CONFIG_QM_TYPE_INT32_SUPPORT
#ifndef INT32_MAX
typedef int                 int32_t;
#endif
#endif

#ifdef CONFIG_QM_TYPE_UINT32_SUPPORT
#ifndef UINT32_MAX
typedef unsigned int        uint32_t;
#endif
#endif

#ifdef CONFIG_QM_TYPE_INT64_SUPPORT
#ifndef __INT64_TYPE__

#ifndef int64_t
typedef long long        int64_t;
#endif
#endif

#endif

#ifdef CONFIG_QM_TYPE_FLOAT32_SUPPORT
#ifndef float32_t
typedef float            float32_t;
#endif
#endif

#ifdef CONFIG_QM_TYPE_FLOAT32_SUPPORT
#ifndef float64_t
typedef double           float64_t;
#endif
#endif

#ifndef true
#define true  1
#endif

#ifndef false
#define false  0
#endif

#ifndef bool_t
typedef unsigned char     bool_t;
#endif


typedef unsigned char           qm_u8;
typedef unsigned short          qm_u16;
typedef unsigned int            qm_u32;
typedef unsigned long           qm_ulong;

typedef signed char             qm_s8;
typedef short                   qm_s16;
typedef int                     qm_s32;
typedef long                    qm_slong;

#ifndef _M_IX86
typedef unsigned long long      qm_u64;
typedef long long               qm_s64;
#else
typedef __int64                 qm_u64;
typedef __int64                 qm_s64;
#endif

typedef char                    qm_char;
typedef char*                   qm_pchar;

typedef unsigned int            qm_size_t;

typedef float                   qm_float;
typedef double                  qm_double;
typedef unsigned char           qm_bool;


/* boolean type definitions */
#define QM_TRUE                         1               /**< boolean true  */
#define QM_FALSE                        0               /**< boolean fails */

#ifndef NULL
#define NULL                ((void *) 0)
#endif

#define swab16(x)                                                           \
    ((uint16_t)(                                                            \
    (((uint16_t)(x) & (uint16_t)0x00ffU) << 8) |                            \
    (((uint16_t)(x) & (uint16_t)0xff00U) >> 8) ))

#define swab32(x)                                                           \
    ((uint32_t)(                                                            \
    (((uint32_t)(x) & (uint32_t)0x000000ffUL) << 24) |                      \
    (((uint32_t)(x) & (uint32_t)0x0000ff00UL) <<  8)  |                     \
    (((uint32_t)(x) & (uint32_t)0x00ff0000UL) >>  8)  |                     \
    (((uint32_t)(x) & (uint32_t)0xff000000UL) >> 24) ))

#define swab64(x)                                                           \
    ((uint64_t)(                                                            \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x00000000000000ffULL) << 56) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x000000000000ff00ULL) << 40) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x0000000000ff0000ULL) << 24) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x00000000ff000000ULL) <<  8) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x000000ff00000000ULL) >>  8) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x0000ff0000000000ULL) >> 24) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0x00ff000000000000ULL) >> 40) |   \
    (uint64_t)(((uint64_t)(x) & (uint64_t)0xff00000000000000ULL) >> 56) ))


#ifdef CONFIG_CPU_BIGENDIAN
#define be64_to_cpu(x)      (x)
#define be32_to_cpu(x)      (x)
#define be16_to_cpu(x)      (x)
#define cpu_to_be64(x)      (x)
#define cpu_to_be32(x)      (x)
#define cpu_to_be16(x)      (x)
#else
#define be64_to_cpu(x)      swab64(x)
#define be32_to_cpu(x)      swab32(x)
#define be16_to_cpu(x)      swab16(x)
#define cpu_to_be64(x)      swab64(x)
#define cpu_to_be32(x)      swab32(x)
#define cpu_to_be16(x)      swab16(x)
#endif


#ifndef QM_ARRAY_SIZE
#define QM_ARRAY_SIZE(array) \
        (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef QM_MAC2STR
#define QM_MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define QM_MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#ifndef QM_UNUSED_ARG
#define QM_UNUSED_ARG(x) (void)x
#endif


int is_space(char c);
char *skip_spaces(const char *str);

#endif

