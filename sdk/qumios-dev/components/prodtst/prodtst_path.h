#ifndef PRODTST_PATH_H
#define PRODTST_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_common.h"
#include "qm_utils_list.h"

#define PRODTST_MAIN_TYPE_MAX_LEN     8
#define PRODTST_HD_TYPE_MAX_LEN  8

typedef struct {
    prodtst_main_type_t main_type;
    prodtst_hd_type_t hd_type;
    int id;
}prodtst_addr_t;

qm_list_t *prodtst_path_unpack(char *path, int len);

int prodtst_path_pack(qm_list_t *addr_list, char *path, int *len);

int prodtst_path_destroy(qm_list_t *addr_list);

int prodtst_addr_list_compare(qm_list_t *a, qm_list_t *b);

int prodtst_addr_compare(prodtst_addr_t *a, prodtst_addr_t *b);

qm_list_t *prodtst_addr_list_copy(qm_list_t *src);

int prodtst_addr_copy(prodtst_addr_t *dest, prodtst_addr_t *src);

prodtst_addr_t *prodtst_addr_first_get(qm_list_t *addr_list);
prodtst_addr_t *prodtst_addr_last_get(qm_list_t *addr_list);

int prodtst_addr_is_dest(qm_list_t *addr_list);

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_PATH_H */
