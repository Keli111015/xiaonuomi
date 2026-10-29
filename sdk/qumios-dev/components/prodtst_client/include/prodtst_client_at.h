#ifndef _PRODTST_CLIENT_AT_H_
#define _PRODTST_CLIENT_AT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"
#include "prodtst_client_common.h"

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT

#include "qm_at.h"

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT

qm_at_result_t mi_pid_model_query(void *channel);
qm_at_result_t mi_pid_model_setup(void *channel, const char *args);

#endif


#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT

qm_at_result_t baidu_cert_combo_query(void *channel);
qm_at_result_t baidu_cert_combo_set_up(void *channel, const char *args);

#endif

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT

#if CONFIG_PRODTST_CLIENT_CHIP_COMBO
qm_at_result_t ali_cert_combo_query(void *channel);
qm_at_result_t baidu_cert_combo_set_up(void *channel, const char *args);
#endif

#endif

#if CONFIG_PRODTST_CLIENT_AT_BLE 
qm_at_result_t qm_prodtst_client_macble_query(void *channel);
#if CONFIG_PRODTST_CLIENT_AT_BLE_ADV
qm_at_result_t qm_prodtst_client_ble_adv_set_up(void *channel, const char *args);
#endif
#endif

#if CONFIG_PRODTST_CLIENT_AT_WIFI
qm_at_result_t qm_prodtst_client_macsta_query(void *channel);
#endif

#if CONFIG_PRODTST_CLIENT_AT_WIFI_RF
qm_at_result_t qm_prodtst_client_rf_wifi_setup(void *channel, const char *args);
#endif

qm_at_result_t qm_prodtst_client_fwver_query(void *channel);
qm_at_result_t qm_prodtst_client_fwname_query(void *channel);
qm_at_result_t qm_prodtst_client_fwname_verify(void *channel, const char *args);

#endif

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_CLIENT_H */
