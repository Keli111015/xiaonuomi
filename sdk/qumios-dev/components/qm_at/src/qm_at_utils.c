#include "qm_at.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_config.h"
#if CONFIG_QM_SPRINTF_SUPPORT
#include "qm_sprintf.h"
#endif
#if CONFIG_QM_AT_PRINT_RAW_CMD
#include "qm_kernel.h"
/**
 * 
 * dump hex format data to console device
 *
 * @param name name for hex object, it will show on log header
 * @param buf hex buffer
 * @param size buffer size
 */
void qm_at_print_raw_cmd(const char *name, const char *buf, uint32_t size)
{
#define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
#define WIDTH_SIZE           32

    uint32_t i, j;

    for (i = 0; i < size; i += WIDTH_SIZE)
    {
        qm_printf("[D/AT] %s: %04X-%04X: ", name, i, i + WIDTH_SIZE);
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                qm_printf("%02X ", buf[i + j]);
            }
            else
            {
                qm_printf("   ");
            }
            if ((j + 1) % 8 == 0)
            {
                qm_printf(" ");
            }
        }
        qm_printf("  ");
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                qm_printf("%c", __is_print(buf[i + j]) ? buf[i + j] : '.');
            }
        }
        qm_printf("\n");
    }
}
#endif

int32_t qm_at_vprintf(void *channel, const char *format, va_list args)
{
    int32_t ret = 0;
    char *big_buf = NULL;
    qm_at_channel_t *at_channel = (qm_at_channel_t*)channel;
    char send_buf[CONFIG_QM_AT_CMD_MAX_LEN] = {0};
    uint32_t last_cmd_len = 0;
    char *buf = send_buf;
    uint32_t len = CONFIG_QM_AT_CMD_MAX_LEN;
#if CONFIG_QM_SPRINTF_SUPPORT
    last_cmd_len = qm_vsnprintf(buf, len, format, args);
#else
    last_cmd_len = vsnprintf(buf, len, format, args);
#endif
    if(last_cmd_len >= len){
        big_buf = (char*)qm_malloc(last_cmd_len+1);
        if(big_buf == NULL){
            return -QM_ENOMEM;
        }
        buf = big_buf;
        memset(big_buf, 0, last_cmd_len + 1);
    #if CONFIG_QM_SPRINTF_SUPPORT
        last_cmd_len = qm_vsnprintf(buf, last_cmd_len+1, format, args);
    #else
        last_cmd_len = vsnprintf(buf, last_cmd_len+1, format, args);
    #endif
#if CONFIG_QM_AT_PRINT_RAW_CMD
        qm_at_print_raw_cmd("sendline", big_buf, last_cmd_len);
#endif
        ret = at_channel->send(at_channel->handle, big_buf, last_cmd_len);
        qm_free(big_buf);
        return ret;

    }else{

#if CONFIG_QM_AT_PRINT_RAW_CMD
        qm_at_print_raw_cmd("sendline", send_buf, last_cmd_len);
#endif
        return at_channel->send(at_channel->handle, send_buf, last_cmd_len);
    }

}

int32_t qm_at_vprintfln(void *channel, const char *format, va_list args)
{
    int32_t ret = 0;
    char *big_buf = NULL;
    qm_at_channel_t *at_channel = (qm_at_channel_t*)channel;
    char send_buf[CONFIG_QM_AT_CMD_MAX_LEN] = {0};
    uint32_t last_cmd_len = 0;
    char *buf = send_buf;
    uint32_t len = CONFIG_QM_AT_CMD_MAX_LEN - 2;
#if CONFIG_QM_SPRINTF_SUPPORT
    last_cmd_len = qm_vsnprintf(buf, len, format, args);
#else
    last_cmd_len = vsnprintf(buf, len, format, args);
#endif
    if(last_cmd_len >= len){
        big_buf = (char*)qm_malloc(last_cmd_len + 1 + 2);
        if(big_buf == NULL){
            return -QM_ENOMEM;
        }
        buf = big_buf;
        memset(big_buf, 0, last_cmd_len + 1 + 2);
    #if CONFIG_QM_SPRINTF_SUPPORT
        last_cmd_len = qm_vsnprintf(buf, last_cmd_len + 1, format, args);
    #else
        last_cmd_len = vsnprintf(buf, last_cmd_len + 1, format, args);
    #endif
        memcpy(big_buf + last_cmd_len, "\r\n", 2);
#if CONFIG_QM_AT_PRINT_RAW_CMD
        qm_at_print_raw_cmd("sendline", big_buf, last_cmd_len + 2);
#endif
        ret = at_channel->send(at_channel->handle, big_buf, last_cmd_len + 2);
        qm_free(big_buf);
        return ret;

    }else{
        memcpy(buf + last_cmd_len, "\r\n", 2);

#if CONFIG_QM_AT_PRINT_RAW_CMD
        qm_at_print_raw_cmd("sendline", send_buf, last_cmd_len+2);
#endif
        return at_channel->send(at_channel->handle, send_buf, last_cmd_len+2);
    }
}
