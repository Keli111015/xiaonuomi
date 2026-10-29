
#ifndef _QM_TCP_H_
#define _QM_TCP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_network.h"

/** @defgroup group_platform_network network
 *  @{
 */

/**
 * @brief Establish a TCP server.
 *
 * @param [in] port: @n Specify the TCP port of TCP server
 *
 * @return The handle of TCP connection.
   @retval   0 : Fail.
   @retval > 0 : Success, the value is handle of this TCP connection.
 */
int qm_tcp_server_creat(uint16_t port);

/**
 * @brief accept a TCP connection.
 *
 * @param [in] fd: @n Specify the TCP connection by handle.
 * @param [in] ip: @n the address info of client
 * @param [in] ip: @n timeout
 *
 * @return The handle of TCP connection.
   @retval   0 : Fail.
   @retval > 0 : Success, the value is handle of this TCP connection.
 */
int qm_tcp_accept(int socket, char ip[QM_NETWORK_ADDR_LEN], int timeout);

/**
 * @brief Establish a TCP connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the TCP server
 * @param [in] port: @n Specify the TCP port of TCP server
 *
 * @return The handle of TCP connection.
   @retval  =  NULL : Fail.
   @retval != NULL : Success, the value is handle of this TCP connection.
 */
int qm_tcp_establish(const char *host, uint16_t port);

/**
 * @brief Destroy the specific TCP connection.
 *
 * @param [in] fd: @n Specify the TCP connection by handle.
 *
 * @return The result of destroy TCP connection.
 * @retval < 0 : Fail.
 * @retval   0 : Success.
 */
int qm_tcp_destroy(int socket);


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
int qm_tcp_write(int socket, const char *buf, uint32_t len, uint32_t timeout_ms);


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
int qm_tcp_read(int socket, char *buf, uint32_t len, uint32_t timeout_ms);



#ifdef __cplusplus
}
#endif
#endif 