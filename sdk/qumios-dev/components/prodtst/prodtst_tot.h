#ifndef PRODTST_TOT_H
#define PRODTST_TOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_common.h"
#include "prodtst_path.h"
#include "qm_types.h"

typedef int (*prodtst_tot_timeout_fn)(char uuid[CONFIG_PRODTST_UUID_LEN + 1]);

int prodtst_tot_init(prodtst_tot_timeout_fn timeout_fn);
int prodtst_tot_record_add(char uuid[CONFIG_PRODTST_UUID_LEN + 1], uint32_t timeout);
int prodtst_tot_record_remove(char uuid[CONFIG_PRODTST_UUID_LEN + 1]);
int prodtst_tot_record_is_exists(char uuid[CONFIG_PRODTST_UUID_LEN + 1], int *is_exists);
int prodtst_tot_reset(void);
int prodtst_tot_deinit(void);


#ifdef __cplusplus
}
#endif


#endif /* PRODTST_TOT_H */