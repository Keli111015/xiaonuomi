#ifndef __MAP_HONOR_WIFI_H__
#define __MAP_HONOR_WIFI_H__

#include "qm.h"
#include "qm_log.h"
#include "id_map.h"
#include "qm_errno.h"



char *xj_get_honor_profile(void);

typedef struct product_key {
    char pro_key[40];
    char pro_secret[40];
}product_key_t;

product_key_t *get_pro_key(void);
char *get_product_type_string(void);


#endif // __MAP_HONOR_WIFI_H__