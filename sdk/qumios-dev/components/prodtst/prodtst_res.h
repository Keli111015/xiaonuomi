#ifndef PRODTST_RES_H
#define PRODTST_RES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "prodtst_path.h"
#include "prodtst_common.h"

typedef enum {
    PRODTST_RES_LOCATION_NONE,
    PRODTST_RES_LOCATION_INNER,
    PRODTST_RES_LOCATION_EXTERNAL
}prodtst_res_location_t;

typedef struct {
    prodtst_hd_type_t hd_type;
    prodtst_res_location_t res_location;
}prodtst_res_dist_t;

typedef struct{
    prodtst_hd_type_t hd_type;
    char *str_hd_type;
}prodtst_hd_type_map_t;

typedef struct{
    prodtst_main_type_t main_type;
    char *str_main_type;
}prodtst_main_type_map_t;

typedef struct{
    prodtst_channel_type_t channel_type;
    char *str_channel_type;
}prodtst_channel_type_map_t;

typedef enum{
    PRODTST_RES_CMD_NONE,
    PRODTST_RES_CMD_CHANNEL_CLEAR,
}prodtst_res_cmd_t;

typedef struct{
    int (*open)(void *addr, void*arg);
    int (*read)(void *addr, void *data, int len, void*arg);
    int (*readtimeout)(void *addr, void *data, int len, int timeout, void*arg);
    int (*write_cfg)(void *addr, void *data, int len);
    int (*read_cfg_timeout)(void *addr, void *data, int len, int timeout);
    int (*write)(void *addr, void *data, int len, void*arg);
    int (*ctrl)(void *addr, int cmd, void *arg);
    int (*close)(void *addr);
}prodtst_res_handle_t;

typedef struct{
    int (*read)(int id, void *data, int len);
    int (*readtimeout)(int id, void *data, int len, int timeout);
    int (*write)(int id, void *data, int len);
}prodtst_res_link_t;

typedef struct prodtst_res{
    prodtst_hd_type_t hd_type;
    prodtst_channel_type_t channel_type;
    prodtst_res_handle_t res_handle;
}prodtst_res_t;

int prodtst_res_register(prodtst_hd_type_t hd_type, prodtst_res_handle_t *res_handle);
qm_list_t *prodtst_res_get_by_hd_type(prodtst_hd_type_t hd_type);

prodtst_channel_type_t prodtst_channel_type_get(char *str_channel_type);
char *prodtst_channel_type_str_get(prodtst_channel_type_t channel_type);
prodtst_hd_type_t prodtst_hd_type_get(char *str_hd_type);
char *prodtst_hd_type_str_get(prodtst_hd_type_t hd_type);
prodtst_main_type_t prodtst_main_type_get(char *str_main_type);
char *prodtst_main_type_str_get(prodtst_main_type_t main_type);
prodtst_res_dist_t *prodtst_res_dist_get(prodtst_hd_type_t hd_type);

extern int prodtst_res_open(prodtst_hd_type_t hd_type, void *addr, void *arg);
extern int prodtst_res_read(prodtst_hd_type_t hd_type, void *addr, void *data, int len, void*arg);
extern int prodtst_res_readtimeout(prodtst_hd_type_t hd_type, void *addr, void *data, int len, int timeout, void*arg);
extern int prodtst_res_write(prodtst_hd_type_t hd_type, void *addr, void *data, int len, void*arg);
extern int prodtst_res_read_cfg_timeout(prodtst_hd_type_t hd_type, void *addr, void *data, int len, int timeout);
extern int prodtst_res_write_cfg(prodtst_hd_type_t hd_type, void *addr, void *data, int len);
extern int prodtst_res_ctrl(prodtst_hd_type_t hd_type, void *addr, int cmd, void *arg);
extern int prodtst_res_close(prodtst_hd_type_t hd_type, void *addr);

#ifdef __cplusplus
}
#endif

#endif /* PRODTST_RES_H */

