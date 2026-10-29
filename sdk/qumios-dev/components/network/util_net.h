

#ifndef _QM_NET_H_
#define _QM_NET_H_

#include "qm_types.h"


/**
 * @brief The structure of network connection(TCP or SSL).
 *   The user has to allocate memory for this structure.
 */

struct util_network;
typedef struct util_network util_network_t, *util_network_pt;

struct util_network {
    const char *pHostAddress;
    uint16_t port;
    
    int ca_crt_len;
    const char *ca_crt;              /*CA certificate*/  /**< NULL, TCP connection; NOT NULL, SSL connection */

    int client_crt_len;
    const char *client_crt;          /*Client certificate*/

    int client_key_len;
    const char *client_key;          /*Client certificate's private key*/

    /**< connection handle: NULL, NOT connection; NOT NULL, handle of the connection */
    void *handle;

    /**< Read data from server function pointer. */
    int (*read)(util_network_pt, char *, uint32_t, uint32_t);

    /**< Send data to server function pointer. */
    int (*write)(util_network_pt, const char *, uint32_t, uint32_t);

    /**< Disconnect the network */
    int (*disconnect)(util_network_pt);

    /**< Establish the network */
    int (*connect)(util_network_pt);
};


int util_net_read(util_network_pt pNetwork, char *buffer, uint32_t len, uint32_t timeout_ms);
int util_net_write(util_network_pt pNetwork, const char *buffer, uint32_t len, uint32_t timeout_ms);
int util_net_disconnect(util_network_pt pNetwork);
int util_net_connect(util_network_pt pNetwork);
int util_net_init(util_network_pt pNetwork, const char *host, uint16_t port, const char *ca_crt);
int util_net_set_client_cert_data(util_network_pt pNetwork, const char *client_crt, int len);
int util_net_set_client_key_data(util_network_pt pNetwork, const char *client_key, int len);

#endif /* IOT_COMMON_NET_H */
