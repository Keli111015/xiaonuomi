#ifndef QM_HTTP_CLIENT_TRANSPORT_H
#define QM_HTTP_CLIENT_TRANSPORT_H

#ifndef QM_HTTP_CLIENT_INTERNAL
#error "qm_http_client_transport.h is internal to qm_http_client; include qm_http_client.h instead"
#endif

#include "qm_http_client.h"

/**
 * @brief Internal HTTP transport vtable.
 *
 * This structure adapts qumi TCP/TLS HAL interfaces to the transport contract
 * required by `qm_http_client`. It is intentionally internal and should not be
 * used directly by application code.
 */
typedef struct {
    /**
     * @brief Establish a transport connection.
     *
     * @param[in] host Target host name or IP address
     * @param[in] port Target port
     * @param[in] config Transport-specific configuration pointer
     * @return Connection handle on success, or NULL on failure
     */
    void *(*connect)(const char *host, uint16_t port, void *config);

    /**
     * @brief Write bytes to the transport.
     *
     * @param[in] handle Transport connection handle
     * @param[in] buf Data buffer to send
     * @param[in] len Number of bytes to send
     * @param[in] timeout_ms Write timeout in milliseconds
     * @return Positive number of bytes written on success
     * @return 0 on timeout when the lower layer reports it that way
     * @return Negative error code on failure
     */
    int (*write)(void *handle, const char *buf, int len, uint32_t timeout_ms);

    /**
     * @brief Read bytes from the transport.
     *
     * @param[in] handle Transport connection handle
     * @param[out] buf Receive buffer
     * @param[in] len Maximum number of bytes to read
     * @param[in] timeout_ms Read timeout in milliseconds
     * @return Positive number of bytes read on success
     * @return 0 on timeout when the lower layer reports it that way
     * @return Negative error code on failure
     */
    int (*read)(void *handle, char *buf, int len, uint32_t timeout_ms);

    /**
     * @brief Close the transport connection.
     *
     * @param[in] handle Transport connection handle
     * @return QM_EOK on success
     * @return Negative error code on failure
     */
    int (*close)(void *handle);

    /**
     * @brief Transport-specific configuration pointer.
     *
     * For TLS transport this points to `qm_http_tls_config_t`. TCP transport
     * leaves it as NULL.
     */
    void *config;
} qm_http_client_transport_t;

/**
 * @brief Initialize the internal transport adapter for plain TCP.
 *
 * This helper fills the internal transport vtable with wrappers around the
 * qumi TCP HAL so the HTTP client can use a unified read/write/connect/close
 * contract regardless of the lower transport type.
 *
 * @param[out] transport Transport object to initialize
 */
void qm_http_client_transport_tcp_init(qm_http_client_transport_t *transport);

/**
 * @brief Initialize the internal transport adapter for TLS.
 *
 * The TLS configuration pointer is stored as-is and later translated to the
 * lower-layer `qm_tls_cfg_t` structure at connect time. The initialized
 * adapter still follows the same internal transport contract as the TCP
 * variant, but all I/O is forwarded through the qumi TLS HAL.
 *
 * @param[out] transport Transport object to initialize
 * @param[in] tls Optional TLS configuration, or NULL
 */
void qm_http_client_transport_tls_init(qm_http_client_transport_t *transport, const qm_http_tls_config_t *tls);

#endif
