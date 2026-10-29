#include "prodtst_special.h"
#include "qm_errno.h"
#include "qm_types.h"

static prodtst_special_t  g_prodtst_special[] = {

    {"NFC", 256, prodtst_nfc_establish, prodtst_nfc_destroy, prodtst_nfc_send, prodtst_nfc_recv, prodtst_nfc_process},

};

#define PRODTST_SPECIAL_NUM    sizeof(g_prodtst_special)/sizeof(g_prodtst_special[0])


prodtst_special_t *prodtst_special_get(char *action)
{
    int i = 0;
    for(i = 0; i < PRODTST_SPECIAL_NUM; i++){
        if(strlen(action) == strlen(g_prodtst_special[i].action) && 0 == strcmp(action, g_prodtst_special[i].action)){
            return &g_prodtst_special[i];
        }
    }
    
    return NULL;
}



