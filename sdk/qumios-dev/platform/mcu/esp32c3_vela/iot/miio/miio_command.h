#ifndef __MIIO_COMMANDE_H_
#define __MIIO_COMMANDE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "miio_operation.h"

void * miio_cmd_init(void *handle);

void on_property_set(property_operation_t *o);
int on_property_get(property_operation_t *o);
void on_action_invoke(action_operation_t *o);
int do_net_changed(void *handle, void *data, uint32_t len);

int miio_command_find_by_method(void *handle, char *method, void *data, uint32_t len);
int uart_comamnd_decoder(char *pbuf, uint32_t buf_sz, char *method, uint32_t *method_len);


#ifdef __cplusplus
}
#endif

#endif