
#ifndef _COMM_BASE_H_
#define _COMM_BASE_H_

#ifdef __cplusplus
extern "C"{
#endif

#include "qm_config.h"
#include "qm_types.h"

#ifndef CONFIG_COMM_BASE_CMD_NUM      
#define CONFIG_COMM_BASE_CMD_NUM              5
#endif

#ifndef CONFIG_COMM_BASE_RECV_BUF_MAX_LEN    
#define CONFIG_COMM_BASE_RECV_BUF_MAX_LEN      64
#endif

#ifndef CONFIG_COMM_BASE_RECV_STEP_MAX_NUM   
#define CONFIG_COMM_BASE_RECV_STEP_MAX_NUM     3
#endif

typedef enum{
    COMM_BASE_DIR_DOWN,
    COMM_BASE_DIR_UP,
}comm_base_dir_t;

typedef enum{
    COMM_BASE_EVENT_RESEND,
    COMM_BASE_EVENT_NO_ACK,
}comm_base_event_t;

typedef struct{
    int cmd;
    int id;
    uint8_t *data;
    int len;
}comm_base_data_info_t;

typedef struct {
    comm_base_event_t event;
    comm_base_data_info_t info;
}comm_base_event_info_t;

typedef int (*recv_len_fn)(void);
typedef int (*recv_check_fn)(uint8_t *data, int len);
typedef int (*unpack_fn)(uint8_t *data, int len);
typedef int (*pack_fn)(uint8_t *data, int *len, void *arg);

typedef struct {
    uint32_t recv_timeout;
    int (*send)(uint8_t *data, int len);
    int (*id_get)(uint8_t *data, int len);
    int (*cmd_get)(uint8_t *data, int len);
    int (*check)(uint8_t *data, int len);
    int (*notify)(comm_base_event_info_t *event_info);
}comm_base_param_t;

int comm_base_deinit(void);
int comm_base_init(comm_base_param_t *param);

int comm_base_data_push_from_isr(uint8_t *data, int len);
int comm_base_data_push(uint8_t *data, int len);

int comm_base_cmd_register(int cmd, comm_base_dir_t dir, pack_fn pack, unpack_fn unpack);
int comm_base_recv_register(int step, recv_len_fn recv_len, recv_check_fn recv_check);

int comm_base_cmd_send(int cmd, int send_count, uint32_t timeout, void *arg);


#ifdef __cplusplus
}
#endif

#endif /* COMM_BASE_H */
