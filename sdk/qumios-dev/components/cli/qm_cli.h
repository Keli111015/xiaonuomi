#ifndef _QM_CLI_H_
#define _QM_CLI_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "qm_config.h"

#ifndef CONFIG_QM_CLI_TASK_SIZE
#define CONFIG_QM_CLI_TASK_SIZE     (4096)
#endif

#ifndef CONFIG_QM_CLI_TASK_PRIO
#define CONFIG_QM_CLI_TASK_PRIO       (2)
#endif

#ifndef CONFIG_QM_CLI_SUPPORT
#define CONFIG_QM_CLI_SUPPORT         0
#endif

#ifndef CONFIG_QM_CLI_MINI_SIZE
#define CONFIG_QM_CLI_MINI_SIZE       1
#endif

#if CONFIG_QM_CLI_MINI_SIZE

/*can config to cut mem size*/
#ifndef CONFIG_QM_CLI_INBUF_SIZE
#define CONFIG_QM_CLI_INBUF_SIZE              256
#endif
#ifndef CONFIG_QM_CLI_OUTBUF_SIZE
#define CONFIG_QM_CLI_OUTBUF_SIZE             200    /*not use now*/
#endif
#ifndef CONFIG_QM_CLI_MAX_COMMANDS
#define CONFIG_QM_CLI_MAX_COMMANDS            32
#endif
#ifndef CONFIG_QM_CLI_MAX_ARG_NUM
#define CONFIG_QM_CLI_MAX_ARG_NUM               8
#endif
#ifndef CONFIG_QM_CLI_MAX_ONCECMD_NUM
#define CONFIG_QM_CLI_MAX_ONCECMD_NUM           1
#endif

#endif

#if CONFIG_QM_CLI_BIG_SIZE
/*can config to cut mem size*/
#ifndef CONFIG_QM_CLI_INBUF_SIZE
#define CONFIG_QM_CLI_INBUF_SIZE              512
#endif
#ifndef CONFIG_QM_CLI_OUTBUF_SIZE
#define CONFIG_QM_CLI_OUTBUF_SIZE             2048    /*not use now*/
#endif
#ifndef CONFIG_QM_CLI_MAX_COMMANDS
#define CONFIG_QM_CLI_MAX_COMMANDS             64
#endif
#ifndef CONFIG_QM_CLI_MAX_ARG_NUM
#define CONFIG_QM_CLI_MAX_ARG_NUM              16
#endif
#ifndef CONFIG_QM_CLI_MAX_ONCECMD_NUM
#define CONFIG_QM_CLI_MAX_ONCECMD_NUM           6
#endif

#endif

/* Structure for registering CLI commands */
struct qm_cli_command {
    const char *name;
    const char *help;

    void (*function)(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);
};

struct qm_cli_st {
    int initialized;
    int echo_disabled;

    const struct qm_cli_command *commands[CONFIG_QM_CLI_MAX_COMMANDS];

    unsigned int num_commands;
    unsigned int bp; /* buffer pointer */

    char inbuf[CONFIG_QM_CLI_INBUF_SIZE];
    char *outbuf;
    #if(CONFIG_QM_CLI_BIG_SIZE)
    int his_idx;
    int his_cur;
    char history[CONFIG_QM_CLI_INBUF_SIZE];
	#endif
};

/**
 * This function registers a command with the command-line interface.
 *
 * @param[in]  command  The structure to register one CLI command
 *
 * @return  0 on success, error code otherwise.
 */
int qm_cli_register_command(const struct qm_cli_command *command);

/**
 * This function unregisters a command from the command-line interface.
 *
 * @param[in]  command  The structure to unregister one CLI command
 *
 * @return  0 on success,  error code otherwise.
 */
int qm_cli_unregister_command(const struct qm_cli_command *command);

/**
 * Register a batch of CLI commands
 * Often, a module will want to register several commands.
 *
 * @param[in]  commands      Pointer to an array of commands.
 * @param[in]  num_commands  Number of commands in the array.
 *
 * @return  0 on success， error code otherwise.
 */
int qm_cli_register_commands(const struct qm_cli_command *commands, int num_commands);

/**
 * Unregister a batch of CLI commands
 *
 * @param[in]  commands      Pointer to an array of commands.
 * @param[in]  num_commands  Number of commands in the array.
 *
 * @return  0 on success, error code otherwise.
 */
int qm_cli_unregister_commands(const struct qm_cli_command *commands, int num_commands);

/**
 * CLI initial function
 *
 * @return  0 on success, error code otherwise
 */
int qm_cli_init(void);

/**
 * Stop the CLI thread and carry out the cleanup
 *
 * @return  0 on success, error code otherwise.
 *
 */
int qm_cli_stop(void);

/**
 * Print CLI msg
 *
 * @param[in]  buff  Pointer to a char * buffer.
 *
 * @return  0  on success, error code otherwise.
 */
int qm_cli_printf(const char *buff, ...);

#ifdef __cplusplus
}
#endif

#endif



