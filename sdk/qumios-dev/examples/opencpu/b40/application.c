#include "prodtst_client.h"
#include "prodtst_client_common.h"

#include "id_map.h"
#include "iot_generic.h"

#include "miio_user_api.h"

#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"

#include "ws2812.h"

driver_ws2812_t config = {
		.
};

void qm_application_start(void)
{   
	ws2812_init();
}

