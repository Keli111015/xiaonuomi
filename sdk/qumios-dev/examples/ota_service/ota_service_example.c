#include "qm.h"
#include "dns_server.h"


#define DNS_DOMAIN "test.com"
#define DNS_IP "192.168.4.1"

void qm_application_start(void)
{
    dns_server_ip_register(DNS_DOMAIN, DNS_IP);

    dns_server_start();
}