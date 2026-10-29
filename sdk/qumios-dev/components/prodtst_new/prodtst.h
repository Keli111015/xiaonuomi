#ifndef __PRODTST_H__
#define __PRODTST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_channel.h"
#include "prodtst_common.h"
#include "qm_kernel.h"
#include "qm_utils_timer.h"

#define CONFIG_PRODTST_LOCAL_COMM_MAX_NUM  8

#define CONFIG_PRODTST_CORE_TASK_SIZE   2048
#define CONFIG_PRODTST_CORE_TASK_PRIO   20

#define CONFIG_PRODTST_LOCAL_TASK_SIZE   2048
#define CONFIG_PRODTST_LOCAL_TASK_PRIO   20

#define PRODTST_HUB_UPLINK_UART_ID    PRODTST_HD_UART1
#define PRODTST_CORE_QUEUE_MAX_LEN    32

typedef struct {
    prodtst_comm_msg_type_t msg_type;
    int code;
    uint32_t len;
    uint8_t *data;
}prodtst_data_point_t;

typedef struct {
    int exist;
    uint8_t *buf;
    qm_task_t task;
    prodtst_addr_t addr;
    void *local_handle;
}prodtst_local_comm_t;

int prodtst_init(void);
int prodtst_data_notify(prodtst_comm_msg_type_t msg_type, int code, uint8_t *data, int len);
int prodtst_data_notify_from_isr(prodtst_comm_msg_type_t msg_type, int code, uint8_t *data, int len);
int prodtst_result_report(uint8_t case_id, uint8_t mac[6], uint8_t *data, int len);

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_H */
