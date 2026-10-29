#ifndef _QM_SSCANF_H_
#define _QM_SSCANF_H_

#include "qm_types.h"
#include "qm_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if CONFIG_QM_SSCANF_SUPPORT

/*****************************************************************************/
/* Config */
/*****************************************************************************/
#ifndef CONFIG_QM_SSCANF_MAX_INT
#define CONFIG_QM_SSCANF_MAX_INT  8
#endif

/*****************************************************************************/
/* Defines */
/*****************************************************************************/
#if CONFIG_QM_SSCANF_MAX_INT == 1
#define QM_SSCANF_INT_T int8_t
#define QM_SSCANF_UINT_T uint8_t
#endif

#if CONFIG_QM_SSCANF_MAX_INT == 2
#define QM_SSCANF_INT_T int16_t
#define QM_SSCANF_UINT_T uint16_t
#endif

#if CONFIG_QM_SSCANF_MAX_INT == 4
#define QM_SSCANF_INT_T int32_t
#define QM_SSCANF_UINT_T uint32_t
#endif

#if CONFIG_QM_SSCANF_MAX_INT == 8
#define QM_SSCANF_INT_T int64_t
#define QM_SSCANF_UINT_T uint64_t
#endif

    /*****************************************************************************/
    /* Prototypes */
    /*****************************************************************************/
    int qm_sscanf(
        const char *str, /**< input string */
        const char *fmt, /**< format string */
        ...              /**< variable arguments */
        );

    const char *qm_s2i(
        const char *str,              /**< string */
        unsigned int width,           /**< width = sizeof(type) */
        QM_SSCANF_UINT_T num_max, /**< max num value */
        void *val,                    /**< value */
        unsigned int flg_neg,         /**< negative flag */
        unsigned int base,            /**< base */
        unsigned int max_field_width  /**< maximum field width */
    );

    int qm_c2i(
        const char chr,   /**< character */
        unsigned int base /**< base */
    );

#endif

#ifdef __cplusplus
}
#endif

#endif // _QM_SSCANF_H_