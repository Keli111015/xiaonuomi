#ifndef PRODTST_CHANNEL_H
#define PRODTST_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "prodtst_path.h"
#include "prodtst_common.h"

typedef enum{
    PRODTST_CHANNEL_TYPE_NONE,
    PRODTST_CHANNEL_TYPE_BLE_MESH,
    PRODTST_CHANNEL_TYPE_BLE_CLIENT,
    PRODTST_CHANNEL_TYPE_SUBMODULE,
    PRODTST_CHANNEL_TYPE_BUS,
    PRODTST_CHANNEL_TYPE_UART,
    PRODTST_CHANNEL_TYPE_MAX
}prodtst_channel_type_t;

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
    PRODTST_CHANNEL_CMD_NONE,
    PRODTST_CHANNEL_CMD_CLEAR,
}prodtst_channel_cmd_t;

typedef struct{
    int (*open)(void);
    int (*read)(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg);
    int (*write)(prodtst_addr_t *addr, void *data, int len, void *arg);
    int (*write_cfg)(prodtst_addr_t *addr, void *data, int len, void *arg);
    int (*read_cfg)(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg);
    int (*ctrl)(prodtst_addr_t *addr, prodtst_channel_cmd_t cmd);
}prodtst_channel_handle_t;

typedef struct{
    int (*read)(int id, void *data, int len);
    int (*readtimeout)(int id, void *data, int len, int timeout);
    int (*write)(int id, void *data, int len);
}prodtst_channel_link_t;

int prodtst_channel_register(prodtst_channel_type_t channel_type, prodtst_channel_handle_t *channel_handle);

prodtst_hd_type_t prodtst_hd_type_get(char *str_hd_type);
char *prodtst_hd_type_str_get(prodtst_hd_type_t hd_type);
prodtst_main_type_t prodtst_main_type_get(char *str_main_type);
char *prodtst_main_type_str_get(prodtst_main_type_t main_type);

prodtst_channel_handle_t *prodtst_channel_handle_get(prodtst_channel_type_t channel_type);
prodtst_channel_type_t prodtst_channel_type_get(prodtst_hd_type_t hd_type);

extern int prodtst_channel_open(prodtst_addr_t *addr, void *arg);
extern int prodtst_channel_read(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg);
extern int prodtst_channel_write(prodtst_addr_t *addr, void *data, int len, void *arg);
extern int prodtst_channel_read_cfg(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg);
extern int prodtst_channel_write_cfg(prodtst_addr_t *addr, void *data, int len, void *arg);
extern int prodtst_channel_ctrl(prodtst_addr_t *addr, prodtst_channel_cmd_t cmd);
extern int prodtst_channel_close(prodtst_addr_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* PRODTST_RES_H */

