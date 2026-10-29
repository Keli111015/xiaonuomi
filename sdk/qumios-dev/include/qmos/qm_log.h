#ifndef QM_LOG_H
#define QM_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_log_impl.h"
/**
 * @brief Log level
 *
 */
typedef enum {
    QM_LL_NONE,  /* disable log */
    QM_LL_FATAL, /* fatal log will output */
    QM_LL_ERROR, /* fatal + error log will output */
    QM_LL_WARN,  /* fatal + warn + error log will output(default level) */
    QM_LL_INFO,  /* info + warn + error log will output */
    QM_LL_DEBUG, /* debug + info + warn + error + fatal log will output */
} qm_log_level_t;

extern unsigned int qm_log_level;
/**
 * Get the log level.
 */
static inline int qm_get_log_level(void) {
    return qm_log_level;
}

/**
 * Set the log level.
 *
 * @param[in]  log_level  level to be set,must be one of QM_LL_NONE,QM_LL_FATAL,QM_LL_ERROR,QM_LL_WARN,QM_LL_INFO or QM_LL_DEBUG.
 */
void qm_set_log_level(qm_log_level_t log_level);

/*
 * Log at fatal level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  fmt  same as printf() usage.
 */
#define QM_LOGF(mod, fmt, ...) QM_LOGF_IMPL(mod, fmt, ##__VA_ARGS__)

/*
 * Log at error level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  fmt  same as printf() usage.
 */
#define QM_LOGE(mod, fmt, ...) QM_LOGE_IMPL(mod, fmt, ##__VA_ARGS__)

/*
 * Log at warning level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  fmt  same as printf() usage.
 */
#define QM_LOGW(mod, fmt, ...) QM_LOGW_IMPL(mod, fmt, ##__VA_ARGS__)

/*
 * Log at info level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  fmt  same as printf() usage.
 */
#define QM_LOGI(mod, fmt, ...) QM_LOGI_IMPL(mod, fmt, ##__VA_ARGS__)

/*
 * Log at debug level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  fmt  same as printf() usage.
 */
#define QM_LOGD(mod, fmt, ...) QM_LOGD_IMPL(mod, fmt, ##__VA_ARGS__)

/*
 * Log at debug level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  name  string description of data.
 * @param[in]  p_data  pointer of data.
 * @param[in]  len  length of data.
 */
#define QM_HEX_LOGD(mod, name, p_data, len) QM_HEX_LOGD_IMPL(mod, name, p_data, len)
/*
 * Log at info level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  name  string description of data.
 * @param[in]  p_data  pointer of data.
 * @param[in]  len  length of data.
 */
#define QM_HEX_LOGI(mod, name, p_data, len) QM_HEX_LOGI_IMPL(mod, name, p_data, len)
/*
 * Log at warning level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  name  string description of data.
 * @param[in]  p_data  pointer of data.
 * @param[in]  len  length of data.
 */
#define QM_HEX_LOGW(mod, name, p_data, len) QM_HEX_LOGW_IMPL(mod, name, p_data, len)
/*
 * Log at error level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  name  string description of data.
 * @param[in]  p_data  pointer of data.
 * @param[in]  len  length of data.
 */
#define QM_HEX_LOGE(mod, name, p_data, len) QM_HEX_LOGE_IMPL(mod, name, p_data, len)
/*
 * Log at fatal level.
 *
 * @param[in]  mod  string description of module.
 * @param[in]  name  string description of data.
 * @param[in]  p_data  pointer of data.
 * @param[in]  len  length of data.
 */
#define QM_HEX_LOGF(mod, name, p_data, len) QM_HEX_LOGF_IMPL(mod, name, p_data, len)


/* Exported macro ------------------------------------------------------------*/
#if  CONFIG_QM_ASSERT
/**
  * @brief  The assert_param macro is used for function's parameters check.
  * @param  expr: If expr is false, it calls assert_failed function
  *         which reports the name of the source file and the source
  *         line number of the call that failed.
  *         If expr is true, it returns no value.
  * @retval None
  */
  #define QM_ASSERT(expr)  QM_ASSERT_IMPL(expr)
/* Exported functions ------------------------------------------------------- */
  void qm_assert_failed(uint8_t* file, uint32_t line);
#else
  #define QM_ASSERT(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */


#ifdef __cplusplus
}
#endif

#endif /* QM_LOG_H */


