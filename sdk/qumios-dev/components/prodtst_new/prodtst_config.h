#ifndef PRODTST_CONFIG_H
#define PRODTST_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "prodtst_common.h"
#include "qm_utils_list.h"

#define PRODTST_TEST_TYPE_MAX_LEN        (8)

typedef struct{
    prodtst_test_type_t test_type;
    char *str_test_type;
}prodtst_test_type_map_t;

typedef struct{
    uint16_t pid;
}prodtst_ble_mesh_config_t;

typedef struct {
    prodtst_addr_t addr;
    char *cfg;
}prodtst_config_t;

typedef struct{
    prodtst_test_type_t test_type;
    int channel_num;
    int config_num;
    prodtst_config_t *config;
}prodtst_config_info_t; 


extern int prodtst_config_unpack(void *json_object, prodtst_config_info_t *config_info);
extern int prodtst_config_pack(prodtst_config_info_t *config_info, void *json_object);
extern int prodtst_config_destroy(prodtst_config_info_t *config_info);
extern int prodtst_config_init(prodtst_config_info_t *config_info);
extern int prodtst_config_deinit(prodtst_config_info_t *config_info);
extern int prodtst_config_active_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
