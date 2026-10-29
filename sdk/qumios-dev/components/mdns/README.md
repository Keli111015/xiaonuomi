# mDNS Component

`mdns` provides synchronous DNS-SD service discovery through the QUMI UDP HAL.
It sends a PTR query with the mDNS unicast-response bit to
`224.0.0.251:5353`, aggregates PTR/SRV/TXT/A records across one or more
response packets, resolves each SRV target to an IPv4
address (falling back to the target host name), and reports every discovered
service instance through a callback that runs synchronously, once per instance.

The component owns packet encoding/parsing only. It does not own a task,
cache, retry policy, or multicast interface selection. The target UDP HAL must
support sending IPv4 multicast and receiving unicast replies on an ephemeral
port; targets without those capabilities should leave `MDNS_SUPPORT` disabled.

One query retains at most four service instances, four IPv4 A records, three
response packets, and 192 bytes of TXT RDATA per service. The current QUMI
network address contract is IPv4-only, so AAAA records are not resolved.
When a service advertises more TXT data than this limit, `txt_truncated` is
set and the retained TXT data must not be treated as complete.

```text
mdns_query -> qm_udp HAL -> mDNS responder
      |                         |
      +------ mdns_service_t ---+
```

## Usage

```c
#include "mdns.h"
#include "qm_errno.h"

static int on_service(const mdns_service_t *service, void *user)
{
    (void)user;
    /* Copy values here if they are needed after the callback returns. */
    return service->port != 0U ? QM_EOK : -QM_EINVAL;
}

int discover_printers(void)
{
    return mdns_query("_ipp._tcp.local", 1000U, on_service, NULL);
}
```

The callback is synchronous and runs once per discovered instance; service
strings are borrowed and must be copied before returning. Returning any non-zero
value stops discovery and is propagated unchanged to the caller. `mdns_query()`
returns `QM_EOK` when at least one instance was found and the callback returned
`QM_EOK`. It propagates a callback result or HAL error when no valid response
was accepted (for example `-QM_ETIMEOUT` when the first packet never arrives),
and returns `-QM_ENOENT` when valid packets contain no complete service. Task
scheduling, retries, caching, and socket or interface setup remain
application/HAL responsibilities.
