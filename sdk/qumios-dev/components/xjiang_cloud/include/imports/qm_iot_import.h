#ifndef __MQTT_PLATFORM_H__
#define __MQTT_PLATFORM_H__

#include "qm_types.h"
#include "qm_kernel.h"

#ifndef NULL
#ifdef  __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif


#ifndef bool
typedef unsigned char bool;
#endif


/** @defgroup group_platform platform
 *  @{
 */

#define _IN_                /* indicate that this is an input parameter */
#define _OU_                /* indicate that this is an output parameter */
#define _INOUT_         /**< indicate that this is a io parameter. */
#define _IN_OPT_        /**< indicate that this is a optional input parameter. */
#define _OUT_OPT_       /**< indicate that this is a optional output parameter. */
#define _INOUT_OPT_     /**< indicate that this is a optional io parameter. */

#define IOT_TRUE    (1)     /* indicate boolean value true */
#define IOT_FALSE   (0)     /* indicate boolean value false */

#define PID_STRLEN_MAX      (64)
#define MID_STRLEN_MAX      (64)


/*********************************** mutex interface ***********************************/

/** @defgroup group_platform_mutex mutex
 *  @{
 */

/**
 * @brief Create a mutex.
 *
 * @retval NULL : Initialize mutex failed.
 * @retval NOT_NULL : The mutex handle.
 * @see None.
 * @note None.
 */
void *hal_mutex_create(void);

/**
 * @brief Destroy the specified mutex object, it will release related resource.
 *
 * @param [in] mutex @n The specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_mutex_destory(_IN_ void *mutex);

/**
 * @brief Waits until the specified mutex is in the signaled state.
 *
 * @param [in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_mutex_lock(_IN_ void *mutex);

/**
 * @brief Deallocate memory block
 *
 * @param[in] ptr @n Pointer to a memory block previously allocated with platform_malloc.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_free(void *ptr);

/**
 * @brief Allocates a block of size bytes of memory, returning a pointer to the beginning of the block.
 *
 * @param [in] size @n specify block size in bytes.
 * @return A pointer to the beginning of the block.
 * @see None.
 * @note Block value is indeterminate.
 */
void *hal_malloc(int size);

/**
 * @brief Releases ownership of the specified mutex object..
 *
 * @param [in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_mutex_unlock(_IN_ void *mutex);


/** @} */ /* end of platform_mutex */


/** @defgroup group_platform_other other
 *  @{
 */

/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was boot.
 *
 * @return the number of milliseconds.
 * @see None.
 * @note None.
 */
uint32_t hal_uptime_ms(void);


/**
 * @brief Sleep thread itself.
 *
 * @param [in] ms @n the time interval for which execution is to be suspended, in milliseconds.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_sleep_ms(_IN_ uint32_t ms);

/**
 * @brief Set seed for a sequence of pseudo-random integers, which will be returned by hal_random_get()
 *
 * @param [in] seed @n A start point for the random number sequence
 * @return None.
 * @see None.
 * @note None.
 */
void hal_srandom(_IN_ uint32_t seed);

/**
 * @brief Get a random integer
 *
 * @param [in] region @n Range of generated random numbers
 * @return Random number
 * @see None.
 * @note None.
 */
uint32_t hal_random_get(_IN_ uint32_t region);

/**
 * @brief Writes formatted data to string.
 *
 * @param [out] str: @n String that holds written text.
 * @param [in] len: @n Maximum length of character will be written
 * @param [in] fmt: @n Format that contains the text to be written, it can optionally contain embedded format specifiers
     that specifies how subsequent arguments are converted for output.
 * @param [in] ...: @n the variable argument list, for formatted and inserted in the resulting string replacing their respective specifiers.
 * @return bytes of character successfully written into string.
 * @see None.
 * @note None.
 */
//int hal_snprintf(_OU_ char *str, _IN_ const int len, _IN_ const char *fmt, ...);
#define hal_snprintf    qm_snprintf

/**
 * Add a new KV pair.
 *
 * @param[in]  key    the key of the KV pair.
 * @param[in]  value  the value of the KV pair.
 * @param[in]  len    the length of the value.
 * @param[in]  sync   save the KV pair to flash right now (should always be 1).
 *
 * @return  0 on success, negative error on failure.
 */
int hal_kv_set(const char *key, const void *value, int len, int sync);

/**
 * Get the KV pair's value stored in buffer by its key.
 *
 * @note: the buffer_len should be larger than the real length of the value,
 *        otherwise buffer would be NULL.
 *
 * @param[in]      key         the key of the KV pair to get.
 * @param[out]     buffer      the memory to store the value.
 * @param[in-out]  buffer_len  in: the length of the input buffer.
 *                             out: the real length of the value.
 *
 * @return  0 on success, negative error on failure.
 */
int hal_kv_get(const char *key, void *buffer, int *buffer_len);

/**
 * Delete the KV pair by its key.
 *
 * @param[in]  key  the key of the KV pair to delete.
 *
 * @return  0 on success, negative error on failure.
 */
