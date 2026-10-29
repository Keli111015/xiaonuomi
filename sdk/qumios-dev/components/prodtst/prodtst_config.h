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

typedef enum {
    PRODTST_UART_PARITY_NONE = 0x0,     /*!< Disable UART parity*/
    PRODTST_UART_PARITY_EVEN = 0x1,     /*!< Enable UART even parity*/
    PRODTST_UART_PARITY_ODD  = 0x2      /*!< Enable UART odd parity*/
} prodtst_uart_parity_t;

typedef struct{
    int channel_id;
    prodtst_channel_type_t channel_type;
    int baudrate;
    int databits;
    int parity;
    int stopbits;
}prodtst_uart_config_t;

typedef struct{
    uint16_t pid;
}prodtst_ble_mesh_config_t;

typedef struct {
    prodtst_hd_type_t hd_type;
    qm_list_t *addr_list;
    union {
        prodtst_uart_config_t uart_config;
        prodtst_ble_mesh_config_t ble_mesh_config;
    }cfg;
}prodtst_config_t;

typedef struct{
    prodtst_hd_type_t hd_type;
    int (*unpack)(void *json_object, prodtst_config_t *config_info);
    int (*pack)(prodtst_config_t *config_info, void *json_object);
    int (*destroy)(prodtst_config_t *config_info);
    int (*init)(prodtst_config_t *config_info);
    int (*deinit)(prodtst_config_t *config_info);
}prodtst_config_handle_t;

typedef struct{
    prodtst_test_type_t test_type;
    int button_enable;
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