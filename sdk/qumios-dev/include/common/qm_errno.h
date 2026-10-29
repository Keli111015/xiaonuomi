#ifndef QM_ERROR_H
#define QM_ERROR_H

#include "qm_types.h"


#define QM_EOK                          0               /**< There is no error */
#define QM_ERROR                        1               /**< A generic error happens */
#define QM_ETIMEOUT                     2               /**< Timed out */
#define QM_EFULL                        3               /**< The resource is full */
#define QM_EEMPTY                       4               /**< The resource is empty */
#define QM_ENOMEM                       5               /**< No memory */
#define QM_ENOSYS                       6               /**< No system */
#define QM_EBUSY                        7               /**< Busy */
#define QM_EIO                          8               /**< IO error */
#define QM_EINTR                        9               /**< Interrupted system call */
#define QM_EINVAL                       10              /**< Invalid argument */
#define QM_EINIT                        11              /**< init error */
#define QM_ENOENT                        12              /**< No such file or directory */

#define QM_ERR_WIFI_BASE                0x1000          /*!< Starting number of WiFi error codes */


typedef  int32_t  qm_err_t;

#endif /* QM_ERROR_H */
