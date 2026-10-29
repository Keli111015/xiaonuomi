#include "qm_at.h"
#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_QM_AT_SERVER 

#if CONFIG_QM_AT_SERVER_BASE_CMD_SUPPORT

static qm_at_result_t at_exec(void *channel);
static qm_at_result_t ate_setup(void *channel, const char *args);

static qm_at_cmd_t base_at_cmd[] = {
    {"AT", NULL, NULL, NULL, NULL, at_exec},
    {"ATE", "<value>", NULL, NULL, ate_setup, NULL},
};

#define QM_BASE_AT_CMD_NUM (sizeof(base_at_cmd) / sizeof(base_at_cmd[0]))

static qm_at_result_t at_exec(void *channel)
{
    return QM_AT_RESULT_OK;
}

static qm_at_result_t ate_setup(void *channel, const char *args)
{
    int echo_mode = atoi(args);
    
    if(echo_mode == QM_AT_ECHO_MODE_CLOSE || echo_mode == QM_AT_ECHO_MODE_OPEN)
    {
        qm_at_server_echo_mode_set(channel, echo_mode);
    }
    else
    {
        return QM_AT_RESULT_FAILE;
    }

    return QM_AT_RESULT_OK;
}


int32_t qm_at_base_cmd_init(qm_at_server_t *server)
{
    return qm_at_register_commands(server, base_at_cmd, QM_BASE_AT_CMD_NUM);
}

#endif

#endif
