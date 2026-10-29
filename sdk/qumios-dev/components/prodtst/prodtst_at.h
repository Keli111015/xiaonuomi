#ifndef PRODTST_AT_H
#define PRODTST_AT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_dt.h"
#include "prodtst_path.h"
#include "qm_at.h"

#ifndef CONFIG_PRODTST_EXT_AT_BUF_MAX_SIZE
#define CONFIG_PRODTST_EXT_AT_BUF_MAX_SIZE          512
#endif

#ifndef CONFIG_PRODTST_EXT_CFG_AT_BUF_MAX_SIZE
#define CONFIG_PRODTST_EXT_CFG_AT_BUF_MAX_SIZE      128
#endif

#ifndef CONFIG_PRODTST_INNER_AT_BUF_MAX_SIZE
#define CONFIG_PRODTST_INNER_AT_BUF_MAX_SIZE        128
#endif


#define PRODTST_AT_RESULT_OK    "ok"
#define PRODTST_AT_RESULT_FAIL  "fail"

typedef struct {
    void *handle;
    int (*read)(void *handle, char *data, int len, int timeout);
    int (*write)(void *handle, char *data, int len);
}prodtst_at_link_t;

extern int prodtst_at_init(void);
extern int prodtst_at_register_commands(qm_at_cmd_t *cmd_tbl, uint16_t cmd_num);
extern int prodtst_at_client_generic_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, prodtst_at_link_t *at_link, prodtst_at_rsp_data_t *at_rsp_data);
extern int prodtst_at_client_inner_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, prodtst_at_rsp_data_t *at_rsp_data);
extern int prodtst_at_client_ext_exec_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, void *addr, prodtst_at_rsp_data_t *at_rsp_data);
extern int prodtst_at_client_ext_exec_cfg_cmd(char *cmd, prodtst_recv_rule_at_t *recv_rule_at, void *addr, prodtst_at_rsp_data_t *at_rsp_data);

#ifdef __cplusplus
}
#endif

#endif