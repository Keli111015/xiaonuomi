#include "qm_types.h"
#include "qm_iot_import.h"
#include "qm_tcp.h"
#include "iot_cloud_debug.h"

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
void *hal_tcp_establish(const char *host, uint16_t port)
{
    return qm_tcp_establish(host, port);
}

/**
 * @brief Destroy the specific TCP connection.
 *
 * @param [in] handle: @n Specify the TCP connection by handle.
 *
 * @return The result of destroy TCP connection.
 * @retval < 0 : Fail.
 * @retval   0 : Success.
 */
int32_t hal_tcp_destroy(_IN_ void *handle)
{
    return qm_tcp_destory(handle);
}


int32_t hal_tcp_write(void *handle, const char *buf, uint32_t len, uint32_t timeout_ms)
{
    return qm_tcp_write(handle, buf, len, timeout_ms);
}


int32_t hal_tcp_read(void *handle, char *buf, uint32_t len, uint32_t timeout_ms)
{
    return qm_tcp_read(handle, buf, len, timeout_ms);
}


