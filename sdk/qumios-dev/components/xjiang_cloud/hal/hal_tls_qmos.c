#include "qm_types.h"
#include "qm_iot_import.h"
#include "iot_cloud_debug.h"
#include "qm_tls.h"

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
    _IN_ size_t ca_crt_len)
{
    qm_tls_cfg_t tls_cfg = {0};

    tls_cfg.ca_crt = ca_crt;
    tls_cfg.ca_crt_len = (int)ca_crt_len;

    return qm_tls_establish(host, port, &tls_cfg);
}

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
int32_t hal_ssl_destroy(_IN_ void *handle)
{
    return qm_tls_destroy(handle);
}

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
int32_t hal_ssl_write(_IN_ void *handle, _IN_ const char *buf, _IN_ int len, _IN_ uint32_t timeout_ms)
{
    return qm_tls_write(handle, buf, len, timeout_ms);
}

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
int32_t hal_ssl_read(_IN_ void *handle, _OU_ char *buf, _OU_ int len, _IN_ uint32_t timeout_ms)
{
    return qm_tls_read(handle, buf, len, timeout_ms);
}
