
#ifndef _DNS_SERVER_H_
#define _DNS_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"

#if CONFIG_DNS_SERVER_SUPPORT

int dns_server_ip_register(const char *domain, const char *ip);

int dns_server_start(void);

int dns_server_stop(void);

#endif


#ifdef __cplusplus
}
#endif

#endif /* DNS_SERVER_H */
