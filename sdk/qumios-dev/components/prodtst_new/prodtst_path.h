#ifndef PRODTST_PATH_H
#define PRODTST_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_common.h"

#define PRODTST_MAIN_TYPE_MAX_LEN     16
#define PRODTST_HD_TYPE_MAX_LEN       8

int prodtst_path_unpack(char *path, int len, prodtst_addr_t *addr);

int prodtst_path_pack(prodtst_addr_t *addr, char *path, int *len);

int prodtst_addr_copy(prodtst_addr_t *dest, prodtst_addr_t *src);

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_PATH_H */
