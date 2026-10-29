#include "qm_hal_kv.h"
#include "qm.h"
#include "qm_utils_string.h"
#include "native_gecko.h"

#include "qm_log.h"
#define MIBLE_USER_REC_ID_BASE   0x50

int qm_hal_kv_init(void)
{

    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    uint16_t record_id = 0; 
    struct gecko_msg_flash_ps_load_rsp_t *p_rsp = NULL;
    
    record_id = int_str_to_num(key, strlen(key));
    p_rsp = gecko_cmd_flash_ps_load(MIBLE_USER_REC_ID_BASE + record_id + 0x4000);
    
    *buffer_len = p_rsp->value.len;
    if(buffer){
    	memcpy(buffer, p_rsp->value.data, p_rsp->value.len);
    }
    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    uint16_t record_id = 0; 
    struct gecko_msg_flash_ps_save_rsp_t *p_rsp;

    record_id = int_str_to_num(key, strlen(key));
    p_rsp = gecko_cmd_flash_ps_save(MIBLE_USER_REC_ID_BASE + record_id + 0x4000, len, value);

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    uint16_t record_id = 0; 
    struct gecko_msg_flash_ps_erase_rsp_t *p_rsp;

    record_id = int_str_to_num(key, strlen(key));
    p_rsp = gecko_cmd_flash_ps_erase(MIBLE_USER_REC_ID_BASE + record_id + 0x4000);

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
