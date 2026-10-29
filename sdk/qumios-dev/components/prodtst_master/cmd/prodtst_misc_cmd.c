#include "prodtst_cmd.h"
#include "qm_at.h"
#include "qm_errno.h"
#include "qm_log.h"

#define LOG_TAG "special cmd"


prodtst_cmd_t prodtst_misc_cmd[] = {
    {"notify", "AT+NOTIFY", PRODTST_NFC_MAX_LEN, CONFIG_QM_PRODTST_CMD_ACK_DEFAULT_TIMEOUT, NULL, NULL },
};

#define PRODTST_MISC_CMD_NUM   (sizeof(prodtst_misc_cmd)/sizeof(prodtst_misc_cmd[0]))

prodtst_cmd_t *prodtst_misc_cmd_get(char *key)
{
    int i = 0;
    if(key == NULL){
        return NULL;
    }

    for(i = 0; i < PRODTST_MISC_CMD_NUM; i++){
        if((strlen(key) == strlen(prodtst_misc_cmd[i].name)) && 
        (0 == strcmp(key, prodtst_misc_cmd[i].name))){
            return &prodtst_misc_cmd[i];
        }
    }
    return NULL;
}