int hal_kv_del(const char *key);



/** @defgroup group_platform_network network
 *  @{
 */

/**
 * @brief Establish a TCP connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the TCP server
 * @param [in] port: @n Specify the TCP port of TCP server
 *
 * @return The handle of TCP connection.
   @retval   0 : Fail.
   @retval > 0 : Success, the value is handle of this TCP connection.
 */
void *hal_tcp_establish(_IN_ const char *host, _IN_  uint16_t port);


/**
 * @brief Destroy the specific TCP connection.
 *
 * @param [in] handle: @n Specify the TCP connection by handle.
 *
 * @return The result of destroy TCP connection.
 * @retval < 0 : Fail.
 * @retval   0 : Success.
 */
int32_t hal_tcp_destroy(_IN_ void *handle);


/**
 * @brief Write data into the specific TCP connection.
 *        The API will return immediately if 'len' be written into the specific TCP connection.
 *
 * @param [in] handle @n A descriptor identifying a connection.
 * @param [in] buf @n A pointer to a buffer containing the data to be transmitted.
 * @param [in] len @n The length, in bytes, of the data pointed to by the 'buf' parameter.
 * @param [in] timeout_ms @n Specify the timeout value in millisecond. In other words, the API block 'timeout_ms' millisecond maximumly.
 *
 * @retval      < 0 : TCP connection error occur..
 * @retval        0 : No any data be write into the TCP connection in 'timeout_ms' timeout period.
 * @retval (0, len] : The total number of bytes be written in 'timeout_ms' timeout period.

 * @see None.
 */
int32_t hal_tcp_write(_IN_ void *handle, _IN_ const char *buf, _IN_ uint32_t len, _IN_ uint32_t timeout_ms);


/**
 * @brief Read data from the specific TCP connection with timeout parameter.
 *        The API will return immediately if 'len' be received from the specific TCP connection.
 *
 * @param [in] handle @n A descriptor identifying a TCP connection.
 * @param [out] buf @n A pointer to a buffer to receive incoming data.
 * @param [out] len @n The length, in bytes, of the data pointed to by the 'buf' parameter.
 * @param [in] timeout_ms @n Specify the timeout value in millisecond. In other words, the API block 'timeout_ms' millisecond maximumly.
 *
 * @retval       -2 : TCP connection error occur.
 * @retval       -1 : TCP connection be closed by remote server.
 * @retval        0 : No any data be received in 'timeout_ms' timeout period.
 * @retval (0, len] : The total number of bytes be received in 'timeout_ms' timeout period.

 * @see None.
 */
int32_t hal_tcp_read(_IN_ void *handle, _OU_ char *buf,
                     _OU_ uint32_t len, _IN_ uint32_t timeout_ms);


/**
 * @brief Establish a SSL connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the SSL server
 * @param [in] port: @n Specify the SSL port of SSL server
 * @param [in] ca_crt @n Specify the root certificate which is PEM format.
 * @param [in] ca_crt_len @n Length of root certificate, in bytes.
 * @return SSL handle.
 * @see None.
 * @note None.
 */
void *hal_ssl_establish(
            _IN_ const char *host,
            _IN_ uint16_t port,
            _IN_ const char *ca_crt,
            _IN_ size_t ca_crt_len);


/**
 * @brief Destroy the specific SSL connection.
 *
 * @param[in] handle: @n Handle of the specific connection.
 *
 * @return The result of destroy ssl
 *
 * @retval < 0 : Fail.
 * @retval   0 : Success.
 */
int32_t hal_ssl_destroy(_IN_ void *handle);


/**
 * @brief Write data into the specific SSL connection.
 *        The API will return immediately if 'len' be written into the specific SSL connection.
 *
 * @param [in] handle @n A descriptor identifying a connection.
 * @param [in] buf @n A pointer to a buffer containing the data to be transmitted.
 * @param [in] len @n The length, in bytes, of the data pointed to by the 'buf' parameter.
 * @param [in] timeout_ms @n Specify the timeout value in millisecond. In other words, the API block 'timeout_ms' millisecond maximumly.
 * @retval      < 0 : SSL connection error occur..
 * @retval        0 : No any data be write into the SSL connection in 'timeout_ms' timeout period.
 * @retval (0, len] : The total number of bytes be written in 'timeout_ms' timeout period.
 * @see None.
 */
int32_t hal_ssl_write(_IN_ void *handle, _IN_ const char *buf, _IN_ int len, _IN_ uint32_t timeout_ms);


