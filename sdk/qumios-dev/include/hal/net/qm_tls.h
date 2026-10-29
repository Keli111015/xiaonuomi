#ifndef _QM_TLS_H_
#define _QM_TLS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

typedef struct
{
    int ca_crt_len;
    const char *ca_crt;              /*CA certificate*/ 

    int client_crt_len;
    const char *client_crt;          /*Client certificate*/

    int client_key_len;
    const char *client_key;          /*Client certificate's private key*/
} qm_tls_cfg_t;

typedef struct
{
    int server_crt_len;
    const char *server_crt;          /*Server certificate*/

    int server_key_len;
    const char *server_key;          /*Server private key*/

    int client_ca_len;
    const char *client_ca;           /*Client CA certificate for mTLS*/

    qm_bool verify_client;           /*Verify client certificate when client_ca is set*/
    qm_bool session_tickets;         /*Enable TLS session tickets when supported*/
    const char **alpn_protos;        /*Optional NULL-terminated ALPN protocol list*/
} qm_tls_server_cfg_t;

/**
 * @brief Establish a SSL connection.
 *
 * @param [in] host: @n Specify the hostname(IP) of the SSL server
 * @param [in] port: @n Specify the SSL port of SSL server
 * @param [in] cfg @n TLS client configuration.
 * @return SSL handle. =  NULL: fail,!= NULL: success.
 * @see None.
 * @note None.
 */
void *qm_tls_establish(const char *host, uint16_t port, const qm_tls_cfg_t *cfg);

/**
 * @brief Establish a server-side SSL session over an accepted TCP socket.
 *
 * @param [in] socket Accepted TCP socket.
 * @param [in] cfg Server TLS configuration.
 * @param [in] timeout_ms Handshake timeout in milliseconds. Platform
 *                        implementations may ignore this when the underlying
 *                        TLS stack only exposes a blocking server handshake.
 * @return SSL handle. = NULL: fail, != NULL: success.
 */
void *qm_tls_accept(int socket, const qm_tls_server_cfg_t *cfg, uint32_t timeout_ms);

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
int32_t qm_tls_destroy(void *handle);


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
int32_t qm_tls_write(void *handle, const char *buf, int len, uint32_t timeout_ms);


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
int32_t qm_tls_read(void * handle, char *buf, int len, uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif
#endif
