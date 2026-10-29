#include "qm_ota.h"
#include "qm_log.h"

#include "platform.h"
#include "ota_settings.h"
#include "le501x.h"
#include "ls_hal_flash.h"
#include "reg_base_addr.h"
#include "builtin_timer.h"

#define platform_log(M, ...)       QM_LOGD("ota", M, ##__VA_ARGS__)

#define LE501X_PAGE_SIZE        (256)
#define LE501X_OTA_APP_ADDR_1   (0x18034000)
#define LE501X_OTA_APP_ADDR_2   (0x18034000 + 0x24000)

typedef struct 
{
    uint32_t boot_addr;
    uint32_t start_addr;
    uint32_t offset_addr;

    int write_len;
    uint8_t ota_data_buff[LE501X_PAGE_SIZE];

}le501x_ota_hal_ctx_t;

static struct builtin_timer *ls_timer_inst = NULL;
static le501x_ota_hal_ctx_t g_ota_hal_ctx = {0};


int qm_ota_start(void *arg)
{
    memset(&g_ota_hal_ctx, 0, sizeof(le501x_ota_hal_ctx_t));

    g_ota_hal_ctx.offset_addr = 0;
    g_ota_hal_ctx.boot_addr = LE501X_OTA_APP_ADDR_2;
    g_ota_hal_ctx.start_addr = g_ota_hal_ctx.boot_addr;
    platform_log("ota boot addr  %d!", g_ota_hal_ctx.boot_addr);
    return 0;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{   
    int index = 0;
    int write_len = 0;
    
    while(in_buf_len)
    {
        if(in_buf_len >= LE501X_PAGE_SIZE){
            write_len = LE501X_PAGE_SIZE;
        }else{
            write_len = in_buf_len;
        }
        hal_flash_page_erase(g_ota_hal_ctx.start_addr + index - FLASH_BASE_ADDR);
        hal_flash_multi_io_page_program(g_ota_hal_ctx.start_addr + index - FLASH_BASE_ADDR, in_buf + index, write_len);
        index += write_len;
        in_buf_len -= write_len;
    }

    g_ota_hal_ctx.start_addr += index;
    g_ota_hal_ctx.offset_addr += index;
    *off_set += g_ota_hal_ctx.offset_addr;


    return 0;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    hal_flash_multi_io_read(*off_set + LE501X_OTA_APP_ADDR_2 - FLASH_BASE_ADDR, out_buf, out_buf_len);
    *off_set += out_buf_len;
    return 0;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    memset(&g_ota_hal_ctx, 0, sizeof(le501x_ota_hal_ctx_t));
    return 0;
}

static void ota_reboot(void *param)
{
    ota_boot_addr_set(LE501X_OTA_APP_ADDR_1);   
    platform_log("ota erase req !");
    ota_settings_erase_req_set();
    struct fota_copy_info copy_info =
    {
        .fw_copy_src_addr = LE501X_OTA_APP_ADDR_2,
        .fw_copy_dst_addr = LE501X_OTA_APP_ADDR_1,
        .fw_copy_size = g_ota_hal_ctx.start_addr - LE501X_OTA_APP_ADDR_2,
    };
    platform_log("ota copy size %d!",g_ota_hal_ctx.start_addr - LE501X_OTA_APP_ADDR_2);
    ota_copy_info_set(&copy_info);
    platform_log("Prepare to restart system!");
    platform_reset(RESET_OTA_SUCCEED);    
}

int qm_ota_end(void *arg)
{
    ls_timer_inst = builtin_timer_create(ota_reboot);
    builtin_timer_start(ls_timer_inst, 200, NULL);

    return 0;
}
