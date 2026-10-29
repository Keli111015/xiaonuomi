#ifndef _PPPOS_CLINET_H_
#define _PPPOS_CLINET_H_

#include "qm_config.h"

#if CONFIG_QM_WORK_SUPPORT
#include "qm_event.h"

/** PPPOS client event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_PPPOS);
#endif

#ifndef CONFIG_PPPOS_CLIENT_TASK_SIZE
#define CONFIG_PPPOS_CLIENT_TASK_SIZE    4096
#endif

#ifndef CONFIG_PPPOS_CLIENT_TASK_PRIO
#define CONFIG_PPPOS_CLIENT_TASK_PRIO    20
#endif

typedef enum {
    PPPOS_CLIENT_EVENT_LINK_ON,
    PPPOS_CLIENT_EVENT_LINK_OFF
}pppos_client_event_t;


int pppos_client_init(void);


#endif