/**
 * @brief Read data from the specific SSL connection with timeout parameter.
 *        The API will return immediately if 'len' be received from the specific SSL connection.
 *
 * @param [in] handle @n A descriptor identifying a SSL connection.
 * @param [out] buf @n A pointer to a buffer to receive incoming data.
 * @param [out] len @n The length, in bytes, of the data pointed to by the 'buf' parameter.
 * @param [in] timeout_ms @n Specify the timeout value in millisecond. In other words, the API block 'timeout_ms' millisecond maximumly.
 *
 * @retval       -2 : SSL connection error occur.
 * @retval       -1 : SSL connection be closed by remote server.
 * @retval        0 : No any data be received in 'timeout_ms' timeout period.
 * @retval (0, len] : The total number of bytes be received in 'timeout_ms' timeout period.
 * @see None.
 */
int32_t hal_ssl_read(_IN_ void *handle, _OU_ char *buf, _OU_ int len, _IN_ uint32_t timeout_ms);



/**
 * @brief this is a network address structure, including host(ip or host name) and port.
 */

typedef struct
{
    char host[16];
    uint16_t port; /**< udp port or tcp port in host byte order*/
}platform_netaddr_t, *pplatform_netaddr_t;

/**
 * @brief Create a udp server with the specified port.
 *
 * @param[in] port @n The specified udp sever listen port.
 * @return Server handle.
 @verbatim
 =  NULL: fail.
 != NULL: success.
 @endverbatim
 * @see None.
 * @note It is recommended to add handle value by 1, if 0(NULL) is a valid handle value in your platform.
 */
void *hal_udp_server_create(_IN_ uint16_t port);



/**
 * @brief Establish a UDP connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the UDP server
 * @param [in] port: @n Specify the UDP port of UDP server
 *
 * @retval  < 0 : Fail.
 * @retval >= 0 : Success, the value is handle of this UDP connection.
 * @see None.
 */
void *hal_udp_client_create(_IN_ char *host, _IN_ unsigned short port);

/**
 * @brief Closes an existing udp connection.
 *
 * @param[in] handle @n the specified connection.
 * @return None.
 * @see None.
 * @note None.
 */
void hal_udp_close(_IN_ void *handle );

/**
 * @brief Sends data to a specific destination.
 *
 * @param[in] handle @n A descriptor identifying a connection.
 * @param[in] buffer @n A pointer to a buffer containing the data to be transmitted.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @param[in] netaddr @n A pointer to a netaddr structure that contains the address of the target.
 *
 * @return
 @verbatim
 > 0: the total number of bytes sent, which can be less than the number indicated by length.
 = -1: error occur.
 @endverbatim
 * @see None.
 * @note blocking API.
 */
int hal_udp_write(
    _IN_ void *handle, _IN_ const char *buffer,
    _IN_ uint32_t length,_IN_ pplatform_netaddr_t netaddr );

/**
 * @brief Receives data from a udp connection.
 *
 * @param[in] handle @n A descriptor identifying a connection.
 * @param[out] buffer @n A pointer to a buffer to receive incoming data.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @param[out] netaddr @n A pointer to a netaddr structure that contains the address of the source.
 * @return
 @verbatim
 >  0: The total number of bytes received, which can be less than the number indicated by length.
 <  0: Error occur.
 @endverbatim
 *
 * @see None.
 * @note blocking API.
 */
int hal_udp_read(_IN_ void *handle,_OU_ char *buffer,
                 _IN_ uint32_t length,_IN_ pplatform_netaddr_t netaddr);

/**
 * @brief Receives data from a udp connection.
 *
 * @param[in] handle @n A descriptor identifying a connection.
 * @param[out] buffer @n A pointer to a buffer to receive incoming data.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @param[out] netaddr @n A pointer to a netaddr structure that contains the address of the source.
 * @param [in] timeout_ms @n Specify the timeout value in millisecond. In other words, the API block 'timeout_ms' millisecond maximumly.
 * @return
 @verbatim
 >  0: The total number of bytes received, which can be less than the number indicated by length.
 <  0: Error occur.
 @endverbatim
 *
 * @see None.
 * @note blocking API.
 */
int hal_udp_readtimeout(
    _IN_ void *handle,
     _OU_ char *buffer,
     _IN_ uint32_t length,
     _IN_ unsigned int timeout_ms,
     _OU_ platform_netaddr_t *netaddr);

/** @} */ /* end of platform_network */

/**
 * @brief initialize a firmware upgrade.
 *
 * @param None
 * @return None.
 * @see None.
 * @note None.
 */
void hal_firmware_upgrade_start(void);
/**
 * @brief save firmware upgrade data to flash.
 *
 * @param[in] buffer: @n A pointer to a buffer to save data.
 * @param[in] length: @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @return 0, Save success; -1, Save failure.
 * @see None.
 * @note None.
 */

int hal_firmware_upgrade_write( _IN_ char *buffer, _IN_ uint32_t length);

/**
 * @brief indicate firmware upgrade data complete, and trigger data integrity checking,
 and then reboot the system.
 *
 * @param [in] finish: @n Specify the ota whether is finished
 * @return 0: Success; -1: Failure.
 * @see None.
 * @note None.
 */
int hal_firmware_upgrade_finish(int finish);


#endif  /* __IOT_IMPORT_H__ */
