/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _QM_WEBSOCKET_TRANSPORT_H_
#define _QM_WEBSOCKET_TRANSPORT_H_

#ifndef QM_WEBSOCKET_CLIENT_INTERNAL
#error "qm_websocket_transport.h is internal to qm_websocket_client; include qm_websocket_client.h instead"
#endif

#include "qm_websocket_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the internal TCP transport adapter used for `ws://` connections.
 *
 * The returned object wraps the qumi TCP HAL behind the internal WebSocket
 * transport contract. Ownership stays with the caller, which must later release
 * the transport object with qm_websocket_transport_destroy().
 *
 * @return Transport object on success, or NULL on allocation failure
 */
qm_websocket_transport_t *qm_websocket_transport_tcp_create(void);

/**
 * @brief Create the internal TLS transport adapter used for `wss://` connections.
 *
 * The public TLS configuration is translated into the lower-layer qumi TLS
 * configuration stored by the transport object. Certificate and key buffers are
 * still referenced directly, so they must remain valid while the transport may
 * be used.
 *
 * @param[in] tls_config Optional TLS configuration, or NULL for defaults
 * @return Transport object on success, or NULL on allocation/configuration error
 */
qm_websocket_transport_t *qm_websocket_transport_tls_create(const qm_websocket_tls_config_t *tls_config);

/**
 * @brief Destroy a transport object created by this internal module.
 *
 * This releases the transport object itself. Active connection handles returned
 * by transport->connect must be closed first through transport->close().
 *
 * @param[in] transport Transport object to destroy, or NULL
 */
void qm_websocket_transport_destroy(qm_websocket_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* _QM_WEBSOCKET_TRANSPORT_H_ */
