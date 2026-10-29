#ifndef __SERIAL_PLATFORM_H__
#define __SERIAL_PLATFORM_H__

#include "qm.h"
#include "serial_common.h"
#include "serial_protocol.h"

#if CONFIG_QM_SERIAL_PRINTF_SUPPORT
#include "qm_log.h"
#endif

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
/*********************************** mutex interface ***********************************/

/** @defgroup group_platform_mutex platform_mutex
 *  @{
 */

/**
 * @brief Create a mutex.
 * @return Mutex handle.
 * @see None.
 * @note None.
 */
void *qm_serial_mutex_init(void);

/**
 * @brief Destroy the specified mutex object, it will free related resource.
 *
 * @param[in] mutex @n The specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
int qm_serial_mutex_deinit(void *mutex);

/**
 * @brief Waits until the specified mutex is in the signaled state.
 *
 * @param[in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
int qm_serial_mutex_lock(void *mutex);

/**
 * @brief Releases ownership of the specified mutex object..
 *
 * @param[in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
int qm_serial_mutex_unlock(void *mutex);

/** @} */


/* Semaphore functions: */
/**
 * @brief Create a new semaphore
 * @param count initial count of the semaphore
 * @return sem handle
 *  */
void *qm_serial_sem_init(uint8_t count);
/**
 * @brief Signals a semaphore
 * @param sem the semaphore to signal */
int qm_serial_sem_signal(void *sem);
/**
 * @brief Wait for a semaphore for the specified timeout
 * @param sem the semaphore to wait for
 * @param timeout timeout in milliseconds to wait (0 = wait forever)
 * @return time (in milliseconds) waited for the semaphore
 *         or mbus_ARCH_TIMEOUT on timeout */
int qm_serial_sem_wait(void *sem, uint32_t timeout);
/**
 * @brief Delete a semaphore
 * @param sem semaphore to delete */
int qm_serial_sem_deinit(void *sem);

#endif

/**
 * @brief Initialize uart
 * @return handle of uart.
 * @see None.
 * @note None.
 */
void *qm_serial_uart_init(void);

/**
 * @brief Increases the count of the specified semaphore object by 1.
 *
 * @param[in] A pointer to a buffer containing the data to be transmitted.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @return None.
 * @see None.
 * @note None.
 */
int qm_serial_send(void *handle, uint8_t* buffer, uint32_t length);

/**
 * @brief Increases the count of the specified semaphore object by 1.
 *
 * @param[in] buffer @n A pointer to a buffer to receive incoming data.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @param[in] timeout_ms @n timeout interval in millisecond.
 * @return None.
 * @see None.
 * @note None.
 */
int qm_serial_recv(void *handle, uint8_t* buffer, uint32_t length, uint32_t timeout_ms);

/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was boot.
 *
 * @param[in] time_info
 * @return 0:success, <0:failed
 * @see None.
 * @note None.
 */
int qm_serial_get_time(serial_time_info_t *time_info);

/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was boot.
 *
 * @param None.
 * @return the number of milliseconds.
 * @see None.
 * @note None.
 */
uint32_t qm_serial_get_timems( void );

/**
 * @brief sleep thread itself.
 *
 * @param[in] ms @n the time interval for which execution is to be suspended, in milliseconds.
 * @return None.
 * @see None.
 * @note None.
 */
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
void qm_serial_msleep(uint32_t ms);
#endif

/**
 * @brief Allocates a block of size bytes of memory, returning a pointer to the beginning of the block.
 *
 * @param[in] size @n specify block size in bytes.
 * @return A pointer to the beginning of the block.
 * @see None.
 * @note Block value is indeterminate.
 */
void *qm_serial_malloc(uint32_t size);

/**
 * @brief Deallocate memory block
 *
 * @param[in] ptr @n Pointer to a memory block previously allocated with platform_malloc.
 * @return None.
 * @see None.
 * @note None.
 */
void qm_serial_free(void *ptr);

#if CONFIG_QM_SERIAL_PRINTF_SUPPORT
#define qm_serial_printf      QM_LOGD
#define qm_serial_hex_printf  QM_HEX_LOGD
#endif


#endif
