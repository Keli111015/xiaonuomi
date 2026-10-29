#ifndef __QM_CHECK_H__
#define __QM_CHECK_H__

#include "qm_errno.h"
#include "qm_log.h"
#include "qm_config.h"

/*
 * The likely and unlikely macro pairs:
 * These macros are useful to place when application
 * knows the majority ocurrence of a decision paths,
 * placing one of these macros can hint the compiler
 * to reorder instructions producing more optimized
 * code.
 */
#if (CONFIG_QM_COMPILER_OPTIMIZATION_PERF)
#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif
#else
#ifndef likely
#define likely(x)      (x)
#endif
#ifndef unlikely
#define unlikely(x)    (x)
#endif
#endif

#if CONFIG_QM_CHECK_SUPPORT
/**
 * Macro which can be used to check the error code. If the code is not QM_EOK, it prints the message and returns.
 */
#if CONFIG_QM_CHECK_SILENT

#define QM_RETURN_ON_ERROR(x, log_tag, format, ...) do {                                       \
        qm_err_t err_rc_ = (x);                                                                \
        if (unlikely(err_rc_ != QM_EOK)) {                                                      \
            return err_rc_;                                                                     \
        }                                                                                       \
    } while(0)

/**
 * Macro which can be used to check the error code. If the code is not QM_EOK, it prints the message,
 * sets the local variable 'ret' to the code, and then exits by jumping to 'goto_tag'.
 */
#define QM_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ...) do {                               \
        qm_err_t err_rc_ = (x);                                                                \
        if (unlikely(err_rc_ != QM_EOK)) {                                                      \
            ret = err_rc_;                                                                      \
            goto goto_tag;                                                                      \
        }                                                                                       \
    } while(0)


/**
 * Macro which can be used to check the condition. If the condition is not 'true', it prints the message
 * and returns with the supplied 'err_code'.
 */
#define QM_RETURN_ON_FALSE(a, err_code, log_tag, format, ...) do {                             \
        if (unlikely(!(a))) {                                                                   \
            return err_code;                                                                    \
        }                                                                                       \
    } while(0)

/**
 * Macro which can be used to check the condition. If the condition is not 'true', it prints the message,
 * sets the local variable 'ret' to the supplied 'err_code', and then exits by jumping to 'goto_tag'.
 */
#define QM_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...) do {                     \
        if (unlikely(!(a))) {                                                                   \
            ret = err_code;                                                                     \
            goto goto_tag;                                                                      \
        }                                                                                       \
    } while (0)

#else 
/**
 * Macro which can be used to check the error code. If the code is not QM_EOK, it prints the message and returns.
 */
#define QM_RETURN_ON_ERROR(x, log_tag, format, ...) do {                                       \
        qm_err_t err_rc_ = (x);                                                                \
        if (unlikely(err_rc_ != QM_EOK)) {                                                      \
            QM_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);        \
            return err_rc_;                                                                     \
        }                                                                                       \
    } while(0)

/**
 * Macro which can be used to check the error code. If the code is not QM_EOK, it prints the message,
 * sets the local variable 'ret' to the code, and then exits by jumping to 'goto_tag'.
 */
#define QM_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ...) do {                               \
        qm_err_t err_rc_ = (x);                                                                \
        if (unlikely(err_rc_ != QM_EOK)) {                                                      \
            QM_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);        \
            ret = err_rc_;                                                                      \
            goto goto_tag;                                                                      \
        }                                                                                       \
    } while(0)

/**
 * Macro which can be used to check the condition. If the condition is not 'true', it prints the message
 * and returns with the supplied 'err_code'.
 */
#define QM_RETURN_ON_FALSE(a, err_code, log_tag, format, ...) do {                             \
        if (unlikely(!(a))) {                                                                   \
            QM_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);        \
            return err_code;                                                                    \
        }                                                                                       \
    } while(0)

/**
 * Macro which can be used to check the condition. If the condition is not 'true', it prints the message,
 * sets the local variable 'ret' to the supplied 'err_code', and then exits by jumping to 'goto_tag'.
 */
#define QM_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...) do {                     \
        if (unlikely(!(a))) {                                                                   \
            QM_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);        \
            ret = err_code;                                                                     \
            goto goto_tag;                                                                      \
        }                                                                                       \
    } while (0)


#endif

#else

#define QM_RETURN_ON_ERROR(x, log_tag, format, ...)
#define QM_GOTO_ON_ERROR(x, goto_tag, log_tag, format, ...)
#define QM_RETURN_ON_FALSE(a, err_code, log_tag, format, ...)
#define QM_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...)

#endif

#endif /* QM_CHECK_H */

