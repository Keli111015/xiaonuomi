#include "qm.h"
#include "qm_log.h"

#include "qm_ble_gap.h"

#include "honor_ble_comm.h"

#include "cJSON.h"

#define LOG_TAG "honor comm"

#define TEMP_BUFF_LEN   (64)

typedef struct 
{
    char str_mac[13];
    uint8_t salt[16];
    int registered;
    uint8_t authcode[16];
    uint8_t authcodeid[16];
    honor_ble_comm_param_t param;
    uint32_t report_id;   
}honor_ble_comm_ctx_t;


int honor_ble_comm_init(honor_ble_comm_param_t *param)
{

    return QM_EOK;
}

static uint32_t get_report_seq(void) {
	return 0;
}

static cJSON *data_item_parse_creat(honor_ble_service_t *service)
{
    cJSON *item = NULL;
    cJSON *datat_item = NULL;
   
    return datat_item;
}

int honor_ble_comm_report(honor_ble_profile_t *profile)
{
    int ret = QM_EOK;

    
    return ret;
}

int honor_ble_comm_adv_start(uint8_t is_near)
{
    return honor_ble_core_adv_start(is_near);
}

int honor_ble_comm_adv_stop(void)
{
    return honor_ble_core_adv_stop();
}

int honor_ble_comm_reset(uint8_t is_near)
{
    return QM_FALSE;
}