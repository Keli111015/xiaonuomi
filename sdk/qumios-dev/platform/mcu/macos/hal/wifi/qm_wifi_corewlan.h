#pragma once

#include "qm_wifi_dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int qm_macos_wifi_get_ap_info(const char *interface_name,
                              qm_wifi_ap_record_t *info);
int qm_macos_wifi_authorize_location(uint32_t timeout_ms);
void qm_macos_wifi_set_reuse_current_network(qm_bool enabled);

#ifdef __cplusplus
}
#endif
