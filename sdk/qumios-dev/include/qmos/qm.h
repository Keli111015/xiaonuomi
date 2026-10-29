#ifndef _QM_H
#define _QM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
//#include "qm_cli.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_types.h"
#include "qm_check.h"

/**
 * 
 * Entry for qm start 
 */
void qm_main(void);

/**
 * Entry for qm application start 
 */
void qm_application_start(void);

/**
 * init for qm kernel 
 */
void qm_kernel_init(void);

/**
 * Transmit data on a UART interface
 *
 * @param  data     pointer to the start of data
 * @param  size     number of bytes to transmit
 * @param  timeout  time to wait
 *
 * @return  0 success, EIO if an error occurred with any step
 */
int32_t qm_dbg_write(void *data, uint32_t size, uint32_t timeout);


/**
 * Recv data on a UART interface
 *
 * @param  data     pointer to the start of recv buf
 * @param  expect_size     number of bytes expect to recv
 * @param  timeout  time to wait
 *
 * @return  0 success, EIO if an error occurred with any step
 */
int32_t qm_dbg_read(void *data, uint32_t expect_size, uint32_t timeout);

#if CONFIG_QM_NOOS_SUPPORT

int qm_os_yield(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* QM_QM_H */

