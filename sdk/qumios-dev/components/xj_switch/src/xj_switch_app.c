
#include "xj_switch_event.h"
#include "xj_switch_app.h"
#include "qm_config.h"
#include "qm_types.h"
#include "crazy_button.h"
#include "xj_switch.h"
#include "qm_kv.h"

#include <sys/printk.h>

void xj_switch_app_init(void)
{
    
    if(!xj_switch_is_inited())
    {
        return;
    }

    //事件初始化
    xj_switch_event_init();
}
